// SPDX-License-Identifier: GPL-3.0-only
#include <soundcurrent/duplex_bridge.hpp>
#include <algorithm>
#include <limits>

namespace soundcurrent::daw {
static_assert(std::atomic<Frame>::is_always_lock_free);
static_assert(std::atomic<std::uint32_t>::is_always_lock_free);
namespace {
bool active(DuplexStatus s) noexcept {
    return s == DuplexStatus::Ready || s == DuplexStatus::Running || s == DuplexStatus::Underflow;
}
DuplexStatus convert(PlaybackStatus s) noexcept {
    switch (s) {
    case PlaybackStatus::Running:
        return DuplexStatus::Running;
    case PlaybackStatus::Underflow:
        return DuplexStatus::Underflow;
    case PlaybackStatus::Complete:
        return DuplexStatus::Complete;
    case PlaybackStatus::Stopped:
        return DuplexStatus::Stopped;
    case PlaybackStatus::ReaderFailed:
        return DuplexStatus::ReaderFailed;
    case PlaybackStatus::InvalidBuffer:
        return DuplexStatus::BufferUnavailable;
    case PlaybackStatus::TimingError:
        return DuplexStatus::ClockDiscontinuity;
    case PlaybackStatus::ProcessorFailed:
        return DuplexStatus::ProcessorFailed;
    }
    return DuplexStatus::ProcessorFailed;
}
CaptureEndReason reason(DuplexStatus s, CaptureStatus capture) noexcept {
    switch (s) {
    case DuplexStatus::Complete:
        return CaptureEndReason::RangeComplete;
    case DuplexStatus::RateChanged:
        return CaptureEndReason::RateChanged;
    case DuplexStatus::QuantumExceeded:
        return CaptureEndReason::QuantumExceeded;
    case DuplexStatus::ClockDiscontinuity:
        return CaptureEndReason::ClockDiscontinuity;
    case DuplexStatus::BufferUnavailable:
    case DuplexStatus::DeviceLost:
        return CaptureEndReason::DeviceLost;
    case DuplexStatus::CaptureFailed:
        return capture == CaptureStatus::WriterFailed ? CaptureEndReason::WriterFailed
                                                      : CaptureEndReason::CaptureFailed;
    case DuplexStatus::ReaderFailed:
    case DuplexStatus::ProcessorFailed:
        return CaptureEndReason::ProcessorFailed;
    default:
        return CaptureEndReason::UserStop;
    }
}
} // namespace
std::size_t armedCapturePayloadBytes(CaptureConfig config, std::size_t inputs) {
    config = prepareCaptureConfig(config);
    if (inputs != config.layout.channels)
        throw ProjectError(ErrorCode::InvalidState, "Invalid armed input shape");
    PayloadCharge payload("Armed capture declaration", SIZE_MAX);
    payload.add(capturePayloadBytes(config));
    payload.add(8192);
    payload.add(inputs, sizeof(std::uint32_t) + 2 * sizeof(float *));
    return payload.bytes();
}
struct DuplexBridge::State {
    struct Lane {
        ArmedCapture binding;
        std::vector<const float *> input;
        std::vector<const float *> captureInput;
        std::atomic<Frame> captured{0};
        PunchRange range;
        bool windowed = false;
        Frame captureAt = 0;
        std::uint32_t captureFrames = 0, offset = 0;
        bool originSet = false; // Audio-owner only; pipe publishes immutable origin.
        Lane(ArmedCapture b, PunchRange r, bool w)
            : binding(std::move(b)), input(binding.inputChannels.size()),
              captureInput(binding.inputChannels.size()), range(r), windowed(w) {}
    };
    ResourceLease resourceLease;
    MixPlaybackRun &run;
    std::uint32_t nativeInputs;
    CaptureBackend backend;
    std::vector<std::unique_ptr<Lane>> lanes;
    std::vector<LiveMixInput> live;
    std::atomic<std::uint32_t> terminal{0}, faultReady{0}, originReady{0};
    DeviceBlockClock previous{};
    DuplexCallbackFault fault{};
    CaptureTimingOrigin origin{};
    bool started = false;
    SpscQueue<DuplexObservation, 64> observations;
    std::atomic<std::uint64_t> dropped{0};
    State(MixPlaybackRun &r, std::uint32_t n, CaptureBackend b)
        : run(r), nativeInputs(n), backend(b) {}
    DuplexStatus status() const noexcept {
        return static_cast<DuplexStatus>(terminal.load(std::memory_order_acquire));
    }
    DuplexStatus finish(DuplexStatus desired) noexcept {
        auto prior = terminal.load(std::memory_order_acquire);
        if (active(static_cast<DuplexStatus>(prior)))
            terminal.compare_exchange_strong(prior, static_cast<std::uint32_t>(desired),
                                             std::memory_order_acq_rel, std::memory_order_acquire);
        const auto result = status();
        if (!active(result)) {
            run.requestStop();
            run.cancelReader();
            for (auto &l : lanes)
                l->binding.pipe->finish(reason(result, l->binding.pipe->status()));
        }
        return result;
    }
    void record(const DeviceBlockClock &clock, DuplexStatus detected, Frame position,
                std::size_t inputs, std::size_t outputs, std::uint32_t capacity,
                std::size_t capture = SIZE_MAX) noexcept {
        if (faultReady.load(std::memory_order_relaxed))
            return;
        const auto &c = run.config().graph;
        fault = {clock,           previous,
                 detected,        position,
                 c.generation,    run.sampleRate(),
                 c.maximumFrames, capacity,
                 nativeInputs,    run.graph().plan().output.channels,
                 inputs,          outputs,
                 capture,         started};
        faultReady.store(1, std::memory_order_release);
    }
};
std::size_t DuplexBridge::bindingPayloadBytes(std::span<const ArmedCapture> arms,
                                              std::size_t budget) {
    PayloadCharge bridgeCharge("Duplex bridge bindings", budget);
    bridgeCharge.add(sizeof(State) + 8192);
    for (const auto &a : arms)
        bridgeCharge.add(1, sizeof(State::Lane) + sizeof(LiveMixInput) + 8192 +
                                a.inputChannels.size() *
                                    (sizeof(std::uint32_t) + 2 * sizeof(float *)));
    return bridgeCharge.bytes();
}
DuplexBridge::DuplexBridge(MixPlaybackRun &r, const Session &s, std::vector<ArmedCapture> arms,
                           std::uint32_t inputs, CaptureBackend backend, std::size_t budget,
                           std::optional<PunchRange> punch)
    : state_(std::make_unique<State>(r, inputs, backend)) {
    auto &st = *state_;
    validate(s);
    const auto &c = r.config().graph;
    if (punch && (punch->begin < c.startFrame || punch->begin >= punch->end ||
                  punch->end > r.config().endFrame))
        throw ProjectError(ErrorCode::InvalidState, "Invalid prepared punch range");
    if (arms.empty() || arms.size() > 256 || !inputs || inputs > 256 ||
        backend > CaptureBackend::Asio || s.sampleRate != r.sampleRate() ||
        r.position() != c.startFrame)
        throw ProjectError(ErrorCode::InvalidState, "Invalid duplex preparation");
    if (!budget)
        throw ProjectError(ErrorCode::InvalidState, "Invalid duplex memory budget");
    // Query immutable prepared declarations, never a later caller model or mutable cache stats.
    const auto bridgeBytes = bindingPayloadBytes(arms, budget);
    PayloadCharge total("Duplex prepared payload", budget);
    total.add(r.payloadBytes());
    total.add(bridgeBytes);
    auto payload = total.bytes();
    if (c.resources)
        st.resourceLease = c.resources->reserve(bridgeBytes);
    if (payload > budget)
        throw ProjectError(ErrorCode::InvalidState, "Duplex playback reservation exceeds budget");
    for (const auto &a : arms) {
        if (!a.pipe)
            throw ProjectError(ErrorCode::InvalidState, "Missing armed capture pipe");
        // The bridge declaration above owns the bindings. Each external pipe
        // contributes only its pool/object, matching the separate ledger lease.
        const auto bytes = capturePayloadBytes(a.pipe->config());
        if (bytes > budget - payload)
            throw ProjectError(ErrorCode::InvalidState, "Duplex capture payload exceeds budget");
        payload += bytes;
    }
    for (auto &a : arms) {
        if (a.captureRange && punch)
            throw ProjectError(ErrorCode::InvalidState, "Conflicting shared and lane punch ranges");
        const auto range =
            a.captureRange.value_or(punch.value_or(PunchRange{c.startFrame, r.config().endFrame}));
        if (range.begin < c.startFrame || range.begin >= range.end ||
            range.end > r.config().endFrame)
            throw ProjectError(ErrorCode::InvalidState, "Invalid armed capture range");
        const auto captureStart = range.begin;
        const auto t = std::find_if(s.tracks.begin(), s.tracks.end(),
                                    [&](const auto &t) { return t.id == a.track; });
        if (!a.pipe || a.pipe->config().deferredStart || t == s.tracks.end() ||
            a.inputChannels.size() != t->layout.channels || !validRecordingMonitor(a.monitoring) ||
            (a.monitorRange && (a.monitoring != RecordingMonitor::AutoRecording ||
                                a.monitorRange->begin < c.startFrame ||
                                a.monitorRange->begin >= a.monitorRange->end ||
                                a.monitorRange->end > r.config().endFrame)) ||
            a.pipe->config().sampleRate != s.sampleRate || a.pipe->config().layout != t->layout ||
            a.pipe->config().startFrame != captureStart ||
            a.pipe->config().maximumCallbackFrames < c.maximumFrames ||
            a.pipe->status() != CaptureStatus::Running || a.pipe->producerDone() ||
            a.pipe->timingOrigin() || a.pipe->nextFrame() != captureStart ||
            std::any_of(
                a.inputChannels.begin(), a.inputChannels.end(),
                [&](auto n) { return n >= inputs; }) ||
            std::any_of(
                st.lanes.begin(), st.lanes.end(),
                [&](const auto &l) {
                    return l->binding.track == a.track || l->binding.pipe == a.pipe;
                }))
            throw ProjectError(ErrorCode::InvalidState, "Invalid armed capture binding");
        const bool windowed = a.captureRange.has_value() || punch.has_value();
        auto lane = std::make_unique<State::Lane>(std::move(a), range, windowed);
        if (lane->binding.monitoring != RecordingMonitor::Off) {
            const auto &tracks = r.graph().plan().tracks;
            const auto mapped = std::find_if(tracks.begin(), tracks.end(), [&](const auto &t) {
                return t.track == lane->binding.track;
            });
            if (mapped == tracks.end() ||
                r.graph().prepared(std::size_t(mapped - tracks.begin())).channels() !=
                    lane->input.size())
                throw ProjectError(ErrorCode::InvalidState,
                                   "Monitored capture has no explicit mix lane");
            auto live = LiveMixInput{std::size_t(mapped - tracks.begin()), lane->input};
            if (lane->binding.monitoring == RecordingMonitor::AutoRecording) {
                const auto monitor = lane->binding.monitorRange.value_or(range);
                live.beginFrame = monitor.begin;
                live.endFrame = monitor.end;
            }
            st.live.push_back(live);
        }
        st.lanes.push_back(std::move(lane));
    }
}
DuplexBridge::~DuplexBridge() = default;
DuplexStatus DuplexBridge::status() const noexcept {
    return state_->status();
}
void DuplexBridge::requestFault(DuplexStatus desired) noexcept {
    if (active(desired) || desired == DuplexStatus::Complete ||
        desired > DuplexStatus::CaptureFailed)
        return;
    auto &s = *state_;
    auto prior = s.terminal.load(std::memory_order_acquire);
    // Control owner only. Running/Underflow may alternate; retry until the
    // first terminal winner is known. The callback uses one bounded comparison.
    while (active(static_cast<DuplexStatus>(prior)))
        if (s.terminal.compare_exchange_strong(prior, static_cast<std::uint32_t>(desired),
                                               std::memory_order_acq_rel,
                                               std::memory_order_acquire)) {
            s.run.requestStop();
            return;
        }
}
void DuplexBridge::requestStop() noexcept {
    requestFault(DuplexStatus::Stopped);
}
void DuplexBridge::finishQuiescent() noexcept {
    state_->finish(active(status()) ? DuplexStatus::Stopped : status());
}
Frame DuplexBridge::capturedFrames(std::size_t n) const {
    if (n >= state_->lanes.size())
        throw ProjectError(ErrorCode::InvalidParameter, "Capture lane missing");
    return state_->lanes[n]->captured.load(std::memory_order_acquire);
}
std::optional<DuplexCallbackFault> DuplexBridge::callbackFault() const noexcept {
    if (!state_->faultReady.load(std::memory_order_acquire))
        return {};
    return state_->fault;
}
std::optional<CaptureTimingOrigin> DuplexBridge::timingOrigin() const noexcept {
    if (!state_->originReady.load(std::memory_order_acquire))
        return {};
    return state_->origin;
}
bool DuplexBridge::observation(DuplexObservation &o) noexcept {
    return state_->observations.tryPop(o);
}
std::uint64_t DuplexBridge::droppedObservations() const noexcept {
    return state_->dropped.load(std::memory_order_acquire);
}
DuplexStatus DuplexBridge::process(const DeviceBlockClock &clock,
                                   std::span<const float *const> input,
                                   std::span<float *const> output,
                                   std::uint32_t capacity) noexcept {
    auto &s = *state_;
    const auto silence = [&] {
        for (auto *p : output)
            if (p)
                std::fill_n(p, capacity, 0.f);
    };
    auto next = status();
    if (!active(next)) {
        silence();
        return s.finish(next);
    }
    const auto &c = s.run.config().graph;
    const auto at = s.run.position();
    if (!clock.duration || clock.duration > c.maximumFrames || clock.duration > capacity)
        next = DuplexStatus::QuantumExceeded;
    else if (clock.rateNumerator != 1 || clock.rateDenominator != s.run.sampleRate())
        next = DuplexStatus::RateChanged;
    else if (input.size() != s.nativeInputs ||
             output.size() != s.run.graph().plan().output.channels)
        next = DuplexStatus::BufferUnavailable;
    else if (clock.xrun || clock.discontinuity || clock.position > UINT64_MAX - clock.duration ||
             (s.started && (clock.id != s.previous.id ||
                            clock.position != s.previous.position + s.previous.duration)))
        next = DuplexStatus::ClockDiscontinuity;
    else if (std::any_of(input.begin(), input.end(), [](auto *p) { return !p; }) ||
             std::any_of(output.begin(), output.end(), [](auto *p) { return !p; })) {
        if (!s.started && std::all_of(input.begin(), input.end(), [](auto *p) { return !p; }) &&
            std::all_of(output.begin(), output.end(), [](auto *p) { return !p; }))
            return next;
        next = DuplexStatus::BufferUnavailable;
    }
    if (active(next))
        for (std::size_t n = 0; n < output.size(); ++n)
            for (std::size_t previous = 0; previous < n; ++previous)
                if (output[n] == output[previous])
                    next = DuplexStatus::BufferUnavailable;
    if (!active(next)) {
        s.record(clock, next, at, input.size(), output.size(), capacity);
        silence();
        return s.finish(next);
    }
    const auto frames = static_cast<std::uint32_t>(
        std::min<Frame>(Frame(clock.duration), s.run.config().endFrame - at));
    // Preflight every lane's first-origin offset before publishing any origin or
    // raw sample. Playback/native clocks stay monotonic through preroll/postroll.
    auto earliestOffset = UINT32_MAX;
    for (auto &lane : s.lanes) {
        auto &l = *lane;
        l.captureAt = std::max(at, l.range.begin);
        const auto end = std::min(at + frames, l.range.end);
        l.captureFrames = static_cast<std::uint32_t>(std::max<Frame>(0, end - l.captureAt));
        l.offset = l.captureFrames ? static_cast<std::uint32_t>(l.captureAt - at) : 0;
        if (!l.captureFrames || l.originSet)
            continue;
        const auto offsetNs = std::uint64_t(l.offset) * 1000000000ULL / s.run.sampleRate();
        if (clock.monotonicNs && clock.monotonicNs > UINT64_MAX - offsetNs) {
            s.record(clock, DuplexStatus::ClockDiscontinuity, at, input.size(), output.size(),
                     capacity);
            silence();
            return s.finish(DuplexStatus::ClockDiscontinuity);
        }
        earliestOffset = std::min(earliestOffset, l.offset);
    }
    const auto originAt = [&](std::uint32_t offset) noexcept {
        const auto offsetNs = std::uint64_t(offset) * 1000000000ULL / s.run.sampleRate();
        return CaptureTimingOrigin{s.backend,
                                   clock.position + offset,
                                   clock.monotonicNs ? clock.monotonicNs + offsetNs : 0,
                                   c.generation,
                                   clock.id,
                                   clock.cycle,
                                   clock.rateNumerator,
                                   clock.rateDenominator,
                                   clock.delay};
    };
    if (earliestOffset != UINT32_MAX) {
        for (std::size_t n = 0; n < s.lanes.size(); ++n) {
            auto &l = *s.lanes[n];
            if (!l.captureFrames || l.originSet)
                continue;
            if (!l.binding.pipe->setTimingOrigin(originAt(l.offset))) {
                s.record(clock, DuplexStatus::CaptureFailed, at, input.size(), output.size(),
                         capacity, n);
                silence();
                return s.finish(DuplexStatus::CaptureFailed);
            }
            l.originSet = true;
        }
        if (!s.originReady.load(std::memory_order_relaxed)) {
            s.origin = originAt(earliestOffset);
            s.originReady.store(1, std::memory_order_release);
        }
    }
    // Raw publication precedes all EQ/matrix output, including aliased native views.
    std::size_t failed = SIZE_MAX;
    for (std::size_t n = 0; n < s.lanes.size(); ++n) {
        auto &l = *s.lanes[n];
        for (std::size_t channel = 0; channel < l.input.size(); ++channel)
            l.input[channel] = input[l.binding.inputChannels[channel]];
        if (l.captureFrames) {
            auto captured = std::span<const float *const>(l.input);
            if (l.offset) {
                for (std::size_t channel = 0; channel < l.input.size(); ++channel)
                    l.captureInput[channel] = l.input[channel] + l.offset;
                captured = l.captureInput;
            }
            const auto r = l.binding.pipe->push(captured, l.captureFrames, l.captureAt);
            l.captured.fetch_add(r.acceptedFrames, std::memory_order_release);
            if (r.status != CaptureStatus::Running && failed == SIZE_MAX)
                failed = n;
        }
    }
    if (failed != SIZE_MAX) {
        s.record(clock, DuplexStatus::CaptureFailed, at, input.size(), output.size(), capacity,
                 failed);
        silence();
        return s.finish(DuplexStatus::CaptureFailed);
    }
    for (const auto &lane : s.lanes)
        if (lane->windowed && lane->captureFrames &&
            lane->captureAt + lane->captureFrames == lane->range.end)
            lane->binding.pipe->finish(CaptureEndReason::RangeComplete);
    const auto report = s.run.process(output, static_cast<std::uint32_t>(clock.duration), s.live);
    next = convert(report.status);
    if (!active(next) && next != DuplexStatus::Complete)
        s.record(clock, next, at, input.size(), output.size(), capacity);
    s.previous = clock;
    s.started = true;
    for (auto *p : output)
        std::fill(p + clock.duration, p + capacity, 0.f);
    next = s.finish(next);
    if (!active(next) && next != DuplexStatus::Complete)
        silence();
    if (!s.observations.tryPush({clock, report, next}))
        s.dropped.fetch_add(1, std::memory_order_relaxed);
    return next;
}
} // namespace soundcurrent::daw
