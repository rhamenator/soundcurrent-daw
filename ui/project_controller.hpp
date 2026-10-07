// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include <soundcurrent/recording.hpp>
#include <soundcurrent/project_store.hpp>
#include <memory>
#include <optional>

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
    Structural
};
struct ProjectCommand {
    ProjectCommand(CommandKind type = CommandKind::Save) : kind(type) {}
    CommandKind kind = CommandKind::Save;
    std::filesystem::path path;
    std::string name;
    std::shared_ptr<const RecordingResult> recording; // Finalized/recovered owned take; immutable.
    std::shared_ptr<const std::vector<RecordingResult>>
        recordings; // Alternative atomic group, admitted against trusted receipt/state resources.
    std::vector<SessionEdit> edits; // One atomic group, at most 64 operations.
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
    bool final = true;
};
enum class Admission { Accepted, Full, Closing };
enum class IoOperation { None, Create, Open, Save, AttachRecording };
struct AttachmentCompletion {
    std::uint64_t request = 0;
    std::optional<ErrorCode> error;
    std::string diagnostic;
};
struct ControllerSnapshot {
    std::shared_ptr<const Session> session;
    std::filesystem::path root;
    std::uint64_t modelRevision = 0, savedRevision = 0, completedCommands = 0, errorSerial = 0;
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
    std::optional<ErrorCode> errorCode;
    std::string diagnostic;
    IoOperation io = IoOperation::None;
    bool dirty = false, closing = false, closed = false;
};
struct ControllerOptions {
    // I/O worker only. Used for cancellation/slow storage/failure qualification.
    std::function<void()> beforeIo;
    std::function<void()> beforeSavePublish;
    std::function<void()> beforeCommand; // Control-worker admission/failure fixture only.
    ProjectBudget admission{}; // Trusted application configuration, never project metadata.
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
    void requestShutdown() noexcept; // Priority flag, unaffected by a full command queue.
  private:
    struct State;
    std::unique_ptr<State> state_;
};
} // namespace soundcurrent::daw::ui
