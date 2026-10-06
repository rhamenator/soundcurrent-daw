// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include "capture.hpp"
#include "project_store.hpp"
#include <functional>
#include <memory>

namespace soundcurrent::daw {
struct RecordingSpec {
    Id projectId = Id::generate();
    Id trackId = Id::generate();
    Id assetId = Id::generate();
    CaptureConfig capture;
    Frame inputLatencyFrames = 0; // Engine-frame alignment, supplied by backend.
    std::optional<Id> recoveredFrom;
    bool operator==(const RecordingSpec &) const = default;
};
enum class RecordingBoundary {
    BeforeAudioWrite,
    AfterAudioFlush,
    BeforeJournalPublish,
    BeforeMediaPublish,
    AfterMediaPublish,
    BeforeAssetHashRead
};
enum class RecordingWriterPhase {
    WriteHashBegin,
    WriteHashEnd,
    AudioFlushBegin,
    AudioFlushEnd,
    JournalPublishBegin,
    JournalPublishEnd,
    IdleBegin,
    IdleEnd
};
struct RecordingWriterObservation {
    RecordingWriterPhase phase = RecordingWriterPhase::WriteHashBegin;
    Frame writtenFrames = 0, committedFrames = 0;
    CaptureBacklog backlog{};
    bool hasBacklog = false; // Construction checkpoints have no producer/pipe.
};
struct RecordingWriterInstrumentation {
    void *context = nullptr;
    // Disk/construction owners only; bounded/noexcept. Never called by audio.
    void (*observe)(void *, const RecordingWriterObservation &) noexcept = nullptr;
};
struct RecordingOptions {
    // Zero defaults to one second; checkpoints occur after completed slabs.
    Frame checkpointFrames = 0;
    // Worker-only progress/cancellation/fault boundary; never called by audio.
    std::function<void(RecordingBoundary, Frame)> boundary;
    RecordingWriterInstrumentation instrumentation{};
};
struct RecordingResult {
    RecordingSpec spec;
    Asset asset;
    CaptureStatus captureStatus;
    Durability durability;
};
// No internal thread/device ownership. A disk worker owns drain/finalize;
// construction can be on control and ownership transferred before worker start.
// Join the worker before destruction. Failed/incomplete files remain for recovery.
class CaptureWriter {
  public:
    CaptureWriter(std::filesystem::path projectRoot, RecordingSpec, RecordingOptions = {});
    ~CaptureWriter();
    CaptureWriter(const CaptureWriter &) = delete;
    CaptureWriter &operator=(const CaptureWriter &) = delete;
    bool drainOne(CapturePipe &);
    RecordingResult finalize(CapturePipe &); // Producer stopped, all slabs drained.
    const std::filesystem::path &jobDirectory() const noexcept;
    Frame writtenFrames() const noexcept; // Disk owner or after join.
    Frame checkpointFrames() const noexcept;

  private:
    friend class RecordingWorker;
    void observeWait(CapturePipe &, bool beginning) noexcept;
    struct State;
    std::unique_ptr<State> state_;
};
// Production disk supervisor. Sleep/polling and all file/hash/journal work run
// on its worker, never on the audio owner. Stop callbacks, pipe.finish(), then
// wait() off RT for a normal finalized take. cancel() preserves a checkpoint.
// Destruction cancels/joins its disk thread; the caller still owns the pipe and
// must stop the audio producer before destroying that separate object.
class RecordingWorker {
  public:
    RecordingWorker(CapturePipe &, std::filesystem::path projectRoot, RecordingSpec,
                    RecordingOptions = {});
    ~RecordingWorker();
    RecordingWorker(const RecordingWorker &) = delete;
    RecordingWorker &operator=(const RecordingWorker &) = delete;
    void cancel() noexcept;
    bool complete() const noexcept;
    Frame writtenFrames() const noexcept;
    const std::filesystem::path &jobDirectory() const noexcept;
    RecordingResult wait(); // Control owner; joins and surfaces worker failure.
  private:
    struct State;
    std::unique_ptr<State> state_;
};
struct RecordingRecovery {
    RecordingSpec spec;
    Frame committedFrames = 0;
    Frame observedFrames = 0;
    std::uint64_t nextSequence = 0;
    std::string sampleSha256;
    std::filesystem::path source;
    bool finalized = false;
    bool writerActivityConfirmed = false; // Transient inspection evidence, never serialized.
    CaptureStatus captureStatus = CaptureStatus::Running;
    std::uint64_t rejectedFrames = 0;
    std::uint64_t observedInvalidInputSamples = 0;
    std::optional<CaptureTimingOrigin> timingOrigin;
    CaptureEndReason endReason = CaptureEndReason::Unknown;
    bool operator==(const RecordingRecovery &) const = default;
};
// Worker/control-only. Bounded parse and streaming prefix verification.
RecordingRecovery inspectRecording(const std::filesystem::path &jobDirectory,
                                   const std::function<void()> &boundary = {},
                                   bool requireInactive = false);
// Metadata is NOT audio verification. Streaming verification remains an explicit step.
enum class RecordingJobStatus {
    NeedsVerification,
    LegacyNeedsVerification,
    Active,
    Empty,
    Foreign,
    Invalid,
    RecoveredSource
};
struct RecordingJobEntry {
    std::filesystem::path job;
    RecordingJobStatus status = RecordingJobStatus::Invalid;
    std::optional<RecordingRecovery> checkpoint;
    std::string diagnostic;
};
struct RecordingDiscovery {
    std::vector<RecordingJobEntry> entries;
    std::size_t directoryEntries = 0, attached = 0;
    bool truncated = false;
    std::vector<std::string> warnings;
};
struct RecordingDiscoveryOptions {
    std::size_t maximumDirectoryEntries = 8192, maximumJobs = 512;
    std::function<void()> boundary; // Worker-only cancellation/fault boundary.
};
RecordingDiscovery discoverRecordings(const std::filesystem::path &root, const Session &,
                                      const RecordingDiscoveryOptions & = {});
// Copies only the verified checkpoint into a new asset/job. Original unchanged.
RecordingResult recoverRecording(const std::filesystem::path &projectRoot,
                                 const std::filesystem::path &jobDirectory,
                                 const std::function<void()> &boundary = {});
// Transactional model edit; caller saves with ProjectStore afterward. No disk I/O.
// Raw asset is intact; input latency shifts/initially trims the non-destructive clip.
void attachRecording(Session &, const RecordingResult &);
} // namespace soundcurrent::daw
