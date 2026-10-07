// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include "manual_punch.hpp"
#include "recording.hpp"
#include <exception>

namespace soundcurrent::daw {
// One generation, monotonic priority signal. Any thread may request; no reset or
// endpoint pointer escapes. Retain shared ownership until native/control joins.
// The callback only observes stop; the serialized owner performs cancel/drain.
class ManualRecordingInterrupt {
    std::atomic<std::uint32_t> requested_{0};

  public:
    void requestStop() noexcept {
        requested_.fetch_or(1, std::memory_order_release);
    }
    void requestCancel() noexcept {
        requested_.fetch_or(3, std::memory_order_release);
    }
    bool stopRequested() const noexcept {
        return requested_.load(std::memory_order_acquire) != 0;
    }
    bool cancelRequested() const noexcept {
        return (requested_.load(std::memory_order_acquire) & 2) != 0;
    }
};
static_assert(std::atomic<std::uint32_t>::is_always_lock_free);
struct ManualRecordingArm {
    ManualPunchArm binding;
    RecordingOptions writer;
};
struct ManualRecordingOptions {
    MixPlaybackConfig playback;
    CaptureConfig capture; // Zero-start template; per-arm layouts derived before allocation.
    ReadAheadOptions reader;
    std::uint32_t nativeInputs = 1;
    CaptureBackend backend = CaptureBackend::Unknown;
    std::size_t memoryBudgetBytes = 256 * 1024 * 1024;
    bool staggerCheckpoints = true;
    std::shared_ptr<ManualRecordingInterrupt> interrupt; // Optional, prepared off RT; never reset.
};
enum class ManualLaneOutcome { Complete, Empty, Failed, Canceled };
struct ManualRecordedLane {
    RecordingSpec spec;
    Frame capturedFrames = 0, writtenFrames = 0;
    CaptureStatus captureStatus = CaptureStatus::Running;
    CaptureEndReason endReason = CaptureEndReason::Unknown;
    std::optional<CaptureTimingOrigin> origin;
    ManualLaneOutcome outcome = ManualLaneOutcome::Empty;
    std::optional<RecordingResult> result;
    std::optional<std::filesystem::path> job;
    std::optional<RecordingRecovery> checkpoint; // Verified after lease/worker release.
    std::exception_ptr error, verificationError; // Original failure preserved independently.
};
struct ManualRecordedGroup {
    std::uint64_t take = 0;
    Frame beginFrame = 0, endFrame = 0;
    std::vector<ManualRecordedLane> lanes;
    bool canceled = false;
    // Only complete, full-length, range-complete lanes count as a complete group.
    bool complete() const noexcept;
};
// Transactional model copy, no saving/reset of the running immutable graph.
// Refuses canceled/incomplete groups unless explicit partial adoption is chosen;
// empty/failed lanes remain visible in the receipt and are never fabricated.
Session withManualRecording(const Session &, const ManualRecordedGroup &,
                            bool allowPartial = false);

// One serialized disk/control owner plus one audio callback owner. Prepare and
// service may perform filesystem work, allocation and worker joins: never call
// them from the GUI/audio threads. Native callbacks must be joined BEFORE stop,
// cancel or destruction. The prepared session/graph remains immutable across takes.
class ManualRecordingRun {
  public:
    ManualRecordingRun(std::filesystem::path, const Session &, MixPlan,
                       std::vector<ManualRecordingArm>, ManualRecordingOptions);
    ~ManualRecordingRun();
    ManualRecordingRun(const ManualRecordingRun &) = delete;
    ManualRecordingRun &operator=(const ManualRecordingRun &) = delete;
    std::uint64_t prepareTake(); // At most eight pending/unconsumed groups, bounded backpressure.
    void abandonTake(std::uint64_t); // Only unused Prepared slots with no pending commands.
    ManualPunchSubmit submit(ManualPunchCommand) noexcept;
    bool acknowledgement(ManualPunchReceipt &) noexcept; // Reliable until explicitly consumed.
    void service(); // Start late workers, collect completed workers/groups and retain errors.
    bool takeGroup(ManualRecordedGroup &); // Control; frees an already joined/reclaimed slot.
    DuplexStatus process(const DeviceBlockClock &, std::span<const float *const>,
                         std::span<float *const>, std::uint32_t capacity) noexcept;
    void requestFault(DuplexStatus) noexcept;
    void requestStop() noexcept;
    void stop();              // After callback join. Drains replies, finishes/joins every consumer.
    void cancel();            // After callback join. Keeps each independently durable checkpoint.
    void checkError() const;  // Retained first control/disk error; cleanup cannot overwrite it.
    void checkReader() const; // Reader failure independent of recording failures, after stop.
    DuplexStatus status() const noexcept;
    std::optional<DuplexCallbackFault> callbackFault() const noexcept;
    PreparedMixGraph &graph() noexcept; // Existing bounded parameter queues; no graph reset.
    Frame position() const noexcept;
    std::uint64_t missingTrackFrames() const noexcept;
    std::size_t occupiedSlots() const noexcept; // Serialized control owner only.

  private:
    struct State;
    std::unique_ptr<State> state_;
};
} // namespace soundcurrent::daw
