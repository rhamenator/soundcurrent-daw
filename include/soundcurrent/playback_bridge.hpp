// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include "audio_bridge.hpp"
#include "playback_reader.hpp"

namespace soundcurrent::daw {
enum class PlaybackBridgeStatus : std::uint32_t {
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
    ProcessorFailed
};
struct PlaybackObservation {
    DeviceBlockClock device;
    PlaybackReport playback;
    PlaybackBridgeStatus status = PlaybackBridgeStatus::Ready;
};
// Backend-free playback clock gate. One audio owner calls process; one control
// consumer drains diagnostics. Run outlives bridge and all native callbacks.
class PlaybackBridge {
  public:
    explicit PlaybackBridge(PlaybackRun &, CaptureBackend = CaptureBackend::Unknown);
    PlaybackBridgeStatus process(const DeviceBlockClock &, std::span<float *const>,
                                 std::uint32_t capacity) noexcept;
    void requestStop() noexcept;
    void requestFault(PlaybackBridgeStatus) noexcept;
    // Only after native callbacks are joined; transfers callback ownership.
    void finishQuiescent() noexcept;
    PlaybackBridgeStatus status() const noexcept;
    bool observation(PlaybackObservation &) noexcept;
    std::uint64_t droppedObservations() const noexcept;
    std::optional<CaptureTimingOrigin> timingOrigin() const noexcept;

  private:
    PlaybackRun &run_;
    CaptureBackend backend_;
    DeviceBlockClock previous_{};
    CaptureTimingOrigin origin_{};
    bool started_ = false;
    std::atomic<std::uint32_t> originReady_{0}, state_{0};
    SpscQueue<PlaybackObservation, 64> observations_;
    std::atomic<std::uint64_t> dropped_{0};
    void finish(PlaybackBridgeStatus) noexcept;
};
} // namespace soundcurrent::daw
