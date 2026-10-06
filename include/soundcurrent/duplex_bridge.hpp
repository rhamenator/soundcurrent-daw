// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include "audio_bridge.hpp"
#include "mix_reader.hpp"

namespace soundcurrent::daw {
// Control-side capture-pool and binding reservation; normalizes/validates config.
std::size_t armedCapturePayloadBytes(CaptureConfig, std::size_t inputChannels);
// Immutable half-open engine-frame range within the prepared playback range.
// Capture pipes start at begin; playback and explicit monitoring keep running.
struct PunchRange {
    Frame begin = 0, end = 0;
    bool operator==(const PunchRange &) const = default;
};
struct ArmedCapture {
    Id track;
    CapturePipe *pipe = nullptr; // Must outlive the bridge and callback owner.
    std::vector<std::uint32_t> inputChannels;
    RecordingMonitor monitoring = RecordingMonitor::Off;
    // Optional per-lane raw window. Mutually exclusive with a shared bridge punch.
    std::optional<PunchRange> captureRange = {};
    // AutoRecording follows desired timeline coordinates, independently of the
    // delayed raw window. Omitted: shared punch or this lane's capture range.
    std::optional<PunchRange> monitorRange = {};
};
enum class DuplexStatus : std::uint32_t {
    Ready,
    Running,
    Underflow,
    Complete,
    Stopped,
    RateChanged,
    QuantumExceeded,
    ClockDiscontinuity,
    BufferUnavailable,
    DeviceLost,
    ReaderFailed,
    ProcessorFailed,
    CaptureFailed
};
struct DuplexCallbackFault {
    DeviceBlockClock received, previous;
    DuplexStatus detected = DuplexStatus::Ready;
    Frame enginePosition = 0;
    std::uint64_t generation = 0;
    std::uint32_t expectedRate = 0, maximumFrames = 0, capacity = 0;
    std::size_t expectedInputs = 0, expectedOutputs = 0, inputs = 0, outputs = 0;
    std::size_t failedCapture = SIZE_MAX;
    bool hadPrevious = false;
};
struct DuplexObservation {
    DeviceBlockClock device;
    MixPlaybackReport playback;
    DuplexStatus status = DuplexStatus::Ready;
};
// Single audio owner; one control consumer/parameter producer. Preparation,
// memory admission and destruction are off RT. Raw pipes and run outlive this
// bridge. One native clock gates both playback and all armed raw captures.
class DuplexBridge {
  public:
    DuplexBridge(MixPlaybackRun &, const Session &, std::vector<ArmedCapture>,
                 std::uint32_t nativeInputs, CaptureBackend = CaptureBackend::Unknown,
                 std::size_t memoryBudgetBytes = 256 * 1024 * 1024, std::optional<PunchRange> = {});
    ~DuplexBridge();
    DuplexBridge(const DuplexBridge &) = delete;
    DuplexBridge &operator=(const DuplexBridge &) = delete;
    DuplexStatus process(const DeviceBlockClock &, std::span<const float *const>,
                         std::span<float *const>, std::uint32_t capacity) noexcept;
    void requestFault(DuplexStatus) noexcept;
    void requestStop() noexcept;
    void finishQuiescent() noexcept; // Only after native stop/join.
    DuplexStatus status() const noexcept;
    Frame capturedFrames(std::size_t) const; // Control-side atomic read.
    std::optional<DuplexCallbackFault> callbackFault() const noexcept;
    // Earliest captured sample across lanes, independent of binding order.
    // Per-lane origins live on their pipes and can differ. Unknown until capture
    // starts; cycle/delay identify the containing native callback.
    std::optional<CaptureTimingOrigin> timingOrigin() const noexcept;
    bool observation(DuplexObservation &) noexcept;
    std::uint64_t droppedObservations() const noexcept;

  private:
    struct State;
    std::unique_ptr<State> state_;
};
} // namespace soundcurrent::daw
