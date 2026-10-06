// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include "duplex_bridge.hpp"
#include "recording.hpp"

namespace soundcurrent::daw {
struct DuplexRecordingLane {
    RecordingSpec spec;
    std::vector<std::uint32_t> inputChannels;
    RecordingMonitor monitoring = RecordingMonitor::Off;
    RecordingOptions writer;
};
struct PreparedPunchLane {
    Id track;
    Frame inputLatencyFrames = 0;
    PunchRange capture;
};
struct MusicalPunchPlan {
    PunchRange timeline;
    Frame requiredPlaybackEnd = 0;
    std::vector<PreparedPunchLane> lanes;
};
// Control-only, no disk/jobs. Timeline locators are project sample frames (tempo
// conversion is upstream). Each raw range adds declared input latency; reject
// overflow, invalid rates/latencies/identities or more than 256 arms.
MusicalPunchPlan prepareMusicalPunch(PunchRange, std::uint32_t sampleRate,
                                     std::span<const DuplexRecordingLane>);
struct DuplexRecordingOptions {
    MixPlaybackConfig playback;
    std::uint32_t nativeInputs = 1;
    std::size_t memoryBudgetBytes = 256 * 1024 * 1024;
    CaptureBackend backend = CaptureBackend::Unknown;
    ReadAheadOptions reader;
    bool staggerCheckpoints = true;
    std::optional<PunchRange> punch;
    // Timeline-frame locators, mutually exclusive with the raw punch above.
    // Preparation sets capture starts and extends playback for required latency
    // postroll, bounded by the existing 60-second input-latency admission limit.
    std::optional<PunchRange> musicalPunch;
};
struct DuplexCaptureSnapshot {
    Frame captured = 0, written = 0;
    CaptureStatus status = CaptureStatus::Running;
    CaptureEndReason endReason = CaptureEndReason::Unknown;
    std::uint64_t rejected = 0, invalidSamples = 0;
    bool writerComplete = false;
    std::optional<CaptureTimingOrigin> origin;
};
// Framework-independent control owner. Preparation admits all capture pools
// before allocation and creates no jobs. startWriters() precedes callback
// activation. Stop/join the native callback owner BEFORE stop(), cancel() or
// destruction. These control methods may block; never invoke on GUI/audio.
// One audio producer and one control consumer; immutable lanes live until join.
class DuplexRecordingRun {
  public:
    DuplexRecordingRun(std::filesystem::path root, const Session &, MixPlan,
                       std::vector<DuplexRecordingLane>, DuplexRecordingOptions);
    ~DuplexRecordingRun();
    DuplexRecordingRun(const DuplexRecordingRun &) = delete;
    DuplexRecordingRun &operator=(const DuplexRecordingRun &) = delete;
    void startWriters(); // Once; retains and rethrows the original preparation error.
    void checkActivation() const;
    DuplexStatus process(const DeviceBlockClock &, std::span<const float *const>,
                         std::span<float *const>, std::uint32_t capacity) noexcept;
    void requestFault(DuplexStatus) noexcept;
    void requestStop() noexcept;
    void stop() noexcept;   // After native join: finish pipes, join every worker.
    void cancel() noexcept; // After native join: retain independently recoverable checkpoints.
    const RecordingResult &result(std::size_t) const; // After stop; per-lane disk exception.
    const RecordingSpec &spec(std::size_t) const;
    std::optional<PunchRange> captureRange(std::size_t) const;
    Frame playbackEnd() const noexcept;
    std::optional<std::filesystem::path> jobDirectory(std::size_t) const;
    DuplexCaptureSnapshot capture(std::size_t) const;
    std::size_t lanes() const noexcept;
    DuplexStatus status() const noexcept;
    Frame position() const noexcept;
    std::uint64_t missingTrackFrames() const noexcept;
    std::optional<CaptureTimingOrigin> timingOrigin() const noexcept;
    std::optional<DuplexCallbackFault> callbackFault() const noexcept;
    PreparedMixGraph &graph() noexcept;
    bool observation(DuplexObservation &) noexcept;
    std::uint64_t droppedObservations() const noexcept;
    void checkReader() const; // After stop; retained reader exception, independently of takes.

  private:
    struct State;
    std::unique_ptr<State> state_;
};
} // namespace soundcurrent::daw
