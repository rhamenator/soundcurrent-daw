// SPDX-License-Identifier: GPL-3.0-only
#include <soundcurrent/audio_bridge.hpp>
#include <algorithm>
#include <cmath>
#include <limits>

namespace soundcurrent::daw {
AudioBridge::AudioBridge(const Session &s, const Id &track, CapturePipe &capture,
                         AudioBridgeOptions options, CapturePipe *tap)
    : eq_(s, track, options.maximumFrames, options.generation),
      driver_(eq_, capture.config().startFrame), capture_(capture), tap_(tap), options_(options) {
    const auto t = std::find_if(s.tracks.begin(), s.tracks.end(),
                                [&](const auto &item) { return item.id == track; });
    if (capture.config().deferredStart || capture.config().sampleRate != eq_.sampleRate() ||
        t == s.tracks.end() || capture.config().layout != t->layout ||
        capture.config().maximumCallbackFrames < options.maximumFrames ||
        options.stopAfterFrames < 0 ||
        options.stopAfterFrames > std::numeric_limits<Frame>::max() - capture.config().startFrame ||
        (tap && tap->config() != capture.config()) || capture.producerDone())
        throw ProjectError(ErrorCode::InvalidState, "Audio bridge/capture configuration mismatch");
}
AudioBridgeStatus AudioBridge::status() const noexcept {
    return static_cast<AudioBridgeStatus>(state_.load(std::memory_order_acquire));
}
AudioBridgeStatus AudioBridge::publish(AudioBridgeStatus desired) noexcept {
    auto current = state_.load(std::memory_order_acquire);
    if (current == static_cast<std::uint32_t>(AudioBridgeStatus::Ready) ||
        current == static_cast<std::uint32_t>(AudioBridgeStatus::Running)) {
        if (state_.compare_exchange_strong(current, static_cast<std::uint32_t>(desired),
                                           std::memory_order_acq_rel, std::memory_order_acquire))
            return desired;
    }
    return static_cast<AudioBridgeStatus>(current);
}
Frame AudioBridge::capturedFrames() const noexcept {
    return publishedFrames_.load(std::memory_order_acquire);
}
std::uint64_t AudioBridge::droppedObservations() const noexcept {
    return dropped_.load(std::memory_order_relaxed);
}
bool AudioBridge::observation(BackendObservation &o) noexcept {
    return observations_.tryPop(o);
}
void AudioBridge::requestStop() noexcept {
    requestFault(AudioBridgeStatus::Stopped);
}
void AudioBridge::requestFault(AudioBridgeStatus desired) noexcept {
    if (desired == AudioBridgeStatus::Ready || desired == AudioBridgeStatus::Running ||
        desired == AudioBridgeStatus::Complete || desired > AudioBridgeStatus::ProcessorFailed)
        return;
    auto current = state_.load(std::memory_order_acquire);
    // Only Ready -> Running can race an active-state comparison. Two strong
    // attempts suffice; there is no unbounded retry in the callback path.
    for (int attempt = 0; attempt < 2; ++attempt) {
        if (current != static_cast<std::uint32_t>(AudioBridgeStatus::Ready) &&
            current != static_cast<std::uint32_t>(AudioBridgeStatus::Running))
            return;
        if (state_.compare_exchange_strong(current, static_cast<std::uint32_t>(desired),
                                           std::memory_order_acq_rel, std::memory_order_acquire))
            return;
    }
}
AudioBridgeStatus AudioBridge::finish(AudioBridgeStatus state) noexcept {
    state = publish(state);
    auto reason = CaptureEndReason::UserStop;
    switch (state) {
    case AudioBridgeStatus::Complete:
        reason = CaptureEndReason::RangeComplete;
        break;
    case AudioBridgeStatus::RateChanged:
        reason = CaptureEndReason::RateChanged;
        break;
    case AudioBridgeStatus::QuantumExceeded:
        reason = CaptureEndReason::QuantumExceeded;
        break;
    case AudioBridgeStatus::ClockDiscontinuity:
        reason = CaptureEndReason::ClockDiscontinuity;
        break;
    case AudioBridgeStatus::BufferUnavailable:
    case AudioBridgeStatus::DeviceLost:
        reason = CaptureEndReason::DeviceLost;
        break;
    case AudioBridgeStatus::CaptureFailed:
        reason = capture_.status() == CaptureStatus::WriterFailed ? CaptureEndReason::WriterFailed
                                                                  : CaptureEndReason::CaptureFailed;
        break;
    case AudioBridgeStatus::ProcessorFailed:
        reason = CaptureEndReason::ProcessorFailed;
        break;
    default:
        break;
    }
    capture_.finish(reason);
    if (tap_)
        tap_->finish(reason);
    return state;
}
void AudioBridge::finishQuiescent() noexcept {
    const auto state = status();
    finish(state == AudioBridgeStatus::Ready || state == AudioBridgeStatus::Running
               ? AudioBridgeStatus::Stopped
               : state);
}
AudioBridgeStatus AudioBridge::process(const DeviceBlockClock &clock,
                                       std::span<const float *const> input,
                                       std::span<float *const> output,
                                       std::uint32_t bufferFrames) noexcept {
    auto state = status();
    // Native adapter supplies backing capacity for every nonnull output view.
    // Even contract failures silence only those proven-capacity views.
    const auto silence = [&] {
        for (auto *p : output)
            if (p)
                std::fill_n(p, bufferFrames, 0.f);
    };
    if (state != AudioBridgeStatus::Ready && state != AudioBridgeStatus::Running) {
        silence();
        return finish(state);
    }
    if (clock.duration == 0 || clock.duration > options_.maximumFrames ||
        clock.duration > bufferFrames)
        state = AudioBridgeStatus::QuantumExceeded;
    else if (clock.rateNumerator != 1 || clock.rateDenominator != eq_.sampleRate())
        state = AudioBridgeStatus::RateChanged;
    else if (input.size() != eq_.channels() || output.size() != eq_.channels() ||
             std::any_of(input.begin(), input.end(), [](auto *p) { return !p; }) ||
             std::any_of(output.begin(), output.end(), [](auto *p) { return !p; }))
        state = AudioBridgeStatus::BufferUnavailable;
    else if (clock.xrun || clock.discontinuity ||
             clock.position > std::numeric_limits<std::uint64_t>::max() - clock.duration ||
             (clockStarted_ && (clock.id != previous_.id ||
                                clock.position != previous_.position + previous_.duration)))
        state = AudioBridgeStatus::ClockDiscontinuity;
    if (state != AudioBridgeStatus::Ready && state != AudioBridgeStatus::Running) {
        silence();
        return finish(state);
    }
    if (!clockStarted_) {
        const CaptureTimingOrigin origin{
            options_.backend, clock.position, clock.monotonicNs,   options_.generation,
            clock.id,         clock.cycle,    clock.rateNumerator, clock.rateDenominator,
            clock.delay};
        if (!capture_.setTimingOrigin(origin) || (tap_ && !tap_->setTimingOrigin(origin))) {
            silence();
            return finish(AudioBridgeStatus::CaptureFailed);
        }
    }
    previous_ = clock;
    clockStarted_ = true;
    const auto n = static_cast<std::uint32_t>(clock.duration);
    const auto engineStart = driver_.frame();
    // Raw copy and input metering must precede DSP, including in-place output.
    float inputPeak = 0;
    for (auto *channel : input)
        for (std::uint32_t frame = 0; frame < n; ++frame)
            if (std::isfinite(channel[frame]))
                inputPeak = std::max(inputPeak, std::abs(channel[frame]));
    const auto captureFrames = static_cast<std::uint32_t>(
        options_.stopAfterFrames ? std::min<Frame>(n, options_.stopAfterFrames - captured_) : n);
    const auto captureReport = capture_.push(input, captureFrames, engineStart);
    captured_ += captureReport.acceptedFrames;
    publishedFrames_.store(captured_, std::memory_order_release);
    const auto report = driver_.process(input, output, n);
    if (report.status != ProcessStatus::Ok) {
        silence();
        return finish(AudioBridgeStatus::ProcessorFailed);
    }
    if (tap_) {
        for (std::uint32_t c = 0; c < eq_.channels(); ++c)
            tapPointers_[c] = output[c];
        const auto tapReport = tap_->push({tapPointers_.data(), eq_.channels()},
                                          captureReport.acceptedFrames, engineStart);
        if (tapReport.status != CaptureStatus::Running)
            state = AudioBridgeStatus::CaptureFailed;
    }
    if (captureReport.status != CaptureStatus::Running)
        state = AudioBridgeStatus::CaptureFailed;
    else if (state != AudioBridgeStatus::CaptureFailed)
        state = options_.stopAfterFrames && captured_ == options_.stopAfterFrames
                    ? AudioBridgeStatus::Complete
                    : AudioBridgeStatus::Running;
    BackendObservation o{clock, engineStart, n, state};
    o.inputPeak = inputPeak;
    o.invalidSamples = report.invalidInputSamples;
    o.numericFaultSamples = report.numericFaultSamples;
    for (std::uint32_t c = 0; c < eq_.channels(); ++c)
        for (std::uint32_t f = 0; f < n; ++f) {
            if (std::isfinite(output[c][f]))
                o.outputPeak = std::max(o.outputPeak, std::abs(output[c][f]));
        }
    state = publish(state);
    o.status = state;
    if (!observations_.tryPush(o))
        dropped_.fetch_add(1, std::memory_order_relaxed);
    if (state != AudioBridgeStatus::Running)
        return finish(state);
    return state;
}
} // namespace soundcurrent::daw
