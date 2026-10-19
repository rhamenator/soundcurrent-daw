// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include <soundcurrent/recording.hpp>
#include <soundcurrent/manual_recording.hpp>
#include <soundcurrent/loop_recording.hpp>
#include <soundcurrent/project_store.hpp>
#include <soundcurrent/resource_ledger.hpp>
#include <memory>
#include <optional>

namespace soundcurrent::daw {class VerifiedClipStretch;class VerifiedNativeTake;class VerifiedCompAssemblyAdoption;class PreparedCompTransition;}
namespace soundcurrent::daw::ui {
enum class CommandKind {
    Create,
    Open,
    Save,
    Parameter,
    CancelGesture,
    Undo,
    Redo,
    Barrier,
    AttachRecording,
    Routing,
    Monitoring,
    Structural,
    HistoryLimits,
    SnapshotLimits,
    MemoryLimits,
    Autosave,
    AcquireAutosaveInhibition,
    ReleaseAutosaveInhibition
};
struct StructuralGuard {
    std::uint64_t projectEpoch;
    Id project;
    std::filesystem::path root;
};
struct LoopRecordedTake {
    LoopRecordingContext context;
    std::vector<LoopRecordedSource> sources;
    std::uint64_t projectEpoch = 0;
};
struct LoopTakeRetention {
    std::shared_ptr<const LoopRecordedTake> take;
    std::uint64_t first = 0, count = 0;
    bool allowPartial = false;
};
struct ProjectCommand {
    ProjectCommand(CommandKind type = CommandKind::Save) : kind(type) {}
    CommandKind kind = CommandKind::Save;
    std::filesystem::path path;
    std::string name;
    std::uint32_t sampleRate = 48000; // Create only; existing projects retain their rate.
    std::shared_ptr<const RecordingResult> recording; // Finalized/recovered owned take; immutable.
    std::shared_ptr<const std::vector<RecordingResult>>
        recordings; // Alternative atomic group, admitted against trusted receipt/state resources.
    // Alternative complete group: retained inactive until an explicit comp edit.
    std::shared_ptr<const ManualRecordedGroup> retainedTake;
    std::shared_ptr<const LoopTakeRetention> retainedLoop;
    std::vector<SessionEdit> edits; // One atomic group, at most 64 operations.
    std::shared_ptr<const VerifiedNativeTake> nativeTake;
    std::shared_ptr<const VerifiedClipStretch> stretchResult; // Retains admitted result credit through queue/adoption.
    std::shared_ptr<const VerifiedCompAssemblyAdoption> compAssemblyResult;
    std::shared_ptr<const PreparedCompTransition> compPreparation;
    std::optional<StructuralGuard> structuralGuard;
    std::uint64_t structuralRequest=0;
    std::optional<RouteAddress> routeAddress;
    RouteIntent route;
    std::optional<Id> monitoringTrack;
    RecordingMonitor monitoring = RecordingMonitor::Off;
    std::optional<RouteChannelPatch>
        routePatch; // Merge with latest accepted route, not a stale GUI copy.
    std::optional<ParameterAddress> address;
    double value = 0;
    std::uint64_t gesture = 0;
    std::uint64_t barrier = 0;
    std::uint64_t attachmentRequest = 0; // Optional caller-owned correlation ID.
    std::optional<HistoryBudget> historyBudget;
    std::uint64_t historyRequest = 0;
    std::optional<std::size_t> snapshotBytes;
    std::uint64_t snapshotRequest = 0;
    std::optional<std::size_t> memoryBytes;
    std::uint64_t memoryRequest = 0;
    // Caller tokens must increase across acquisitions, including project epochs.
    std::uint64_t autosaveInhibitionToken = 0, autosaveInhibitionEpoch = 0;
    bool final = true;
};
enum class Admission { Accepted, Full, Closing };
enum class IoOperation { None, Create, Open, Save, AttachRecording, Autosave };
struct AttachmentCompletion {
    std::uint64_t request = 0;
    std::optional<ErrorCode> error;
    std::string diagnostic;
};
struct HistoryPolicyCompletion {
    std::uint64_t request = 0;
    std::optional<ErrorCode> error;
    std::string diagnostic;
};
struct AutosaveInhibitionCompletion {
    std::uint64_t token = 0, projectEpoch = 0;
    bool acquisition = false;
    std::optional<ErrorCode> error;
};
struct ControllerSnapshot {
    std::shared_ptr<const Session> session;
    std::filesystem::path root;
    std::uint64_t modelRevision = 0, savedRevision = 0, completedCommands = 0, errorSerial = 0;
    // Nonzero acknowledges exclusive autosave inhibition; acquire refuses active I/O.
    std::uint64_t autosaveInhibitionToken = 0;
    // Separate success/refusal receipts survive unrelated errors and stale requests.
    AutosaveInhibitionCompletion autosaveInhibitionCompleted, autosaveInhibitionRejected;
    std::uint64_t autosavedRevision = 0; // Successful autosave only; never marks clean.
    std::uint64_t projectEpoch = 0; // Each successful Create/Open, even the same project ID.
    std::uint64_t lastBarrier = 0, attachedRecordings = 0;
    // One retained immutable receipt at the exact accepted-command prefix.
    std::shared_ptr<const Session> barrierSession;
    std::filesystem::path barrierRoot;
    std::uint64_t barrierRevision = 0;
    std::optional<Id> lastAttachedAsset;
    std::vector<Id> lastAttachedAssets; // One successful verified group, stable canonical IDs.
    // Single serialized desktop attachment owner. Unrelated errors cannot
    // replace these separately retained IO and command-rejection receipts.
    AttachmentCompletion attachmentCompleted, attachmentRejected;
    AttachmentCompletion structuralCompleted,structuralRejected;
    std::optional<ErrorCode> errorCode;
    std::string diagnostic;
    HistoryBudget historyBudget{};
    HistoryResources historyResources{};
    HistoryPolicyCompletion historyCompleted;
    ResourceUsage snapshotResources{};
    HistoryPolicyCompletion snapshotCompleted;
    ResourceUsage memoryResources{};
    std::size_t canonicalBytes = 0, historyBytes = 0;
    HistoryPolicyCompletion memoryCompleted;
    IoOperation io = IoOperation::None;
    bool dirty = false, closing = false, closed = false;
};
struct ControllerOptions {
    // I/O worker only. Used for cancellation/slow storage/failure qualification.
    std::function<void()> beforeIo;
    std::function<void()> beforeSavePublish;
    std::function<void()> beforeCommand;        // Control-worker admission/failure fixture only.
    std::function<void()> beforeInitialPublish; // Control-worker startup qualification only.
    HistoryBudget historyBudget{};
    std::size_t snapshotBytes = 256 * 1024 * 1024;
    ProjectBudget admission{}; // Trusted application configuration, never project metadata.
    std::size_t memoryBytes = 1024 * 1024 * 1024;
    // Trusted opt-in until recording policy/recovery UI is wired. Zero disables.
    std::uint64_t autosaveIntervalMilliseconds = 0;
    // Control-worker clock seam; production uses steady_clock. Never audio.
    std::function<std::uint64_t()> autosaveNowMilliseconds;
};
// Qt is confined to this desktop adapter. Canonical Session/EditHistory belong
// to the control worker; blocking filesystem operations to a separate I/O worker.
// GUI submits a bounded command queue and polls one immutable latest snapshot.
class ProjectController {
  public:
    explicit ProjectController(ControllerOptions = {});
    ~ProjectController(); // Fallback joins off audio; normal GUI waits asynchronously for closed.
    ProjectController(const ProjectController &) = delete;
    ProjectController &operator=(const ProjectController &) = delete;
    Admission submit(ProjectCommand);
    std::shared_ptr<const ControllerSnapshot> snapshot() const;
    // Live ownership accounting, including externally retained older snapshots.
    ResourceUsage snapshotResources() const;
    ResourceUsage memoryResources() const;
    ResourceLedger resourceLedger() const; // Off-audio owners join the same parent budget.
    void requestShutdown() noexcept;       // Priority flag, unaffected by a full command queue.
  private:
    struct State;
    std::unique_ptr<State> state_;
};
} // namespace soundcurrent::daw::ui
