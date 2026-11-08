// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include <soundcurrent/recording.hpp>
#include <soundcurrent/manual_recording.hpp>
#include <soundcurrent/loop_recording.hpp>
#include <soundcurrent/project_store.hpp>
#include <soundcurrent/resource_ledger.hpp>
#include <soundcurrent/restoration_audit.hpp>
#include <memory>
#include <optional>

namespace soundcurrent::daw {class VerifiedClipStretch;class VerifiedNativeTake;class VerifiedCompAssemblyAdoption;class PreparedCompTransition;}
namespace soundcurrent::daw::ui {
struct AssetRestorationInput;
struct SavedManifestSourceRequest {
    Id project,asset;
    std::uint64_t projectEpoch=0,modelRevision=0,request=0;
};
struct RestorationInspectionRequest {
    Id project,asset,operation;
    std::uint64_t projectEpoch=0,modelRevision=0,request=0;
};
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
    ReleaseAutosaveInhibition,
    AutosavePolicy,
    InspectAutosave,
    RestoreAutosave,
    RestoreAsset,
    CancelAssetRestoration,
    InspectAssetRestoration,
    InspectSavedAssetRestoration,
    PrepareSavedRestorationSource,
    StageSavedRestoration,
    CancelSavedRestorationStaging,
    ReconcileAssetRestoration,
    CancelAssetRestorationInspection,
    PublishSavedRestoration,
    CancelSavedPublication,
    ReconcileSavedPublication,
    PrepareSavedManifestSource,
    CancelSavedManifestSource,
    StageSavedManifest,
    CancelSavedManifestStaging
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
    // RestoreAsset atomically acquires this SAME caller namespace and epoch.
    std::uint64_t autosaveInhibitionToken = 0, autosaveInhibitionEpoch = 0;
    // Recording preparation may close its current edit gesture atomically with inhibition.
    bool commitGestureForAutosaveInhibition = false;
    // Application policy only, never loaded from untrusted project metadata.
    std::optional<std::uint64_t> autosaveIntervalMilliseconds;
    std::uint64_t autosavePolicyRequest = 0;
    std::uint64_t autosaveReviewRequest = 0, autosaveRestoreRequest = 0;
    std::shared_ptr<const AssetRestorationInput> restoration;
    std::uint64_t restorationJob=0; // Cancel only; exact current dispatch identity.
    std::optional<SavedManifestSourceRequest> savedManifestSource;
    std::uint64_t savedManifestSourceJob=0; // Cancel only; exact active manifest-only dispatch.
    std::uint64_t savedManifestPreparationJob=0,savedManifestStagingRequest=0,savedManifestStagingJob=0; // Distinct private manifest owner namespace.
    std::optional<RestorationInspectionRequest> restorationInspection;
    std::uint64_t restorationInspectionJob=0; // Cancel only, exact active dispatch.
    std::filesystem::path restorationSource; // Explicit selected file for saved-source preparation only.
    std::uint64_t savedStagingRequest=0,savedPreparationJob=0,savedStagingJob=0; // Opaque current private owner; no paths or capability DTO.
    std::uint64_t savedPublicationRequest=0,savedPublicationJob=0;
    bool final = true;
};
enum class Admission { Accepted, Full, Closing };
enum class IoOperation { None, Create, Open, Save, AttachRecording, Autosave, InspectAutosave, RestoreAutosave, RestoreAsset, InspectAssetRestoration, StageSavedRestoration, PublishSavedRestoration, PrepareSavedManifestSource, StageSavedManifest };
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
struct AutosaveReview {
    std::shared_ptr<const Session> session;
    std::uint64_t request = 0, projectEpoch = 0, modelRevision = 0;
};
struct AssetRestorationCompletion {
    std::uint64_t request=0,job=0;
    std::optional<Id> operation;
    std::size_t childPid=0;
    std::optional<int> childExit;
    bool permitSent=false,canonicalVerified=false,needsReconciliation=false;
    std::optional<bool> published; // Unknown after ambiguous permission/termination.
    bool receiptPublished=false,postCommitFlushFailed=false;
    unsigned durability=0;
    std::optional<ErrorCode> error;
};
// Plain observations only: no native handles, passive Session or resume authority.
struct RestorationInspectionCompletion {
    std::uint64_t request=0,job=0,projectEpoch=0,modelRevision=0,sourceBytes=0;
    std::array<char,36> project{},asset{},operation{};
    std::array<char,64> manifestSha256{};
    std::uint64_t checkerJob=0;
    std::size_t checkerPid=0;
    std::optional<int> checkerExit;
    bool sourcePrepared=false; // Historical completion; live private readiness is separate.
    std::array<char,64> checkedSourceSha256{};
    bool savedManifest=false; // Passive pre-open selection, never an accepted model.
    bool ambiguityReleased=false; // Fresh live reconciliation only; never historical publication attribution.
    std::optional<RestorationMediaObservation> canonical,staged;
    std::optional<RestorationReceiptObservation> receipt;
    std::optional<ErrorCode> error;
};
// No operation/journal identity: passive manifest-only source observations.
struct SavedManifestSourceCompletion {
    std::uint64_t request=0,job=0,projectEpoch=0,modelRevision=0,checkerJob=0,sourceBytes=0;
    std::size_t checkerPid=0;
    std::optional<int> checkerExit;
    std::array<char,36> project{},asset{};
    std::array<char,64> manifestSha256{},checkedSourceSha256{};
    bool sourcePrepared=false;
    std::optional<RestorationMediaObservation> canonical;
    std::optional<ErrorCode> error;
};
// Historical stage-only observations, never publication/current ownership.
struct SavedStagingCompletion {
    std::uint64_t request=0,job=0,preparationJob=0;
    std::optional<Id> operation;
    std::size_t childPid=0;
    std::optional<int> childExit;
    bool reaped=false,requestSent=false,stagingMayExist=false,recordVerified=false,stagedBytesVerified=false;
    // requestSent conservatively means a write was attempted/queued, not child consumption.
    std::optional<bool> normalExit; // Present only after an actually reaped child; exitCode alone is not normal-exit proof.
    bool killRequested=false; // An issued request, not attribution if exit raced it.
    bool launchAttempted=false,launchRetired=false,qualificationProbeFailed=false,cancellationObserved=false;
    std::optional<ErrorCode> error;
};
// Observed facts only, never a source proof, native stage or adoption capability.
struct SavedPublicationCompletion {
    SavedStagingCompletion staging;
    bool permitSent=false,terminalVerified=false,canonicalVerified=false,receiptVerified=false,needsReconciliation=false;
    std::optional<bool> published;
    bool receiptPublished=false,postCommitFlushFailed=false;
    unsigned durability=0;
};
struct ControllerSnapshot {
    std::shared_ptr<const Session> session;
    std::filesystem::path root;
    std::uint64_t modelRevision = 0, savedRevision = 0, completedCommands = 0, errorSerial = 0;
    // Nonzero acknowledges exclusive autosave inhibition; acquire refuses active I/O.
    std::uint64_t autosaveInhibitionToken = 0;
    // Separate success/refusal receipts survive unrelated errors and stale requests.
    AutosaveInhibitionCompletion autosaveInhibitionCompleted, autosaveInhibitionRejected;
    std::uint64_t autosaveIntervalMilliseconds = 0;
    HistoryPolicyCompletion autosavePolicyCompleted;
    HistoryPolicyCompletion autosaveReviewCompleted, autosaveRestoreCompleted;
    AutosaveReview autosaveReview;
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
    AssetRestorationCompletion assetRestorationCompleted;
    AttachmentCompletion assetRestorationRejected;
    std::uint64_t assetRestorationJob=0;
    bool assetRestorationNeedsReconciliation=false;
    RestorationInspectionCompletion restorationInspectionCompleted;
    AttachmentCompletion restorationInspectionRejected;
    std::uint64_t restorationInspectionJob=0;
    std::uint64_t restorationPreparedSource=0; // Live opaque ID; never publication permission.
    SavedManifestSourceCompletion savedManifestSourceCompleted;
    AttachmentCompletion savedManifestSourceRejected;
    std::uint64_t savedManifestSourceJob=0,savedManifestPreparedSource=0;
    SavedStagingCompletion savedManifestStagingCompleted;
    AttachmentCompletion savedManifestStagingRejected;
    std::uint64_t savedManifestStagingJob=0;
    SavedStagingCompletion savedStagingCompleted;
    AttachmentCompletion savedStagingRejected;
    std::uint64_t savedStagingJob=0;
    SavedPublicationCompletion savedPublicationCompleted;
    AttachmentCompletion savedPublicationRejected;
    std::uint64_t savedPublicationJob=0;
    bool savedPublicationNeedsReconciliation=false;
    IoOperation io = IoOperation::None;
    bool dirty = false, closing = false, closed = false;
};
struct ControllerOptions {
    // I/O worker only. Used for cancellation/slow storage/failure qualification.
    std::function<void()> beforeIo;
    std::function<void()> beforeSavePublish;
    std::function<void()> beforeCommand;        // Control-worker admission/failure fixture only.
    std::function<void()> beforeInitialPublish; // Control-worker startup qualification only.
    // Trusted application policy, never selected by project/worker metadata.
    std::filesystem::path restorationWorkerProgram = {};
    unsigned restorationDeadlineMilliseconds=20000;
    std::filesystem::path restorationSourceCheckerProgram = {};
    unsigned restorationSourceDeadlineMilliseconds=20000;
    std::function<void()> restorationSourceBeforeSpawn = {},restorationSourceBeforeTerminal = {};
    std::function<void(std::string &,std::size_t)> restorationSourceReportHook = {};
    std::function<void()> restorationInspectionBeforeVerify = {}; // Trusted I/O qualification seam.
    std::uint64_t restorationInspectionMaximumBytes=1024ULL*1024*1024;
    std::function<void()> restorationBeforePermit = {},restorationAfterPermit = {};
    std::function<void(std::string &,std::size_t)> restorationReadyHook = {}; // Qualification only.
    std::function<void(std::string &,std::size_t)> restorationTerminalHook = {}; // Qualification only.
    std::filesystem::path savedStagingWorkerProgram = {};
    std::filesystem::path savedManifestStagingWorkerProgram = {}; // Trusted parent configuration, not project data.
    std::filesystem::path savedPublicationWorkerProgram = {};
    std::function<void()> savedPublicationBeforePermit = {},savedPublicationAfterPermit = {};
    std::function<void(std::string &,std::size_t)> savedPublicationReadyHook = {}; // Qualification only.
    std::function<void()> savedPublicationBeforeReturn = {}; // I/O qualification: deliberately escapes after retirement; caller-owned progress must survive.
    std::function<void()> savedPublicationBeforeFinish = {}; // Control-thread qualification, before final owner/cancel checks; no permission mutex held.
    unsigned savedStagingDeadlineMilliseconds=20000; // Base, unchanged for small selections when size allowance is disabled.
    unsigned savedStagingMillisecondsPerMiB=1000; // Additional trusted I/O allowance from checked source extent.
    std::uint64_t savedStagingMaximumDeadlineMilliseconds=30*60*1000;
    std::function<std::uint64_t()> savedStagingNowMilliseconds = {}; // Thread-safe qualification clock; called on I/O and watchdog, never untrusted metadata.
    std::function<void()> savedStagingBeforeSourceChunk = {},savedStagingBeforeDestination = {};
    std::function<void(std::string_view)> savedStagingDiagnosticHook = {}; // Qualification only.
    std::function<void(bool launchRetired)> savedStagingRetirementProbe = {}; // Native pins held before kill and after QProcess retirement; confirmed PID/reap stays separate.
    std::function<void()> savedStagingAfterNativeRetirement = {},savedStagingAfterVerification = {},savedStagingAfterLaunch = {};
    std::string savedStagingFixtureMode = {}; // Qualification only; production worker rejects this argument.
    std::function<void()> savedStagingBeforeSpawn = {},savedStagingAfterSpawn = {},savedStagingBeforeTerminal = {};
    std::function<void(std::string &,std::size_t)> savedStagingTerminalHook = {};
    std::function<void()> savedManifestStagingBeforeInput = {},savedManifestStagingBeforeEof = {}; // Trusted qualification only.
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
