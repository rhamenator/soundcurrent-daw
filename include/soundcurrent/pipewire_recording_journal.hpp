// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include "original_recording.hpp"
#include "pipewire_receipt_codec.hpp"
namespace soundcurrent::daw {
struct PipeWireRecordingSpec {
    Id run = Id::generate();
    OriginalRecordingSpec raw;
    std::uint64_t generation = 1;
    ClockStreamIdentity renderIdentity{2,1,1};
    std::uint32_t renderRate = 48000,renderChannels = 2,maximumRenderFrames = 2048;
    bool operator==(const PipeWireRecordingSpec &) const = default;
};
enum class PipeWireJournalBoundary { PendingPublished, RawAppended, ReceiptWritten, RawCheckpoint,
    ReceiptsFlushed, BeforeCheckpoint, CheckpointPublished, RawFinalized, FinalPublished };
struct PipeWireJournalOptions {
    OriginalRecordingOptions raw;
    std::uint64_t checkpointRecords = 64;
    std::function<void(PipeWireJournalBoundary)> boundary; // Disk/control only.
};
struct PipeWireRecordingPrefix {
    PipeWireRecordingSpec declared;
    OriginalRecordingPrefix raw;
    std::uint64_t captureRecords = 0,renderRecords = 0,captureBytes = 0,renderBytes = 0,
                  rawReceiptFrames = 0,monitorQueuedFrames = 0,nativeQueuedFrames = 0;
    std::string captureSha256,renderSha256;
    bool finalized = false; // Stored paired-prefix declaration, not audible/native lifecycle completion.
    std::optional<PipeWireReceiptRecord<PipeWireRenderCycle>> completion; // Verified original first valid final return.
};
// ONE serialized disk owner. Holds raw and independent native-sidecar leases.
// Paired raw/capture receipt writes, bounded records and durable prefix manifests.
// Never called from native callbacks or GUI. Caller retains raw view until append
// returns, and joins all producers/consumers before finalization/retirement.
class PipeWireRecordingJournal {
  public:
    PipeWireRecordingJournal(std::filesystem::path projectRoot,PipeWireRecordingSpec,
                             PipeWireJournalOptions = {});
    ~PipeWireRecordingJournal();
    void appendCapture(const PipeWireReceiptRecord<PipeWireCaptureCycle> &,
                       const std::optional<CaptureBankView> &,bool deferCheckpoint = false);
    // Disk owner: call after releasing the copied raw bank view. No durability
    // policy changes; the same due paired prefix is flushed/published here.
    void checkpointIfDue();
    void appendRender(const PipeWireReceiptRecord<PipeWireRenderCycle> &);
    void checkpoint();
    void finalize(const PreparedCaptureFanout &,const PreparedPipeWireReceiptQueues &);
    const std::filesystem::path &jobDirectory() const noexcept;
  private:
    struct State;
    std::unique_ptr<State> state_;
};
// Inactive-only streaming validation of native receipts and exact bound raw PCM
// prefix. Extra uncommitted tails remain untouched. No SDK/driver activation,
// timestamp fitting, destructive truncation or inferred timeline placement.
PipeWireRecordingPrefix inspectPipeWireRecording(const std::filesystem::path &job,
    const std::optional<PipeWireRecordingSpec> &expected = {},
    std::optional<ResourceLedger> resources = {},const std::function<void()> &beforeRead = {});
} // namespace soundcurrent::daw
