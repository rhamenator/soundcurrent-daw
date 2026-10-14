// SPDX-License-Identifier: GPL-3.0-only
#include "project_controller.hpp"
#include <soundcurrent/native_take_admission.hpp>
#include <soundcurrent/comp_assembly_adoption.hpp>
#include <soundcurrent/warp.hpp>
#include <soundcurrent/stretch_render_protocol.hpp>
#include <QMutex>
#include <QThread>
#include <QWaitCondition>
#include <atomic>
#include <algorithm>
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
void checkLoopTake(const LoopTakeRetention &r,StateBudget budget) {
    if (!r.take || !r.take->projectEpoch)
        throw ProjectError(ErrorCode::InvalidState,"Loop take has no original project epoch");
    const auto &take=*r.take;
    PayloadCharge charge("Loop receipt group",budget.memoryBudgetBytes);
    charge.add(take.sources.size(),sizeof(LoopRecordedSource)+sizeof(LoopRecordingBinding)+8192);
    charge.add(128*1024);
    (void)encodeLoopRecordingContext(take.context);
    const auto &i=take.context.intent;
    (void)loopRecordingExtent(i,take.sources);
    for (const auto &raw:take.sources) {
        const auto &s=raw.recording.spec;
        const auto binding=std::find_if(take.context.sources.begin(),take.context.sources.end(),
            [&](const auto &b){return b.asset==s.assetId;});
        if (binding==take.context.sources.end() || binding->track!=s.trackId ||
            binding->layout!=s.capture.layout || binding->captureBegin!=s.capture.startFrame ||
            binding->inputLatencyFrames!=s.inputLatencyFrames)
            throw ProjectError(ErrorCode::InvalidState,"Loop receipt differs from its prepared source context");
    }
}
Session retainLoop(const Session &s,const LoopTakeRetention &r,const std::string &name,StateBudget budget) {
    checkLoopTake(r,budget);
    return withLoopTakeLanes(s,r.take->context.intent,r.take->sources,r.first,r.count,name,r.allowPartial,budget);
}

struct IoJob {
    IoOperation operation = IoOperation::None;
    std::filesystem::path root;
    std::shared_ptr<const Session> session;
    std::uint64_t revision = 0;
    std::shared_ptr<const std::vector<RecordingResult>> recordings = nullptr;
    std::uint64_t attachmentRequest = 0;
    std::shared_ptr<ResourceLease> loadReservation = nullptr;
    std::shared_ptr<const ManualRecordedGroup> retainedTake = nullptr;
    std::string takeName = {};
    std::shared_ptr<const LoopTakeRetention> retainedLoop = nullptr;
};
struct IoResult {
    IoJob job;
    std::shared_ptr<const Session> loaded;
    std::optional<ErrorCode> error;
    std::string diagnostic;
};
class IoWorker : public QThread {
  public:
    ControllerOptions options;
    SessionSnapshots snapshots;
    std::atomic<bool> &closing;
    QMutex mutex;
    QWaitCondition wake;
    std::optional<IoJob> job;
    std::optional<IoResult> result;
    bool busy = false, quit = false;
    IoWorker(ControllerOptions o, SessionSnapshots s, std::atomic<bool> &c)
        : options(std::move(o)), snapshots(std::move(s)), closing(c) {}
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
                    finished.loaded =
                        snapshots.adopt(store.load(), std::move(*next.loadReservation));
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
                        if (next.retainedLoop) {
                            const auto &take=*next.retainedLoop->take;
                            const auto context=inspectLoopRecordingContext(job);
                            const auto raw=std::find_if(take.sources.begin(),take.sources.end(),
                                [&](const auto &s){return s.recording.spec.assetId==r.spec.assetId;});
                            if (!context || *context!=take.context || raw==take.sources.end() ||
                                verified.timingOrigin!=std::optional(raw->origin) ||
                                verified.endReason!=raw->endReason)
                                throw ProjectError(ErrorCode::MediaMismatch,
                                    "Loop take differs from original context or capture origin");
                            validateLoopRecordingCheckpoint(*context,verified);
                        }
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
    std::shared_ptr<const ControllerSnapshot> latest;
    std::atomic<bool> closing{false};
    ProjectBudget admission;
    HistoryBudget historyBudget;
    ResourceLedger memory;
    SessionSnapshots snapshots;
    ResourceLease canonicalReservation, historyReservation;
    IoWorker io;
    std::unique_ptr<Session> model;
    std::unique_ptr<EditHistory> history;
    std::shared_ptr<const Session> savedModel;
    std::uint64_t activeGesture = 0;
    std::optional<ParameterAddress> activeAddress;
    std::shared_ptr<const Session> gestureBefore;
    ControllerSnapshot view;
    explicit State(ControllerOptions options)
        : admission(options.admission), historyBudget(options.historyBudget),
          memory(options.memoryBytes, "Controller project memory"),
          snapshots(memory.child(options.snapshotBytes, "Retained project snapshots"),
                    options.admission.state),
          io(std::move(options), snapshots, closing) {
        validateHistoryBudget(historyBudget);
        view.historyBudget = historyBudget;
        view.snapshotResources = snapshots.usage();
        view.memoryResources = memory.usage();
        view.canonicalBytes = canonicalReservation.bytes();
        view.historyBytes = historyReservation.bytes();
        latest = std::make_shared<const ControllerSnapshot>(view);
    }
    ~State() override = default;
    void publish() {
        view.historyBudget = historyBudget;
        view.historyResources = history ? history->resources() : HistoryResources{};
        view.snapshotResources = snapshots.usage();
        view.memoryResources = memory.usage();
        view.canonicalBytes = canonicalReservation.bytes();
        view.historyBytes = historyReservation.bytes();
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
            gestureBefore.reset();
        }
    }
    void revised() {
        if (view.modelRevision == std::numeric_limits<std::uint64_t>::max())
            throw ProjectError(ErrorCode::InvalidState, "Project revision exhausted");
        ++view.modelRevision;
    }
    std::shared_ptr<const Session> prepareSnapshot(const Session &value) const {
        if (view.session && *view.session == value)
            return view.session;
        return snapshots.copy(value);
    }
    std::size_t historyPayload() const {
        if (!history)
            return 0;
        const auto usage = history->resources();
        PayloadCharge bytes("Controller history ownership",
                            std::numeric_limits<std::size_t>::max());
        bytes.add(usage.retainedBytes);
        bytes.add(usage.activeBytes);
        return bytes.bytes();
    }
    void reconcile(ResourceLease &work) {
        const auto canonical = model ? sessionPayloadBytes(*model, admission.state) : 0;
        const auto retained = historyPayload();
        // Return shrinking persistent credits to the work lease before funding growth.
        if (canonical < canonicalReservation.bytes())
            canonicalReservation.transferTo(work, canonicalReservation.bytes() - canonical);
        if (retained < historyReservation.bytes())
            historyReservation.transferTo(work, historyReservation.bytes() - retained);
        if (canonical > canonicalReservation.bytes())
            work.transferTo(canonicalReservation, canonical - canonicalReservation.bytes());
        if (retained > historyReservation.bytes())
            work.transferTo(historyReservation, retained - historyReservation.bytes());
    }
    struct OperationWork {
        State &state;
        ResourceLease lease;
        explicit OperationWork(State &owner, std::size_t stagingBytes = 0)
            : state(owner), lease(owner.memory.reserve(stagingBytes)) {}
        ~OperationWork() {
            // Admission was completed before mutation. Reconciliation transfers existing
            // credits; failure here is an internal accounting invariant violation.
            state.reconcile(lease);
        }
        void admit(std::size_t declaredPeak) {
            const auto owned =
                state.canonicalReservation.bytes() + state.historyReservation.bytes();
            declaredPeak = std::max(declaredPeak, state.history ? state.history->checkCommit() : 0);
            const auto extra = declaredPeak > owned ? declaredPeak - owned : 0;
            if (extra > lease.bytes()) {
                if (lease.bytes())
                    lease.resize(extra);
                else
                    lease = state.memory.reserve(extra);
            }
        }
    };
    std::size_t stagingBytes() const {
        PayloadCharge bytes("Controller candidate staging",
                            std::numeric_limits<std::size_t>::max());
        if (model)
            bytes.add(sessionPayloadBytes(*model, admission.state), 2);
        return bytes.bytes();
    }
    void execute(ProjectCommand &command) {
        switch (command.kind) {
        case CommandKind::MemoryLimits:
            if (!command.memoryBytes || !command.memoryRequest)
                throw ProjectError(ErrorCode::InvalidParameter,
                                   "Project memory request is missing");
            if (command.snapshotBytes)
                memory.configureWith(snapshots.resourceLedger(), *command.memoryBytes,
                                     *command.snapshotBytes);
            else
                memory.configure(*command.memoryBytes);
            view.memoryCompleted = {command.memoryRequest, {}, {}};
            break;
        case CommandKind::SnapshotLimits:
            if (!command.snapshotBytes || !command.snapshotRequest)
                throw ProjectError(ErrorCode::InvalidParameter,
                                   "Snapshot resource request is missing");
            snapshots.configure(*command.snapshotBytes);
            view.snapshotCompleted = {command.snapshotRequest, {}, {}};
            break;
        case CommandKind::HistoryLimits:
            if (!command.historyBudget || !command.historyRequest)
                throw ProjectError(ErrorCode::InvalidParameter, "Undo resource request is missing");
            if (view.io != IoOperation::None || activeGesture)
                throw ProjectError(
                    ErrorCode::InvalidState,
                    "Finish project I/O and the active gesture before changing Undo limits");
            validateHistoryBudget(*command.historyBudget);
            if (history)
                history->configure(*command.historyBudget);
            historyBudget = *command.historyBudget;
            view.historyCompleted = {command.historyRequest, {}, {}};
            break;
        case CommandKind::Create:
        case CommandKind::Open: {
            if (view.io != IoOperation::None)
                throw ProjectError(ErrorCode::InvalidState,
                                   "A project operation is already active");
            requireCleanReplacement();
            IoJob job;
            job.operation =
                command.kind == CommandKind::Create ? IoOperation::Create : IoOperation::Open;
            job.root = command.path;
            if (job.operation == IoOperation::Create)
                job.session = snapshots.copy(makeOneTrackSession(command.name, "Audio 1",
                                                                command.sampleRate));
            else
                job.loadReservation = std::make_shared<ResourceLease>(snapshots.reserveLoad());
            OperationWork work(*this);
            work.admit(0);
            commitGesture();
            io.startJob(std::move(job));
            view.io = command.kind == CommandKind::Create ? IoOperation::Create : IoOperation::Open;
            break;
        }
        case CommandKind::Save: {
            requireModel();
            OperationWork work(*this);
            work.admit(0);
            commitGesture();
            if (view.io != IoOperation::None)
                throw ProjectError(ErrorCode::InvalidState,
                                   "A project operation is already active");
            io.startJob({IoOperation::Save, view.root, view.session, view.modelRevision});
            view.io = IoOperation::Save;
            break;
        }
        case CommandKind::Parameter: {
            requireModel();
            if (view.io == IoOperation::Create || view.io == IoOperation::Open)
                throw ProjectError(ErrorCode::InvalidState, "Project replacement is in progress");
            if (!command.address || !command.gesture)
                throw ProjectError(ErrorCode::InvalidParameter, "Parameter gesture is missing");
            // Validate the proposed value before altering the current gesture/history.
            OperationWork work(*this, stagingBytes());
            auto proposed = *model;
            setParameterValue(proposed, *command.address, command.value);
            const bool begins =
                activeGesture != command.gesture || activeAddress != command.address;
            const auto preflightPeak =
                begins ? history->checkBegin(*command.address) : history->checkUpdate();
            work.admit(preflightPeak);
            auto publication = prepareSnapshot(proposed);
            if (begins) {
                commitGesture();
                history->begin(*command.address);
                gestureBefore = view.session;
                activeGesture = command.gesture;
                activeAddress = command.address;
            }
            const auto previous = parameterValue(*model, *command.address);
            history->update(command.value);
            if (previous != command.value)
                revised();
            view.session = std::move(publication);
            if (command.final)
                commitGesture();
            history->acceptPreflight(preflightPeak);
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
            OperationWork work(*this, stagingBytes());
            auto proposed = *model;
            setRouteValue(proposed, *command.routeAddress, value,
                          admission.state); // Reject before committing a gesture.
            const auto preflightPeak = history->checkRoute(*command.routeAddress, value);
            work.admit(preflightPeak);
            auto publication = prepareSnapshot(proposed);
            commitGesture();
            if (history->route(*command.routeAddress, value))
                revised();
            view.session = std::move(publication);
            history->acceptPreflight(preflightPeak);
            break;
        }
        case CommandKind::Monitoring: {
            requireModel();
            if (!command.monitoringTrack || view.io == IoOperation::Create ||
                view.io == IoOperation::Open)
                throw ProjectError(ErrorCode::InvalidState,
                                   "Monitoring change needs the current project");
            OperationWork work(*this, stagingBytes());
            auto proposed = *model;
            setMonitoringValue(proposed, *command.monitoringTrack, command.monitoring);
            const auto preflightPeak =
                history->checkMonitoring(*command.monitoringTrack, command.monitoring);
            work.admit(preflightPeak);
            auto publication = prepareSnapshot(proposed);
            commitGesture();
            if (history->monitoring(*command.monitoringTrack, command.monitoring))
                revised();
            view.session = std::move(publication);
            history->acceptPreflight(preflightPeak);
            break;
        }
        case CommandKind::Structural: {
            requireModel();
            if (view.io == IoOperation::Create || view.io == IoOperation::Open)
                throw ProjectError(ErrorCode::InvalidState, "Project replacement is in progress");
            if(command.structuralGuard){const auto &g=*command.structuralGuard;
                if(g.projectEpoch!=view.projectEpoch || g.project!=model->id || g.root!=view.root)
                    throw ProjectError(ErrorCode::InvalidState,"Background edit belongs to a replaced project");
            }
            if(command.stretchResult && (!command.structuralGuard || command.compAssemblyResult || !command.edits.empty() || !command.stretchResult->ownedBy(memory)))
                throw ProjectError(ErrorCode::InvalidState,"Unbound or unadmitted stretch result");
            if(command.nativeTake && (!command.structuralGuard || command.stretchResult || command.compAssemblyResult || !command.edits.empty() ||
                !command.nativeTake->ownedBy(memory) || command.nativeTake->project()!=model->id || command.nativeTake->root()!=view.root))
                throw ProjectError(ErrorCode::InvalidState,"Native take adoption requires verified current project evidence");
            if(std::any_of(command.edits.begin(),command.edits.end(),[](const auto &e){return std::holds_alternative<InsertNativeTake>(e);}))
                throw ProjectError(ErrorCode::InvalidState,"Desktop native take adoption requires a verified result");
            if(std::any_of(command.edits.begin(),command.edits.end(),[](const auto &e){return std::holds_alternative<ApplyClipStretch>(e);}))
                throw ProjectError(ErrorCode::InvalidState,"Desktop stretch adoption requires a verified result");
            if(command.compAssemblyResult){const auto &r=*command.compAssemblyResult;
                const auto bytes=view.root.generic_u8string();
                const std::string rootIdentity(reinterpret_cast<const char *>(bytes.data()),bytes.size());
                if(!command.structuralGuard || command.stretchResult || command.nativeTake || !command.edits.empty() ||
                    !r.ownedBy(memory) || r.project()!=model->id || r.root()!=view.root ||
                    r.context()!=CompHandlePlanContext{view.projectEpoch,rootIdentity})
                    throw ProjectError(ErrorCode::InvalidState,"Comp adoption requires verified current owner evidence");
            }
            if(std::any_of(command.edits.begin(),command.edits.end(),[](const auto &e){return std::holds_alternative<ApplyCompAssembly>(e);}))
                throw ProjectError(ErrorCode::InvalidState,"Desktop comp adoption requires a verified result");
            PayloadCharge stretchStaging("Verified stretch candidate staging",SIZE_MAX);
            stretchStaging.add(stagingBytes());
            if(command.stretchResult){
                const auto &edit=command.stretchResult->edit();stretchStaging.add(128*1024);
                if(edit.value.warp)stretchStaging.add(warpPayloadBytes(*edit.value.warp),4);
            }
            OperationWork work(*this, stretchStaging.bytes());
            auto proposed = *model;
            std::vector<SessionEdit> verified;
            if(command.stretchResult)verified.push_back(command.stretchResult->edit());
            if(command.nativeTake)verified.push_back(command.nativeTake->edit());
            if(command.compAssemblyResult)verified.push_back(command.compAssemblyResult->edit());
            applySessionEdits(proposed, (command.stretchResult || command.nativeTake || command.compAssemblyResult)?verified:command.edits,
                              admission.state); // Reject before committing a gesture.
            if (proposed != *model &&
                view.modelRevision == std::numeric_limits<std::uint64_t>::max())
                throw ProjectError(ErrorCode::InvalidState, "Project revision exhausted");
            const auto preflightPeak = history->checkAdopt(proposed);
            work.admit(preflightPeak);
            auto publication = prepareSnapshot(proposed);
            commitGesture();
            if (history->adopt(proposed))
                revised();
            view.session = std::move(publication);
            history->acceptPreflight(preflightPeak);
            if(command.structuralRequest)view.structuralCompleted={command.structuralRequest,{}, {}};
            break;
        }
        case CommandKind::CancelGesture:
            requireModel();
            if (activeGesture == command.gesture && activeGesture) {
                OperationWork work(*this); // Cancel only returns credits; needs no new allocation.
                history->cancel();
                activeGesture = 0;
                activeAddress.reset();
                view.session = std::move(gestureBefore);
                revised();
            }
            break;
        case CommandKind::Undo:
        case CommandKind::Redo: {
            requireModel();
            if (view.io == IoOperation::Create || view.io == IoOperation::Open)
                throw ProjectError(ErrorCode::InvalidState, "Project replacement is in progress");
            OperationWork work(*this, stagingBytes());
            std::size_t preflightPeak = 0;
            const auto proposed =
                history->previewTransfer(command.kind == CommandKind::Redo, &preflightPeak);
            work.admit(preflightPeak);
            auto publication = proposed ? prepareSnapshot(*proposed) : view.session;
            commitGesture();
            if (command.kind == CommandKind::Undo ? history->undo() : history->redo())
                revised();
            view.session = std::move(publication);
            break;
        }
        case CommandKind::AttachRecording: {
            requireModel();
            if (view.io != IoOperation::None ||
                int(bool(command.recording)) + int(bool(command.recordings)) + int(bool(command.retainedTake)) + int(bool(command.retainedLoop)) != 1 || command.path != view.root)
                throw ProjectError(ErrorCode::InvalidState,
                                   "Take attachment needs the current project and idle I/O owner");
            // Shape/identity validation is transactional; disk/journal verification belongs
            // to the I/O worker. No speculative asset is published to the canonical model.
            if (command.retainedTake && (!command.structuralGuard ||
                command.structuralGuard->projectEpoch != view.projectEpoch ||
                command.structuralGuard->project != model->id ||
                command.structuralGuard->root != view.root || !command.retainedTake->complete()))
                throw ProjectError(ErrorCode::InvalidState, "Retained take belongs to a replaced project or is incomplete");
            if (command.retainedLoop && (!command.structuralGuard || !command.retainedLoop->take ||
                command.structuralGuard->projectEpoch!=view.projectEpoch ||
                command.structuralGuard->project!=model->id || command.structuralGuard->root!=view.root ||
                command.retainedLoop->take->projectEpoch!=view.projectEpoch))
                throw ProjectError(ErrorCode::InvalidState,"Loop take belongs to a replaced project");
            PayloadCharge staging("Retained take candidate staging", SIZE_MAX);
            staging.add(stagingBytes());
            if (command.retainedTake) {
                staging.add(command.retainedTake->lanes.size(), sizeof(ManualRecordedLane) + sizeof(RecordingResult) + 8192);
                staging.add(command.name.size() + 1, command.retainedTake->lanes.size());
            }
            if (command.retainedLoop) {
                checkLoopTake(*command.retainedLoop,admission.state);
                staging.add(command.retainedLoop->take->sources.size(),sizeof(LoopRecordedSource)+sizeof(LoopRecordingBinding)+8192);
                staging.add(128*1024);
            }
            OperationWork work(*this, staging.bytes());
            auto proposed = *model;
            std::shared_ptr<const std::vector<RecordingResult>> retainedRecords;
            if (command.retainedTake) {
                auto records = std::make_shared<std::vector<RecordingResult>>();
                records->reserve(command.retainedTake->lanes.size());
                for (const auto &lane : command.retainedTake->lanes) records->push_back(*lane.result);
                retainedRecords = std::move(records);
            }
            if (command.retainedLoop) {
                auto records=std::make_shared<std::vector<RecordingResult>>();
                records->reserve(command.retainedLoop->take->sources.size());
                for (const auto &raw:command.retainedLoop->take->sources) records->push_back(raw.recording);
                retainedRecords=std::move(records);
            }
            const auto recordings = (command.retainedTake || command.retainedLoop) ? retainedRecords : command.recordings
                                        ? command.recordings
                                        : std::make_shared<const std::vector<RecordingResult>>(
                                              std::vector<RecordingResult>{*command.recording});
            admitReceipts(recordings, admission.state);
            if (command.retainedTake)
                proposed = withManualTakeLanes(*model, *command.retainedTake, command.name);
            else if (command.retainedLoop)
                proposed = retainLoop(*model,*command.retainedLoop,command.name,admission.state);
            else for (const auto &r : *recordings)
                attachRecording(proposed, r);
            const auto preflightPeak = history->checkAdopt(proposed);
            work.admit(preflightPeak);
            auto publication = prepareSnapshot(proposed);
            commitGesture();
            IoJob job{IoOperation::AttachRecording, view.root, std::move(publication),
                      view.modelRevision, recordings};
            job.attachmentRequest = command.attachmentRequest;
            job.retainedTake = command.retainedTake;
            job.retainedLoop = command.retainedLoop;
            job.takeName = command.name;
            io.startJob(std::move(job));
            view.io = IoOperation::AttachRecording;
            history->acceptPreflight(preflightPeak);
            break;
        }
        case CommandKind::Barrier: {
            if (!command.barrier)
                throw ProjectError(ErrorCode::InvalidState, "Barrier token is missing");
            OperationWork work(*this);
            work.admit(0);
            commitGesture();
            view.lastBarrier = command.barrier;
            view.barrierSession = view.session;
            view.barrierRoot = view.root;
            view.barrierRevision = view.modelRevision;
            break;
        }
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
                    result.job.recordings->empty() || !result.job.session ||
                    model->id != result.job.recordings->front().spec.projectId)
                    throw ProjectError(ErrorCode::InvalidState,
                                       "Recording project changed before attachment");
                if (result.job.retainedLoop && result.job.retainedLoop->take->projectEpoch!=view.projectEpoch)
                    throw ProjectError(ErrorCode::InvalidState,"Loop recording project was replaced before retention");
                if (view.modelRevision == std::numeric_limits<std::uint64_t>::max())
                    throw ProjectError(ErrorCode::InvalidState, "Project revision exhausted");
                // Preserve scalar edits accepted while files were verified.
                OperationWork work(*this, stagingBytes());
                std::optional<Session> admitted;
                if (result.job.revision != view.modelRevision)
                    admitted = result.job.retainedLoop
                        ? retainLoop(*model,*result.job.retainedLoop,result.job.takeName,admission.state)
                        : result.job.retainedTake
                        ? withManualTakeLanes(*model, *result.job.retainedTake, result.job.takeName)
                        : *model;
                std::vector<Id> assets;
                for (const auto &r : *result.job.recordings) {
                    if (admitted && !result.job.retainedTake && !result.job.retainedLoop)
                        attachRecording(*admitted, r);
                    assets.push_back(r.asset.id);
                }
                // With no intervening model change, the verified proposal already
                // owns the exact publication, including its generated clip IDs.
                const auto &candidate = admitted ? *admitted : *result.job.session;
                const auto preflightPeak = history->checkAdopt(candidate);
                work.admit(preflightPeak);
                auto publication = admitted ? prepareSnapshot(candidate) : result.job.session;
                commitGesture();
                history->adopt(candidate);
                revised();
                view.session = std::move(publication);
                view.attachedRecordings += assets.size();
                view.lastAttachedAsset = assets.back();
                view.lastAttachedAssets = std::move(assets);
                history->acceptPreflight(preflightPeak);
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
            try {
                auto publication = result.loaded ? std::move(result.loaded) : result.job.session;
                auto nextReservation =
                    memory.reserve(sessionPayloadBytes(*publication, admission.state));
                auto nextModel = std::make_unique<Session>(*publication);
                nextReservation.resize(sessionPayloadBytes(*nextModel, admission.state));
                auto nextHistory =
                    std::make_unique<EditHistory>(*nextModel, admission.state, historyBudget);
                if (view.modelRevision == std::numeric_limits<std::uint64_t>::max())
                    throw ProjectError(ErrorCode::InvalidState, "Project revision exhausted");
                history = std::move(nextHistory);
                model = std::move(nextModel);
                canonicalReservation = std::move(nextReservation);
                historyReservation.resize(0);
                view.session = publication;
                activeGesture = 0;
                activeAddress.reset();
                gestureBefore.reset();
                view.attachedRecordings = 0;
                view.attachmentCompleted = {};
                view.attachmentRejected = {};
                view.structuralCompleted={};view.structuralRejected={};
                view.lastAttachedAsset.reset();
                view.lastAttachedAssets.clear();
                view.root = std::move(result.job.root);
                ++view.projectEpoch;
                revised();
                view.savedRevision = view.modelRevision;
                savedModel = std::move(publication);
            } catch (const ProjectError &e) {
                error(e.code(), e.what());
            } catch (const std::exception &e) {
                error(ErrorCode::InvalidState, e.what());
            }
        }
    }
    void run() override {
        io.start();
        if (io.options.beforeInitialPublish)
            io.options.beforeInitialPublish();
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
                    if(command->kind==CommandKind::Structural && command->structuralRequest)
                        view.structuralRejected={command->structuralRequest,e.code(),e.what()};
                    if (command->kind == CommandKind::MemoryLimits && command->memoryRequest)
                        view.memoryCompleted = {command->memoryRequest, e.code(), e.what()};
                    if (command->kind == CommandKind::SnapshotLimits && command->snapshotRequest)
                        view.snapshotCompleted = {command->snapshotRequest, e.code(), e.what()};
                    if (command->kind == CommandKind::HistoryLimits && command->historyRequest)
                        view.historyCompleted = {command->historyRequest, e.code(), e.what()};
                    if (command->kind == CommandKind::AttachRecording && command->attachmentRequest)
                        view.attachmentRejected = {command->attachmentRequest, e.code(), e.what()};
                    error(e.code(), e.what());
                } catch (const std::exception &e) {
                    if(command->kind==CommandKind::Structural && command->structuralRequest)
                        view.structuralRejected={command->structuralRequest,ErrorCode::InvalidState,e.what()};
                    if (command->kind == CommandKind::MemoryLimits && command->memoryRequest)
                        view.memoryCompleted = {command->memoryRequest, ErrorCode::InvalidState,
                                                e.what()};
                    if (command->kind == CommandKind::SnapshotLimits && command->snapshotRequest)
                        view.snapshotCompleted = {command->snapshotRequest, ErrorCode::InvalidState,
                                                  e.what()};
                    if (command->kind == CommandKind::HistoryLimits && command->historyRequest)
                        view.historyCompleted = {command->historyRequest, ErrorCode::InvalidState,
                                                 e.what()};
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
ResourceUsage ProjectController::snapshotResources() const {
    return state_->snapshots.usage();
}
ResourceUsage ProjectController::memoryResources() const {
    return state_->memory.usage();
}
ResourceLedger ProjectController::resourceLedger() const {
    return state_->memory;
}
void ProjectController::requestShutdown() noexcept {
    state_->closing.store(true, std::memory_order_release);
    state_->wake.wakeOne();
}
} // namespace soundcurrent::daw::ui
