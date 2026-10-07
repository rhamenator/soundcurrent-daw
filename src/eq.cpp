// SPDX-License-Identifier: GPL-3.0-only
// The biquad recurrence/tiny-state zeroing is adapted from Studio src/dsp.cpp.
// DAW preparation, planar buffers, event transport and ramps are new here.
// See reuse/studio/provenance.json for the pinned origin and modifications.
#include <soundcurrent/eq.hpp>
#include <algorithm>
#include <cmath>
#include <limits>

namespace soundcurrent::daw {
PreparedEq::PreparedEq(const Session &session, const Id &trackId, std::uint32_t maxFrames,
                       std::uint64_t generation, double smoothingMs)
    : PreparedEq(ValidatedSession(session), trackId, maxFrames, generation, smoothingMs) {}
PreparedEq::PreparedEq(const ValidatedSession &validated, const Id &trackId,
                       std::uint32_t maxFrames, std::uint64_t generation, double smoothingMs)
    : trackId_(trackId), processorId_(validated.session().id),
      sampleRate_(validated.session().sampleRate), channels_(0), maxFrames_(maxFrames),
      smoothingFrames_(0), generation_(generation) {
    if (!maxFrames || maxFrames > 65536 || !generation || !std::isfinite(smoothingMs) ||
        smoothingMs < 1 || smoothingMs > 20)
        throw ProjectError(ErrorCode::InvalidState, "Invalid EQ preparation contract");
    const auto *t = &validated.track(trackId);
    processorId_ = t->eq.id;
    channels_ = t->layout.channels;
    layout_ = t->layout;
    smoothingFrames_ = static_cast<std::uint32_t>(std::ceil(smoothingMs * sampleRate_ / 1000.0));
    wet_ = wetTarget_ = t->eq.enabled ? 1 : 0;
    bandIds_.reserve(t->eq.bands.size());
    bands_.reserve(t->eq.bands.size());
    for (const auto &b : t->eq.bands) {
        bandIds_.push_back(b.id);
        const auto c = preparePeakingCoefficients(b.frequencyHz, b.gainDb, b.q, sampleRate_);
        bands_.push_back({c, c, {}, 0});
    }
    states_.resize(bands_.size() * channels_);
}
EqEvent PreparedEq::parameterEvent(const Session &updated, const ParameterAddress &address,
                                   Frame at) const {
    validate(updated);
    if (at < 0 || updated.sampleRate != sampleRate_ || address.trackId != trackId_ ||
        address.processorId != processorId_)
        throw ProjectError(ErrorCode::InvalidParameter,
                           "Parameter event belongs to another prepared plan");
    (void)descriptor(address.parameter);
    const auto i = std::find(bandIds_.begin(), bandIds_.end(), address.bandId);
    if (i == bandIds_.end())
        throw ProjectError(ErrorCode::InvalidParameter, "Unknown prepared band");
    for (const auto &t : updated.tracks)
        if (t.id == trackId_ && t.eq.id == processorId_) {
            if (t.layout != layout_ || t.eq.bands.size() != bandIds_.size())
                throw ProjectError(ErrorCode::InvalidParameter, "Layout needs reprepare");
            for (const auto &id : bandIds_)
                if (std::none_of(t.eq.bands.begin(), t.eq.bands.end(),
                                 [&](const EqBand &b) { return b.id == id; }))
                    throw ProjectError(ErrorCode::InvalidParameter,
                                       "Band structure needs reprepare");
            for (const auto &b : t.eq.bands)
                if (b.id == address.bandId)
                    return {at, generation_,
                            preparePeakingCoefficients(b.frequencyHz, b.gainDb, b.q, sampleRate_),
                            static_cast<std::uint16_t>(i - bandIds_.begin()), EqEventKind::Band};
        }
    throw ProjectError(ErrorCode::InvalidParameter, "Parameter no longer exists");
}
EqEvent PreparedEq::enableEvent(bool enabled, Frame at) const {
    if (at < 0)
        throw ProjectError(ErrorCode::InvalidParameter, "Negative event timestamp");
    return {at, generation_, {{enabled ? 1. : 0., 0, 0, 0, 0}}, 0, EqEventKind::Enable};
}
bool PreparedEq::validEvent(const EqEvent &event) const noexcept {
    if (event.frame < 0 || event.generation != generation_)
        return false;
    const auto &v = event.coefficients.values;
    if (event.kind == EqEventKind::Enable)
        return event.band == 0 && (v[0] == 0 || v[0] == 1) && v[1] == 0 && v[2] == 0 && v[3] == 0 &&
               v[4] == 0;
    if (event.kind != EqEventKind::Band || event.band >= bands_.size())
        return false;
    for (auto x : v)
        if (!std::isfinite(x) || std::abs(x) > 64)
            return false;
    return 1 + v[3] + v[4] > 0 && 1 - v[3] + v[4] > 0 && 1 - v[4] > 0;
}
void PreparedEq::apply(const EqEvent &event) noexcept {
    if (event.kind == EqEventKind::Enable) {
        wetTarget_ = event.coefficients.values[0];
        wetRemaining_ = smoothingFrames_;
        wetStep_ = (wetTarget_ - wet_) / smoothingFrames_;
        return;
    }
    auto &b = bands_[event.band];
    b.target = event.coefficients;
    if (!b.remaining)
        ++activeBandRamps_;
    b.remaining = smoothingFrames_;
    for (std::size_t i = 0; i < 5; ++i)
        b.step.values[i] = (b.target.values[i] - b.current.values[i]) / smoothingFrames_;
}
void PreparedEq::advance() noexcept {
    if (activeBandRamps_)
        for (auto &b : bands_)
            if (b.remaining) {
                --b.remaining;
                if (!b.remaining) {
                    b.current = b.target;
                    --activeBandRamps_;
                } else
                    for (std::size_t i = 0; i < 5; ++i)
                        b.current.values[i] += b.step.values[i];
            }
    if (wetRemaining_) {
        --wetRemaining_;
        wet_ = wetRemaining_ ? wet_ + wetStep_ : wetTarget_;
    }
}
void PreparedEq::resetChannel(std::uint32_t channel) noexcept {
    for (std::size_t b = 0; b < bands_.size(); ++b)
        states_[b * channels_ + channel] = {};
}
void PreparedEq::reset() noexcept {
    std::fill(states_.begin(), states_.end(), State{});
    for (auto &b : bands_) {
        b.current = b.target;
        b.remaining = 0;
    }
    wet_ = wetTarget_;
    wetRemaining_ = 0;
    activeBandRamps_ = 0;
}
EqReport PreparedEq::process(std::span<const float *const> input, std::span<float *const> output,
                             std::uint32_t frames, Frame start,
                             std::span<const EqEvent> events) noexcept {
    EqReport report;
    if (frames > maxFrames_ || input.size() != channels_ || output.size() != channels_) {
        report.status = ProcessStatus::InvalidBuffer;
        return report;
    }
    for (std::uint32_t c = 0; c < channels_; ++c)
        if (!input[c] || !output[c]) {
            report.status = ProcessStatus::InvalidBuffer;
            return report;
        }
    if (start < 0 || start > std::numeric_limits<Frame>::max() - frames) {
        report.status = ProcessStatus::TimingError;
        return report;
    }
    if (events.size() > maxEqEventsPerBlock) {
        report.status = ProcessStatus::EventBudgetExceeded;
        return report;
    }
    Frame previous = start;
    for (const auto &event : events) {
        if (!validEvent(event) || event.frame < previous || event.frame >= start + frames) {
            report.status = ProcessStatus::InvalidEvent;
            return report;
        }
        previous = event.frame;
    }
    std::size_t next = 0;
    for (std::uint32_t frame = 0; frame < frames; ++frame) {
        while (next < events.size() && events[next].frame == start + frame) {
            apply(events[next++]);
            ++report.eventsApplied;
        }
        // First smoothing step still applies at the accepted event sample.
        // Settled plans avoid a second complete band traversal on every sample.
        if (activeBandRamps_ || wetRemaining_)
            advance();
        for (std::uint32_t channel = 0; channel < channels_; ++channel) {
            double dry = input[channel][frame];
            if (!std::isfinite(dry)) {
                dry = 0;
                ++report.invalidInputSamples;
            }
            double value = dry;
            bool fault = false;
            for (std::size_t b = 0; b < bands_.size(); ++b) {
                const auto &c = bands_[b].current.values;
                auto &state = states_[b * channels_ + channel];
                const double y = c[0] * value + state.z1;
                state.z1 = c[1] * value - c[3] * y + state.z2;
                state.z2 = c[2] * value - c[4] * y;
                if (!std::isfinite(y) || !std::isfinite(state.z1) || !std::isfinite(state.z2)) {
                    fault = true;
                    value = 0;
                    break;
                }
                if (std::abs(state.z1) < 1e-30)
                    state.z1 = 0;
                if (std::abs(state.z2) < 1e-30)
                    state.z2 = 0;
                value = y;
            }
            value = wet_ == 1 ? value : wet_ == 0 ? dry : dry + wet_ * (value - dry);
            if (fault || !std::isfinite(value) ||
                std::abs(value) > std::numeric_limits<float>::max()) {
                ++report.numericFaultSamples;
                value = wet_ == 0 ? dry : 0;
                resetChannel(channel);
            }
            output[channel][frame] = static_cast<float>(value);
            report.peak = std::max(report.peak, std::abs(value));
        }
    }
    return report;
}
EqLiveDriver::EqLiveDriver(PreparedEq &eq, Frame start) : eq_(eq), frame_(start) {
    if (start < 0)
        throw ProjectError(ErrorCode::InvalidState, "Invalid live driver start");
}
SubmitStatus EqLiveDriver::submit(const EqEvent &event) noexcept {
    if (!eq_.validEvent(event))
        return SubmitStatus::Invalid;
    if (submitted_ && event.frame < lastSubmitted_)
        return SubmitStatus::OutOfOrder;
    if (!queue_.tryPush(event))
        return SubmitStatus::Full;
    lastSubmitted_ = event.frame;
    submitted_ = true;
    return SubmitStatus::Accepted;
}
SubmitStatus EqLiveDriver::submitImmediate(const EqEvent &event, std::uint64_t revision) noexcept {
    auto prepared = event;
    prepared.frame = 0;
    if (!revision || !eq_.validEvent(prepared))
        return SubmitStatus::Invalid;
    if (revision <= lastImmediateRevision_)
        return SubmitStatus::OutOfOrder;
    if (!immediate_.tryPush({prepared, revision}))
        return SubmitStatus::Full;
    lastImmediateRevision_ = revision;
    return SubmitStatus::Accepted;
}
bool EqLiveDriver::acknowledgement(ImmediateAcknowledgement &result) noexcept {
    return acknowledgements_.tryPop(result);
}
std::uint64_t EqLiveDriver::droppedAcknowledgements() const noexcept {
    return droppedAcknowledgements_.load(std::memory_order_relaxed);
}
EqReport EqLiveDriver::process(std::span<const float *const> input, std::span<float *const> output,
                               std::uint32_t frames) noexcept {
    if (stopped_)
        return {ProcessStatus::Stopped};
    if (frames > eq_.maxFrames() || input.size() != eq_.channels() ||
        output.size() != eq_.channels())
        return {ProcessStatus::InvalidBuffer};
    for (std::uint32_t c = 0; c < eq_.channels(); ++c)
        if (!input[c] || !output[c])
            return {ProcessStatus::InvalidBuffer};
    if (frame_ > std::numeric_limits<Frame>::max() - frames) {
        stopped_ = true;
        return {ProcessStatus::TimingError};
    }
    const auto end = frame_ + frames;
    // A zero-length or rejected block must not consume control edits.
    const auto immediateCount = frames ? immediate_.consumerAvailable() : 0;
    std::size_t count = 0;
    EqEvent event;
    while (count < maxEqTimedEventsPerBlock && queue_.tryPeek(event) && event.frame < end) {
        if (event.frame < frame_) {
            stopped_ = true;
            return {ProcessStatus::TimingError};
        }
        (void)queue_.tryPop(ready_[count++]);
    }
    if (count == maxEqTimedEventsPerBlock && queue_.tryPeek(event) && event.frame < end) {
        stopped_ = true;
        return {ProcessStatus::EventBudgetExceeded};
    }
    // Timed events at the boundary first, then immediate FIFO edits, then
    // later sample-timed automation. No reads of audio-owned fields on control.
    ImmediateUpdate update{};
    if (immediateCount) {
        const auto boundaryEnd = std::find_if(ready_.begin(), ready_.begin() + count,
                                              [&](const EqEvent &e) { return e.frame != frame_; });
        std::move_backward(boundaryEnd, ready_.begin() + count,
                           ready_.begin() + count + immediateCount);
        auto insertion = boundaryEnd;
        for (std::uint32_t i = 0; i < immediateCount; ++i) {
            (void)immediate_.tryPop(update); // Single consumer; snapshotted prefix exists.
            update.event.frame = frame_;
            *insertion++ = update.event;
        }
    }
    count += immediateCount;
    auto result = eq_.process(input, output, frames, frame_, std::span(ready_).first(count));
    if (result.status == ProcessStatus::Ok) {
        if (immediateCount &&
            !acknowledgements_.tryPush({eq_.generation(), update.revision, frame_, immediateCount}))
            droppedAcknowledgements_.fetch_add(1, std::memory_order_relaxed);
        frame_ = end;
    } else
        stopped_ = true;
    return result;
}
} // namespace soundcurrent::daw
