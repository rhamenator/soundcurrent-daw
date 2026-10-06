// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include "capture.hpp"
#include "eq.hpp"
#include <atomic>

namespace soundcurrent::daw {
struct DeviceBlockClock {
    std::uint64_t position = 0, duration = 0, monotonicNs = 0;
    // PipeWire cycle advances each graph cycle; it is not a device generation.
    std::uint32_t id = 0, cycle = 0, rateNumerator = 1, rateDenominator = 48000;
    std::int64_t delay = 0;
    bool xrun = false, discontinuity = false;
};
enum class AudioBridgeStatus : std::uint32_t {
    Ready,
    Running,
    Complete,
    Stopped,
    RateChanged,
    QuantumExceeded,
    ClockDiscontinuity,
    BufferUnavailable,
    DeviceLost,
    CaptureFailed,
    ProcessorFailed
};
struct BackendObservation {
    DeviceBlockClock device;
    Frame engineFrame = 0;
    std::uint32_t frames = 0;
    AudioBridgeStatus status = AudioBridgeStatus::Ready;
    float inputPeak = 0, outputPeak = 0;
    std::uint64_t invalidSamples = 0, numericFaultSamples = 0;
};
struct AudioBridgeOptions {
    std::uint32_t maximumFrames = 2048;
    std::uint64_t generation = 1;
    Frame stopAfterFrames = 0; // Zero: run until explicit stop/fault.
    CaptureBackend backend = CaptureBackend::Synthetic;
};
// Qt/backend-free callback owner; prepare before activation, one control owner
// for parameter events and one audio owner for process. Pipes outlive callbacks.
class AudioBridge {
  public:
    AudioBridge(const Session &, const Id &track, CapturePipe &, AudioBridgeOptions = {},
                CapturePipe *processedTestTap = nullptr);
    PreparedEq &prepared() noexcept {
        return eq_;
    } // Control: immutable event preparation only.
    SubmitStatus submit(const EqEvent &event) noexcept {
        return driver_.submit(event);
    }
    SubmitStatus submitImmediate(const EqEvent &event, std::uint64_t revision) noexcept {
        return driver_.submitImmediate(event, revision);
    }
    bool acknowledgement(ImmediateAcknowledgement &result) noexcept {
        return driver_.acknowledgement(result);
    }
    std::uint64_t droppedAcknowledgements() const noexcept {
        return driver_.droppedAcknowledgements();
    }
    AudioBridgeStatus process(const DeviceBlockClock &, std::span<const float *const>,
                              std::span<float *const>, std::uint32_t bufferFrames) noexcept;
    void requestStop() noexcept;
    void requestFault(AudioBridgeStatus) noexcept; // Control-side device notifications.
    // After native callbacks have stopped/joined; transfers audio ownership.
    void finishQuiescent() noexcept;
    AudioBridgeStatus status() const noexcept;
    Frame capturedFrames() const noexcept;
    bool observation(BackendObservation &) noexcept; // One control consumer; lossy queue.
    std::uint64_t droppedObservations() const noexcept;

  private:
    PreparedEq eq_;
    EqLiveDriver driver_;
    CapturePipe &capture_;
    CapturePipe *tap_;
    AudioBridgeOptions options_;
    DeviceBlockClock previous_{};
    bool clockStarted_ = false;
    Frame captured_ = 0;
    SpscQueue<BackendObservation, 64> observations_;
    std::atomic<std::uint32_t> state_{0};
    std::atomic<Frame> publishedFrames_{0};
    std::atomic<std::uint64_t> dropped_{0};
    std::array<const float *, 256> tapPointers_{};
    AudioBridgeStatus publish(AudioBridgeStatus) noexcept;
    AudioBridgeStatus finish(AudioBridgeStatus) noexcept;
};
} // namespace soundcurrent::daw
