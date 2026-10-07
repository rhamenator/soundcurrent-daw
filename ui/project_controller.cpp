// SPDX-License-Identifier: GPL-3.0-only
#include "project_controller.hpp"
#include <QMutex>
#include <QThread>
#include <QWaitCondition>
#include <atomic>
#include <deque>
#include <limits>

namespace soundcurrent::daw::ui {
namespace {
constexpr std::size_t commandCapacity = 64;
void admitReceipts(const std::shared_ptr<const std::vector<RecordingResult>> &recordings,
                   StateBudget budget) {
    if (!recordings || recordings->empty())
        throw ProjectError(ErrorCode::InvalidState, "A take group needs recording receipts");
    PayloadCharge charge("Take receipt group", budget.memoryBudgetBytes);
    charge.add(recordings->size(), sizeof(RecordingResult) + 8192);
}

struct IoJob {
    IoOperation operation = IoOperation::None;
    std::filesystem::path root;
    std::shared_ptr<const Session> session;
    std::uint64_t revision = 0;
    std::shared_ptr<const std::vector<RecordingResult>> recordings = nullptr;
    std::uint64_t attachmentRequest = 0;
};
struct IoResult {
    IoJob job;
    std::optional<Session> loaded;
    std::optional<ErrorCode> error;
    std::string diagnostic;
};
class IoWorker : public QThread {
  public:
    ControllerOptions options;
    std::atomic<bool> &closing;
    QMutex mutex;
    QWaitCondition wake;
    std::optional<IoJob> job;
    std::optional<IoResult> result;
    bool busy = false, quit = false;
    IoWorker(ControllerOptions o, std::atomic<bool> &c) : options(std::move(o)), closing(c) {}
    void startJob(IoJob next) {
        QMutexLocker lock(&mutex);
        if (busy)
            throw ProjectError(ErrorCode::InvalidState, "A project operation is already active");
        job = std::move(next);
        busy = true;
        wake.wakeOne();
    }
    std::optional<IoResult> takeResult() {
        QMutexLocker lock(&mutex);
        auto finished = std::move(result);
        result.reset();
        if (finished)
            busy = false;
        return finished;
    }
    void stopWorker() {
        {
            QMutexLocker lock(&mutex);
            quit = true;
            wake.wakeOne();
        }
        wait(); // Control worker, never GUI/audio.
    }

  private:
    void run() override {
        for (;;) {
            IoJob next;
            {
                QMutexLocker lock(&mutex);
                while (!job && !quit)
                    wake.wait(&mutex);
                if (!job)
                    return;
                next = std::move(*job);
                job.reset();
            }
            IoResult finished;
            finished.job = next;
            try {
                if (options.beforeIo)
                    options.beforeIo();
                if (closing.load(std::memory_order_acquire))
                    throw ProjectError(ErrorCode::Canceled, "Project operation canceled");
                ProjectStore store(next.root, options.admission);
                if (next.operation == IoOperation::Open)
                    finished.loaded = store.load();
                else if (next.operation == IoOperation::AttachRecording) {
                    admitReceipts(next.recordings, options.admission.state);
                    const auto canceled = [&] {
                        if (closing.load(std::memory_order_acquire))
                            throw ProjectError(ErrorCode::Canceled,
                                               "Take verification canceled; media retained");
                    };
                    for (const auto &r : *next.recordings) {
                        const auto job = next.root / "media" / ("capture-" + r.spec.assetId.str());
                        const auto verified = inspectRecording(job, canceled, true);
                        const auto &a = verified.spec, &b = r.spec;
                        // Journals persist recording identity/timing, not the run's pool
                        // sizing, memory budget or callback bound. Recovery uses its own pool.
                        const bool sameIdentity = a.projectId == b.projectId &&
                                                  a.trackId == b.trackId &&
                                                  a.assetId == b.assetId &&
                                                  a.capture.sampleRate == b.capture.sampleRate &&
                                                  a.capture.layout == b.capture.layout &&
                                                  a.capture.startFrame == b.capture.startFrame &&
                                                  a.inputLatencyFrames == b.inputLatencyFrames &&
                                                  a.recoveredFrom == b.recoveredFrom;
                        if (!verified.finalized || !sameIdentity ||
                            verified.committedFrames != r.asset.frames ||
                            verified.captureStatus != r.captureStatus ||
                            r.asset.relativePath !=
                                "media/capture-" + r.spec.assetId.str() + "/take.wav")
                            throw ProjectError(
                                ErrorCode::MediaMismatch,
                                "Recorded take does not match its finalized journal");
                    }
                    store.verifyMedia(*next.session, canceled);
                    if (closing.load(std::memory_order_acquire))
                        throw ProjectError(
                            ErrorCode::Canceled,
                            "Take attachment canceled; recording remains recoverable");
                } else {
                    if (next.operation == IoOperation::Create &&
                        !std::filesystem::create_directory(next.root))
                        throw ProjectError(ErrorCode::Io, "New project folder already exists");
                    store.save(*next.session, {[&] {
                        if (options.beforeSavePublish)
                            options.beforeSavePublish();
                        if (closing.load(std::memory_order_acquire))
                            throw ProjectError(ErrorCode::Canceled,
                                               "Project save canceled before publication");
                    }});
                }
            } catch (const ProjectError &e) {
                finished.error = e.code();
                finished.diagnostic = e.what();
            } catch (const std::exception &e) {
                finished.error = ErrorCode::Io;
                finished.diagnostic = e.what();
            }
            {
                QMutexLocker lock(&mutex);
                result = std::move(finished);
                if (quit)
                    return;
            }
        }
    }
};
} // namespace
struct ProjectController::State : QThread {
    mutable QMutex mutex;
    QWaitCondition wake;
    std::deque<ProjectCommand> commands;
    std::shared_ptr<const ControllerSnapshot> latest = std::make_shared<ControllerSnapshot>();
    std::atomic<bool> closing{false};
    ProjectBudget admission;
    IoWorker io;
    std::optional<Session> model;
    std::unique_ptr<EditHistory> history;
    std::shared_ptr<const Session> savedModel;
    std::uint64_t activeGesture = 0;
    std::optional<ParameterAddress> activeAddress;
    ControllerSnapshot view;
    bool changed = true;
    explicit State(ControllerOptions options)
        : admission(options.admission), io(std::move(options), closing) {}
    ~State() override = default;
    void publish() {
        if (changed && model)
            view.session = std::make_shared<const Session>(*model);
        changed = false;
        view.dirty = model && (!savedModel || *model != *savedModel);
        auto next = std::make_shared<const ControllerSnapshot>(view);
        QMutexLocker lock(&mutex);
        latest = std::move(next);
    }
    void error(ErrorCode code, std::string detail) {
        view.errorCode = code;
        view.diagnostic = std::move(detail);
        ++view.errorSerial;
    }
    void requireModel() const {
        if (!model)
            throw ProjectError(ErrorCode::InvalidState, "Create or open a project first");
    }
    void commitGesture() {
        if (activeGesture) {
            history->commit();
            activeGesture = 0;
            activeAddress.reset();
        }
    }
    void revised() {
        if (view.modelRevision == std::numeric_limits<std::uint64_t>::max())
            throw ProjectError(ErrorCode::InvalidState, "Project revision exhausted");
        ++view.modelRevision;
        changed = true;
    }
    void execute(ProjectCommand &command) {
        switch (command.kind) {
        case CommandKind::Create:
        case CommandKind::Open: {
            if (view.io != IoOperation::None)
                throw ProjectError(ErrorCode::InvalidState,
                                   "A project operation is already active");
            requireCleanReplacement();
            commitGesture();
            IoJob job;
            job.operation =
                command.kind == CommandKind::Create ? IoOperation::Create : IoOperation::Open;
            job.root = command.path;
            if (job.operation == IoOperation::Create)
                job.session =
                    std::make_shared<const Session>(makeOneTrackSession(command.name, "Audio 1"));
            io.startJob(std::move(job));
            view.io = command.kind == CommandKind::Create ? IoOperation::Create : IoOperation::Open;
            break;
        }
        case CommandKind::Save:
            requireModel();
            commitGesture();
            if (view.io != IoOperation::None)
                throw ProjectError(ErrorCode::InvalidState,
                                   "A project operation is already active");
            io.startJob({IoOperation::Save, view.root, std::make_shared<const Session>(*model),
                         view.modelRevision});
            view.io = IoOperation::Save;
            break;
        case CommandKind::Parameter: {
            requireModel();
            if (view.io == IoOperation::Create || view.io == IoOperation::Open)
                throw ProjectError(ErrorCode::InvalidState, "Project replacement is in progress");
            if (!command.address || !command.gesture)
                throw ProjectError(ErrorCode::InvalidParameter, "Parameter gesture is missing");
            // Validate the proposed value before altering the current gesture/history.
            auto proposed = *model;
            setParameterValue(proposed, *command.address, command.value);
            if (activeGesture != command.gesture || activeAddress != command.address) {
                commitGesture();
                history->begin(*command.address);
                activeGesture = command.gesture;
                activeAddress = command.address;
            }
            const auto previous = parameterValue(*model, *command.address);
            history->update(command.value);
            if (previous != command.value)
                revised();
            if (command.final)
                commitGesture();
            break;
        }
        case CommandKind::Routing: {
            requireModel();
            if (view.io == IoOperation::Create || view.io == IoOperation::Open ||
                !command.routeAddress)
                throw ProjectError(ErrorCode::InvalidState,
                                   "Route change needs the current project");
            const auto value = command.routePatch ? patchedRouteValue(*model, *command.routeAddress,
                                                                      *command.routePatch)
                                                  : command.route;
            auto proposed = *model;
            setRouteValue(proposed, *command.routeAddress, value,
                          admission.state); // Reject before committing a gesture.
            commitGesture();
            if (history->route(*command.routeAddress, value))
                revised();
            break;
        }
        case CommandKind::Monitoring: {
            requireModel();
            if (!command.monitoringTrack || view.io == IoOperation::Create ||
                view.io == IoOperation::Open)
                throw ProjectError(ErrorCode::InvalidState,
                                   "Monitoring change needs the current project");
            auto proposed = *model;
            setMonitoringValue(proposed, *command.monitoringTrack, command.monitoring);
            commitGesture();
            if (history->monitoring(*command.monitoringTrack, command.monitoring))
                revised();
            break;
        }
        case CommandKind::Structural: {
            requireModel();
            if (view.io == IoOperation::Create || view.io == IoOperation::Open)
                throw ProjectError(ErrorCode::InvalidState, "Project replacement is in progress");
            auto proposed = *model;
            applySessionEdits(proposed, command.edits,
                              admission.state); // Reject before committing a gesture.
            if (proposed != *model &&
                view.modelRevision == std::numeric_limits<std::uint64_t>::max())
                throw ProjectError(ErrorCode::InvalidState, "Project revision exhausted");
            commitGesture();
            if (history->adopt(proposed))
                revised();
            break;
        }
        case CommandKind::CancelGesture:
            requireModel();
            if (activeGesture == command.gesture && activeGesture) {
                history->cancel();
                activeGesture = 0;
                activeAddress.reset();
                revised();
            }
            break;
        case CommandKind::Undo:
        case CommandKind::Redo:
            requireModel();
            if (view.io == IoOperation::Create || view.io == IoOperation::Open)
                throw ProjectError(ErrorCode::InvalidState, "Project replacement is in progress");
            commitGesture();
            if (command.kind == CommandKind::Undo ? history->undo() : history->redo())
                revised();
            break;
        case CommandKind::AttachRecording: {
            requireModel();
            if (view.io != IoOperation::None ||
                bool(command.recording) == bool(command.recordings) || command.path != view.root)
                throw ProjectError(ErrorCode::InvalidState,
                                   "Take attachment needs the current project and idle I/O owner");
            // Shape/identity validation is transactional; disk/journal verification belongs
            // to the I/O worker. No speculative asset is published to the canonical model.
            auto proposed = std::make_shared<Session>(*model);
            const auto recordings = command.recordings
                                        ? command.recordings
                                        : std::make_shared<const std::vector<RecordingResult>>(
                                              std::vector<RecordingResult>{*command.recording});
            admitReceipts(recordings, admission.state);
            for (const auto &r : *recordings)
                attachRecording(*proposed, r);
            commitGesture();
            IoJob job{IoOperation::AttachRecording, view.root, std::move(proposed),
                      view.modelRevision, recordings};
            job.attachmentRequest = command.attachmentRequest;
            io.startJob(std::move(job));
            view.io = IoOperation::AttachRecording;
            break;
        }
        case CommandKind::Barrier:
            if (!command.barrier)
                throw ProjectError(ErrorCode::InvalidState, "Barrier token is missing");
            commitGesture();
            view.lastBarrier = command.barrier;
            view.barrierSession = model ? std::make_shared<const Session>(*model) : nullptr;
            view.barrierRoot = view.root;
            view.barrierRevision = view.modelRevision;
            break;
        }
        ++view.completedCommands;
    }
    void requireCleanReplacement() const {
        if (model && (!savedModel || *model != *savedModel))
            throw ProjectError(ErrorCode::InvalidState,
                               "Save the current project before replacing it");
    }
    void finishIo(IoResult result) {
        view.io = IoOperation::None;
        const bool correlated =
            result.job.operation == IoOperation::AttachRecording && result.job.attachmentRequest;
        if (correlated)
            view.attachmentCompleted = {result.job.attachmentRequest, result.error,
                                        result.diagnostic};
        if (result.error) {
            error(*result.error, std::move(result.diagnostic));
            return;
        }
        if (result.job.operation == IoOperation::AttachRecording) {
            try {
                if (!model || view.root != result.job.root || !result.job.recordings ||
                    result.job.recordings->empty() ||
                    model->id != result.job.recordings->front().spec.projectId)
                    throw ProjectError(ErrorCode::InvalidState,
                                       "Recording project changed before attachment");
                if (view.modelRevision == std::numeric_limits<std::uint64_t>::max())
                    throw ProjectError(ErrorCode::InvalidState, "Project revision exhausted");
                // Preserve scalar edits accepted while files were verified.
                commitGesture();
                auto admitted = *model;
                std::vector<Id> assets;
                for (const auto &r : *result.job.recordings) {
                    attachRecording(admitted, r);
                    assets.push_back(r.asset.id);
                }
                history->adopt(admitted);
                revised();
                view.attachedRecordings += assets.size();
                view.lastAttachedAsset = assets.back();
                view.lastAttachedAssets = std::move(assets);
            } catch (const ProjectError &e) {
                if (correlated)
                    view.attachmentCompleted = {result.job.attachmentRequest, e.code(), e.what()};
                error(e.code(), e.what());
            } catch (const std::exception &e) {
                if (correlated)
                    view.attachmentCompleted = {result.job.attachmentRequest,
                                                ErrorCode::InvalidState, e.what()};
                error(ErrorCode::InvalidState, e.what());
            }
        } else if (result.job.operation == IoOperation::Save) {
            // Edits continue during disk I/O. Only the captured revision was saved.
            view.savedRevision = result.job.revision;
            savedModel = result.job.session;
        } else {
            model = result.loaded ? std::move(result.loaded)
                                  : std::optional<Session>(*result.job.session);
            history = std::make_unique<EditHistory>(*model, admission.state);
            activeGesture = 0;
            activeAddress.reset();
            view.attachedRecordings = 0;
            view.attachmentCompleted = {};
            view.attachmentRejected = {};
            view.lastAttachedAsset.reset();
            view.lastAttachedAssets.clear();
            view.root = std::move(result.job.root);
            ++view.projectEpoch;
            revised();
            view.savedRevision = view.modelRevision;
            savedModel = std::make_shared<const Session>(*model);
        }
    }
    void run() override {
        io.start();
        publish();
        while (!closing.load(std::memory_order_acquire)) {
            if (auto result = io.takeResult()) {
                finishIo(std::move(*result));
                publish();
            }
            std::optional<ProjectCommand> command;
            {
                QMutexLocker lock(&mutex);
                if (commands.empty())
                    wake.wait(&mutex, 4);
                if (!commands.empty()) {
                    command = std::move(commands.front());
                    commands.pop_front();
                }
            }
            if (command && !closing.load(std::memory_order_acquire)) {
                try {
                    if (io.options.beforeCommand)
                        io.options.beforeCommand();
                    if (closing.load(std::memory_order_acquire))
                        break;
                    execute(*command);
                } catch (const ProjectError &e) {
                    if (command->kind == CommandKind::AttachRecording && command->attachmentRequest)
                        view.attachmentRejected = {command->attachmentRequest, e.code(), e.what()};
                    error(e.code(), e.what());
                } catch (const std::exception &e) {
                    if (command->kind == CommandKind::AttachRecording && command->attachmentRequest)
                        view.attachmentRejected = {command->attachmentRequest,
                                                   ErrorCode::InvalidState, e.what()};
                    error(ErrorCode::InvalidState, e.what());
                }
                publish();
            }
        }
        view.closing = true;
        publish();
        io.stopWorker();
        // An already-published save still needs to be reported honestly on close.
        if (auto result = io.takeResult())
            finishIo(std::move(*result));
        view.closed = true;
        publish();
    }
};
ProjectController::ProjectController(ControllerOptions options)
    : state_(std::make_unique<State>(std::move(options))) {
    state_->start();
}
ProjectController::~ProjectController() {
    requestShutdown();
    state_->wait();
}
Admission ProjectController::submit(ProjectCommand command) {
    QMutexLocker lock(&state_->mutex);
    if (state_->closing.load(std::memory_order_acquire))
        return Admission::Closing;
    if (state_->commands.size() == commandCapacity)
        return Admission::Full;
    state_->commands.push_back(std::move(command));
    state_->wake.wakeOne();
    return Admission::Accepted;
}
std::shared_ptr<const ControllerSnapshot> ProjectController::snapshot() const {
    QMutexLocker lock(&state_->mutex);
    return state_->latest;
}
void ProjectController::requestShutdown() noexcept {
    state_->closing.store(true, std::memory_order_release);
    state_->wake.wakeOne();
}
} // namespace soundcurrent::daw::ui
