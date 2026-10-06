// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include "session.hpp"
#include "spsc_queue.hpp"
#include <array>
#include <atomic>
#include <span>
#include <vector>

namespace soundcurrent::daw {
inline constexpr std::uint32_t captureSlabs = 32;
inline constexpr std::uint32_t maximumCaptureSlabs = 256;
struct CaptureConfig {
    std::uint32_t sampleRate = 48000;
    ChannelLayout layout;
    std::uint32_t maximumCallbackFrames = 2048;
    // Zero chooses at least two seconds across the fixed 32 slabs.
    std::uint32_t slabFrames = 0;
    std::size_t memoryBudgetBytes = 128 * 1024 * 1024;
    Frame startFrame = 0;
    // Runtime pool admission. Playback retains its separate fixed 32-slab pool.
    std::uint32_t poolSlabs = captureSlabs;
    bool operator==(const CaptureConfig &) const = default;
};
CaptureConfig prepareCaptureConfig(CaptureConfig); // Validates/admission off RT.
// Minimum disk-stall reserve, 2..20 seconds. Preserves slab/callback sizes;
// refuses an impossible slot/memory request rather than silently reducing it.
CaptureConfig withCaptureReserve(CaptureConfig, std::uint32_t milliseconds);
enum class CaptureStatus : std::uint32_t {
    Running,
    Stopped,
    QueueFull,
    InvalidBuffer,
    TimingError,
    WriterFailed
};
enum class CaptureBackend : std::uint32_t { Unknown, Synthetic, PipeWire, Jack, Wasapi, Asio };
enum class CaptureEndReason : std::uint32_t {
    Unknown,
    UserStop,
    RangeComplete,
    DeviceLost,
    RateChanged,
    QuantumExceeded,
    ClockDiscontinuity,
    CaptureFailed,
    ProcessorFailed,
    WriterFailed,
    RecoveredCheckpoint
};
struct CaptureTimingOrigin {
    CaptureBackend backend = CaptureBackend::Unknown;
    std::uint64_t devicePosition = 0, monotonicNs = 0, generation = 0;
    std::uint32_t clockId = 0, cycle = 0, rateNumerator = 1, rateDenominator = 48000;
    std::int64_t driverDelay = 0; // Diagnostic, never substituted for input latency.
    bool operator==(const CaptureTimingOrigin &) const = default;
};
struct CaptureReport {
    CaptureStatus status = CaptureStatus::Running;
    std::uint32_t acceptedFrames = 0;
    std::uint32_t rejectedFrames = 0;
    std::uint64_t invalidInputSamples = 0;
};
struct CapturePacket {
    std::uint64_t sequence = 0;
    Frame firstFrame = 0;
    std::uint32_t frames = 0;
    std::uint32_t slab = 0;
    bool operator==(const CapturePacket &) const = default;
};
struct CapturedSlab {
    CapturePacket packet;
    std::span<const float> interleaved;
};
// Disk-owner snapshot. Ready slabs exclude the acquired slab and the producer's
// unfinished slab. Ready-frame count is an upper bound (a final slab may be short).
struct CaptureBacklog {
    std::uint32_t readySlabs = 0, acquiredFrames = 0;
    std::uint64_t queuedFrameUpperBound = 0, capacityFrames = 0;
};

// Prepared/control construction, one audio producer and one disk consumer.
// Stop and join both owners before destruction. Methods marked audio must not
// be called by the GUI. Consumer sample views expire on release().
class CapturePipe {
  public:
    explicit CapturePipe(CaptureConfig);
    CapturePipe(const CapturePipe &) = delete;
    CapturePipe &operator=(const CapturePipe &) = delete;
    const CaptureConfig &config() const noexcept {
        return config_;
    }
    // Audio owner; input backing capacity/independent channel pointers are the
    // caller's contract. No allocation, locks, logging or disk calls.
    CaptureReport push(std::span<const float *const> input, std::uint32_t frames,
                       Frame firstFrame) noexcept;
    // Audio owner, or control after callback shutdown transfers ownership.
    void finish(CaptureEndReason = CaptureEndReason::UserStop) noexcept;
    // Audio owner once, before its first push. Release-published immutable data.
    bool setTimingOrigin(const CaptureTimingOrigin &) noexcept;
    std::optional<CaptureTimingOrigin> timingOrigin() const noexcept;
    CaptureEndReason endReason() const noexcept;
    Frame nextFrame() const noexcept {
        return nextFrame_;
    } // Audio owner only.
    CaptureStatus status() const noexcept;
    std::uint64_t rejectedFrames() const noexcept;
    std::uint64_t invalidInputSamples() const noexcept;
    bool producerDone() const noexcept;
    // Disk owner. At most one acquired slab; release before acquiring another.
    bool acquire(CapturedSlab &) noexcept;
    bool release(const CapturedSlab &) noexcept;
    bool drained() const noexcept;
    void writerFailed() noexcept;
    CaptureBacklog consumerBacklog() const noexcept; // Disk owner only; no audio changes.

  private:
    CaptureConfig config_;
    std::vector<float> samples_;
    SpscQueue<std::uint32_t, 512> free_;
    SpscQueue<CapturePacket, 512> ready_;
    std::uint32_t current_ = maximumCaptureSlabs, used_ = 0;
    std::uint64_t sequence_ = 0;
    Frame nextFrame_ = 0, slabStart_ = 0;
    std::uint32_t acquired_ = maximumCaptureSlabs;
    CapturePacket acquiredPacket_;
    alignas(64) std::atomic<std::uint32_t> status_{0}, done_{0}, writerFailed_{0};
    std::atomic<std::uint64_t> rejected_{0}, invalid_{0};
    CaptureTimingOrigin timing_{};
    std::atomic<std::uint32_t> originReady_{0}, endReason_{0};
    void publishCurrent() noexcept;
};
} // namespace soundcurrent::daw
