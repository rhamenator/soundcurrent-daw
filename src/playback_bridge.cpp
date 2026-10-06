// SPDX-License-Identifier: GPL-3.0-only
#include <soundcurrent/playback_bridge.hpp>
#include <algorithm>
#include <limits>

namespace soundcurrent::daw {
namespace {
bool active(PlaybackBridgeStatus s) noexcept {
    return s == PlaybackBridgeStatus::Ready || s == PlaybackBridgeStatus::Running ||
           s == PlaybackBridgeStatus::Underflow;
}
PlaybackBridgeStatus converted(PlaybackStatus s) noexcept {
    switch (s) {
    case PlaybackStatus::Running:
        return PlaybackBridgeStatus::Running;
    case PlaybackStatus::Underflow:
        return PlaybackBridgeStatus::Underflow;
    case PlaybackStatus::Complete:
        return PlaybackBridgeStatus::Complete;
    case PlaybackStatus::Stopped:
        return PlaybackBridgeStatus::Stopped;
    case PlaybackStatus::ReaderFailed:
        return PlaybackBridgeStatus::ReaderFailed;
    case PlaybackStatus::InvalidBuffer:
        return PlaybackBridgeStatus::BufferUnavailable;
    case PlaybackStatus::TimingError:
        return PlaybackBridgeStatus::ClockDiscontinuity;
    case PlaybackStatus::ProcessorFailed:
        return PlaybackBridgeStatus::ProcessorFailed;
    }
    return PlaybackBridgeStatus::ProcessorFailed;
}
} // namespace
PlaybackBridge::PlaybackBridge(PlaybackRun &run, CaptureBackend backend)
    : run_(&run), config_(run.config()), backend_(backend) {}
PlaybackBridge::PlaybackBridge(MixPlaybackRun &run, CaptureBackend backend)
    : run_(&run), backend_(backend) {
    const auto &c = run.config();
    config_.sampleRate = run.sampleRate();
    config_.layout = run.graph().plan().output;
    config_.maximumCallbackFrames = c.graph.maximumFrames;
    config_.generation = c.graph.generation;
    config_.startFrame = c.graph.startFrame;
    config_.endFrame = c.endFrame;
}
void PlaybackBridge::stopRun() noexcept {
    std::visit([](auto *run) { run->requestStop(); }, run_);
}
void PlaybackBridge::cancelRun() noexcept {
    std::visit([](auto *run) { run->cancelReader(); }, run_);
}
PlaybackBridgeStatus PlaybackBridge::status() const noexcept {
    return static_cast<PlaybackBridgeStatus>(state_.load(std::memory_order_acquire));
}
void PlaybackBridge::requestFault(PlaybackBridgeStatus next) noexcept {
    if (active(next) || next == PlaybackBridgeStatus::Complete)
        return;
    auto prior = state_.load(std::memory_order_acquire);
    while (active(static_cast<PlaybackBridgeStatus>(prior))) {
        if (state_.compare_exchange_strong(prior, static_cast<std::uint32_t>(next),
                                           std::memory_order_acq_rel, std::memory_order_acquire)) {
            stopRun();
            return;
        }
    }
}
void PlaybackBridge::requestStop() noexcept {
    requestFault(PlaybackBridgeStatus::Stopped);
}
void PlaybackBridge::finish(PlaybackBridgeStatus next) noexcept {
    auto prior = state_.load(std::memory_order_acquire);
    // A concurrent control fault wins over audio publication, including Complete.
    // Conversely a completed range remains complete after late device removal.
    if (active(static_cast<PlaybackBridgeStatus>(prior)))
        state_.compare_exchange_strong(prior, static_cast<std::uint32_t>(next),
                                       std::memory_order_acq_rel, std::memory_order_acquire);
    if (!active(status())) {
        stopRun();
        cancelRun();
    }
}
void PlaybackBridge::finishQuiescent() noexcept {
    if (active(status()))
        requestStop();
    cancelRun();
}
bool PlaybackBridge::observation(PlaybackObservation &o) noexcept {
    return observations_.tryPop(o);
}
std::uint64_t PlaybackBridge::droppedObservations() const noexcept {
    return dropped_.load(std::memory_order_relaxed);
}
std::optional<CaptureTimingOrigin> PlaybackBridge::timingOrigin() const noexcept {
    if (!originReady_.load(std::memory_order_acquire))
        return {};
    return origin_;
}
PlaybackBridgeStatus PlaybackBridge::process(const DeviceBlockClock &clock,
                                             std::span<float *const> output,
                                             std::uint32_t capacity) noexcept {
    const auto silence = [&] {
        for (auto *p : output)
            if (p)
                std::fill_n(p, capacity, 0.f);
    };
    auto next = status();
    if (!active(next)) {
        silence();
        finish(next);
        return status();
    }
    const auto &c = config_;
    const auto channels = c.layout.channels;
    if (!clock.duration || clock.duration > c.maximumCallbackFrames || clock.duration > capacity)
        next = PlaybackBridgeStatus::QuantumExceeded;
    else if (clock.rateNumerator != 1 || clock.rateDenominator != c.sampleRate)
        next = PlaybackBridgeStatus::RateChanged;
    else if (output.size() != channels)
        next = PlaybackBridgeStatus::BufferUnavailable;
    else if (clock.xrun || clock.discontinuity ||
             clock.position > std::numeric_limits<std::uint64_t>::max() - clock.duration ||
             (started_ && (clock.id != previous_.id ||
                           clock.position != previous_.position + previous_.duration)))
        next = PlaybackBridgeStatus::ClockDiscontinuity;
    else if (std::any_of(output.begin(), output.end(), [](auto *p) { return !p; })) {
        // Unmapped inactive/link priming may precede the first valid callback.
        // Skip only an entirely unmapped, correctly sized startup block. A
        // partially mapped multichannel route is a fault, not silent truncation.
        if (!started_ && std::all_of(output.begin(), output.end(), [](auto *p) { return !p; }))
            return next;
        next = PlaybackBridgeStatus::BufferUnavailable;
    }
    if (!active(next)) {
        silence();
        finish(next);
        return status();
    }
    if (!started_) {
        origin_ = {backend_,    clock.position,      clock.monotonicNs,     c.generation, clock.id,
                   clock.cycle, clock.rateNumerator, clock.rateDenominator, clock.delay};
        originReady_.store(1, std::memory_order_release);
    }
    previous_ = clock;
    started_ = true;
    PlaybackReport report;
    std::optional<MixPlaybackReport> mixed;
    std::visit(
        [&](auto *run) {
            if constexpr (std::is_same_v<std::remove_pointer_t<decltype(run)>, PlaybackRun>)
                report = run->process(output, static_cast<std::uint32_t>(clock.duration));
            else {
                mixed = run->process(output, static_cast<std::uint32_t>(clock.duration));
                report = {
                    mixed->status,           mixed->startFrame,
                    mixed->timelineFrames,   static_cast<std::uint32_t>(mixed->missingTrackFrames),
                    mixed->staleTrackFrames, mixed->mix.peak};
            }
        },
        run_);
    // Even if native capacity exceeds the current quantum, certify silent slack.
    for (auto *p : output)
        std::fill(p + clock.duration, p + capacity, 0.f);
    finish(converted(report.status));
    next = status();
    if (next != PlaybackBridgeStatus::Complete && !active(next))
        silence();
    if (!observations_.tryPush({clock, report, next, mixed}))
        dropped_.fetch_add(1, std::memory_order_relaxed);
    return next;
}
} // namespace soundcurrent::daw
