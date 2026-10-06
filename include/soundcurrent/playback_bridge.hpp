// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include "audio_bridge.hpp"
#include "playback_reader.hpp"
#include "mix_reader.hpp"
#include <variant>

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
    std::optional<MixPlaybackReport>
        mix; // Present for a shared-clock mix; counts are track-frames.
};
struct PlaybackCallbackFault {
    DeviceBlockClock received, previous;
    PlaybackBridgeStatus detected = PlaybackBridgeStatus::Ready;
    std::uint64_t generation = 0;
    Frame enginePosition = 0;
    std::uint32_t expectedRate = 0, maximumFrames = 0, capacity = 0, expectedChannels = 0;
    std::size_t outputChannels = 0;
    bool hadPrevious = false;
};
// Backend-free playback clock gate. One audio owner calls process; one control
// consumer drains diagnostics. Run outlives bridge and all native callbacks.
class PlaybackBridge {
  public:
    explicit PlaybackBridge(PlaybackRun &, CaptureBackend = CaptureBackend::Unknown);
    explicit PlaybackBridge(MixPlaybackRun &, CaptureBackend = CaptureBackend::Unknown);
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
    // Immutable first callback validation fault, independent of the lossy meter queue.
    // Control faults need not have an associated callback clock. Diagnostic facts
    // do not override the first winning terminal status.
    std::optional<PlaybackCallbackFault> callbackFault() const noexcept;

  private:
    std::variant<PlaybackRun *, MixPlaybackRun *> run_;
    PlaybackConfig config_;
    void stopRun() noexcept;
    void cancelRun() noexcept;
    CaptureBackend backend_;
    DeviceBlockClock previous_{};
    CaptureTimingOrigin origin_{};
    PlaybackCallbackFault fault_{};
    bool started_ = false;
    std::atomic<std::uint32_t> originReady_{0}, state_{0};
    std::atomic<std::uint32_t> faultReady_{0};
    SpscQueue<PlaybackObservation, 64> observations_;
    std::atomic<std::uint64_t> dropped_{0};
    void finish(PlaybackBridgeStatus) noexcept;
};
} // namespace soundcurrent::daw
