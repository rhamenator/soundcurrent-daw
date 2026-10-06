// SPDX-License-Identifier: GPL-3.0-only
#include "recording_controller.hpp"
#include <QMutex>
#include <QThread>
#include <QWaitCondition>
#include <algorithm>
#include <deque>
#include <limits>
namespace soundcurrent::daw::ui {
namespace {
constexpr std::size_t capacity = 16;
bool active(AudioBridgeStatus s) {
    return s == AudioBridgeStatus::Ready || s == AudioBridgeStatus::Running;
}
#ifdef SC_UI_PIPEWIRE
class NativeRecordingEndpoint : public RecordingEndpoint {
    PipeWireRecording owner_;
    RecordingTelemetry latest_;

  public:
    explicit NativeRecordingEndpoint(const RecordingPreparation &p)
        : owner_(p.root, *p.session, p.spec, p.options) {}
    std::vector<PipeWirePort> ports() override {
        auto ports = owner_.ports();
        std::erase_if(ports, [&](const auto &p) { return p.nodeId == owner_.nodeId(); });
        return ports;
    }
    void connectInputs(const std::vector<PipeWirePort> &p) override {
        owner_.connectInputs(p);
    }
    void connectOutputs(const std::vector<PipeWirePort> &p) override {
        owner_.connectOutputs(p);
    }
    void activate() override {
        owner_.activate();
    }
    void stop() noexcept override {
        owner_.stop();
    }
    RecordingResult result() override {
        return owner_.result();
    }
    std::optional<std::filesystem::path> jobDirectory() override {
        return owner_.jobDirectory();
    }
    EqEvent event(const Session &s, const ParameterAddress &a) override {
        return owner_.prepared().parameterEvent(s, a, 0);
    }
    EqEvent enable(bool v) override {
        return owner_.prepared().enableEvent(v, 0);
    }
    SubmitStatus submit(const EqEvent &e, std::uint64_t r) noexcept override {
        return owner_.submitImmediate(e, r);
    }
    RecordingTelemetry read() override {
        BackendObservation o;
        for (unsigned n = 0; n < 64 && owner_.observation(o); ++n) {
            latest_.inputPeak = o.inputPeak;
            latest_.outputPeak = o.outputPeak;
            latest_.processed = true;
        }
        latest_.receipt.reset();
        ImmediateAcknowledgement receipt;
        for (unsigned n = 0; n < 64 && owner_.acknowledgement(receipt); ++n)
            latest_.receipt = receipt;
        latest_.status = owner_.status();
        latest_.captureStatus = owner_.captureStatus();
        latest_.endReason = owner_.endReason();
        latest_.capturedFrames = owner_.capturedFrames();
        latest_.writtenFrames = owner_.writtenFrames();
        latest_.rejectedFrames = owner_.rejectedFrames();
        latest_.invalidSamples = owner_.invalidInputSamples();
        latest_.droppedMeters = owner_.droppedObservations();
        latest_.droppedReceipts = owner_.droppedAcknowledgements();
        return latest_;
    }
};
#endif
struct Following {
    std::filesystem::path root;
    std::shared_ptr<const Session> session;
    std::uint64_t revision = 0;
};
// First-track recording currently admits only scalar EQ/enable changes. It
// cannot silently keep recording after a project/rate/route/layout/clip change.
bool compatible(const Session &a, const Session &b) {
    if (a.tracks.empty() || b.tracks.empty())
        return false;
    const auto &t = a.tracks.front(), &u = b.tracks.front();
    if (a.id != b.id || a.name != b.name || a.sampleRate != b.sampleRate ||
        a.playheadFrame != b.playheadFrame || a.exportStartFrame != b.exportStartFrame ||
        a.exportEndFrame != b.exportEndFrame || a.assets != b.assets ||
        a.tracks.size() != b.tracks.size() || t.id != u.id || t.name != u.name ||
        t.layout != u.layout || t.clips != u.clips || t.eq.id != u.eq.id ||
        t.eq.bands.size() != u.eq.bands.size())
        return false;
    for (std::size_t n = 0; n < t.eq.bands.size(); ++n)
        if (t.eq.bands[n].id != u.eq.bands[n].id)
            return false;
    return std::equal(a.tracks.begin() + 1, a.tracks.end(), b.tracks.begin() + 1);
}
void checkRecovery(const RecordingCommand &c, const RecordingRecovery &r) {
    if (!c.session || c.session->tracks.empty() || c.job.parent_path() != c.root / "media" ||
        c.job.filename() != utf8Path("capture-" + r.spec.assetId.str()) ||
        c.session->id != r.spec.projectId || c.session->sampleRate != r.spec.capture.sampleRate ||
        c.session->tracks.front().id != r.spec.trackId ||
        c.session->tracks.front().layout != r.spec.capture.layout ||
        std::any_of(c.session->assets.begin(), c.session->assets.end(),
                    [&](const auto &a) { return a.id == r.spec.assetId; }))
        throw ProjectError(ErrorCode::InvalidState, "Recovery belongs to another project or track");
}
} // namespace
struct RecordingController::State : QThread {
    struct Queued {
        RecordingCommand command;
        std::uint64_t epoch;
    };
    mutable QMutex mutex;
    QWaitCondition wake;
    std::deque<Queued> commands;
    std::optional<Following> following, desired;
    std::uint64_t lastFollowed = 0, seenStop = 0, eventRevision = 0, checkedRevision = 0,
                  takeSequence = 0;
    std::atomic<std::uint64_t> stopEpoch{0}, acknowledged{0};
    std::atomic<bool> closing{false};
    std::shared_ptr<const RecordingSnapshot> latest;
    RecordingSnapshot view;
    RecordingControllerOptions options;
    std::unique_ptr<RecordingEndpoint> endpoint;
    std::optional<RecordingPreparation> prepared;
    std::shared_ptr<const Session> acceptedModel;
    bool started = false, processed = false;
    struct Bundle {
        Following target;
        std::vector<EqEvent> events;
        std::size_t submitted = 0;
        std::uint64_t lastEvent = 0;
    };
    std::optional<Bundle> bundle;
    std::chrono::steady_clock::time_point nextInventory;
    explicit State(RecordingControllerOptions o) : options(std::move(o)) {
#ifdef SC_UI_PIPEWIRE
        if (!options.factory)
            options.factory = [](const auto &p) {
                return std::make_unique<NativeRecordingEndpoint>(p);
            };
#endif
        view.supported = bool(options.factory);
        view.phase = view.supported ? RecordingPhase::Idle : RecordingPhase::Unsupported;
        latest = std::make_shared<const RecordingSnapshot>(view);
    }
    void publish() {
        view.pending = bundle || (endpoint && view.acceptedRevision &&
                                  view.desiredRevision != view.appliedRevision);
        auto snapshot = std::make_shared<const RecordingSnapshot>(view);
        QMutexLocker lock(&mutex);
        latest = std::move(snapshot);
    }
    void error(ErrorCode code, std::string detail) {
        view.errorCode = code;
        view.diagnostic = std::move(detail);
        ++view.errorSerial;
    }
    void update(RecordingTelemetry t) {
        if (t.receipt && t.receipt->generation == view.generation) {
            view.appliedEventRevision = t.receipt->revision;
            view.appliedFrame = t.receipt->frame;
        }
        processed = processed || t.processed;
        view.telemetry = std::move(t);
        if (bundle && bundle->submitted == bundle->events.size() &&
            view.appliedEventRevision >= bundle->lastEvent) {
            view.appliedRevision = bundle->target.revision;
            bundle.reset();
        } else if (!bundle && prepared && processed && !view.appliedRevision) {
            view.appliedRevision = view.acceptedRevision;
            view.appliedFrame = prepared->spec.capture.startFrame;
        }
    }
    void retain(std::filesystem::path root, RecordingResult r) {
        if (view.take || takeSequence == UINT64_MAX)
            throw ProjectError(ErrorCode::InvalidState,
                               "Unacknowledged take or exhausted take sequence");
        view.take = PendingTake{
            std::move(root), std::make_shared<const RecordingResult>(std::move(r)), ++takeSequence};
    }
    void stopEndpoint() {
        if (!endpoint)
            return;
        view.phase = RecordingPhase::Finalizing;
        publish();
        endpoint->stop();
        try {
            update(endpoint->read());
            view.job = endpoint->jobDirectory();
            if (started)
                retain(prepared->root, endpoint->result());
        } catch (const ProjectError &e) {
            error(e.code(), e.what());
        } catch (const std::exception &e) {
            error(ErrorCode::Io, e.what());
        }
        endpoint.reset();
        prepared.reset();
        acceptedModel.reset();
        bundle.reset();
        started = false;
        view.ports.reset();
        view.telemetry.inputPeak = view.telemetry.outputPeak = 0;
    }
    void reconcile(bool admitReady = false) {
        if (!endpoint || !prepared || !desired || desired->revision < prepared->modelRevision)
            return;
        view.desiredRevision = desired->revision;
        if (checkedRevision != desired->revision) {
            validate(*desired->session);
            if (desired->root != prepared->root ||
                !compatible(*prepared->session, *desired->session))
                throw ProjectError(ErrorCode::InvalidState,
                                   "Project structure changed; recording stopped");
            checkedRevision = desired->revision;
        }
        if (view.phase == RecordingPhase::Ready && !admitReady)
            return;
        if (!bundle && desired->revision > view.acceptedRevision) {
            Bundle next;
            next.target = *desired;
            const auto &old = acceptedModel->tracks.front().eq;
            const auto &t = desired->session->tracks.front();
            for (std::size_t n = 0; n < t.eq.bands.size(); ++n)
                if (t.eq.bands[n] != old.bands[n])
                    next.events.push_back(
                        endpoint->event(*desired->session,
                                        {t.id, t.eq.id, t.eq.bands[n].id, BandParameter::GainDb}));
            if (t.eq.enabled != old.enabled)
                next.events.push_back(endpoint->enable(t.eq.enabled));
            if (next.events.empty()) {
                acceptedModel = desired->session;
                view.acceptedRevision = desired->revision;
                if (processed)
                    view.appliedRevision = desired->revision;
            } else
                bundle = std::move(next);
        }
        if (!bundle)
            return;
        while (bundle->submitted < bundle->events.size()) {
            if (eventRevision == UINT64_MAX)
                throw ProjectError(ErrorCode::InvalidState, "Recording event revision exhausted");
            const auto revision = eventRevision + 1;
            const auto admitted = endpoint->submit(bundle->events[bundle->submitted], revision);
            if (admitted == SubmitStatus::Full)
                return;
            if (admitted != SubmitStatus::Accepted)
                throw ProjectError(ErrorCode::InvalidState,
                                   "Recording rejected a prepared EQ change");
            eventRevision = revision;
            bundle->lastEvent = revision;
            ++bundle->submitted;
        }
        acceptedModel = bundle->target.session;
        view.acceptedRevision = bundle->target.revision;
    }
    bool interrupted(std::uint64_t epoch) const {
        return closing.load(std::memory_order_acquire) ||
               stopEpoch.load(std::memory_order_acquire) != epoch;
    }
    void execute(Queued &q) {
        auto &c = q.command;
        if (view.take)
            throw ProjectError(ErrorCode::InvalidState,
                               "Attach or keep the pending take before another job");
        if (c.kind == RecordingCommandKind::Prepare) {
            if (!options.factory || !c.session || !c.modelRevision || c.session->tracks.empty())
                throw ProjectError(ErrorCode::InvalidState,
                                   "Recording backend/project/track unavailable");
            validate(*c.session);
            if (endpoint) {
                if (started)
                    throw ProjectError(ErrorCode::InvalidState,
                                       "Stop recording before preparing another take");
                stopEndpoint();
            }
            view.phase = RecordingPhase::Preparing;
            publish();
            if (options.beforePrepare)
                options.beforePrepare();
            if (interrupted(q.epoch))
                return;
            if (view.generation == UINT64_MAX)
                throw ProjectError(ErrorCode::InvalidState, "Recording generation exhausted");
            RecordingPreparation p;
            p.root = c.root;
            p.session = c.session;
            p.modelRevision = c.modelRevision;
            p.spec.projectId = c.session->id;
            p.spec.trackId = c.session->tracks.front().id;
            p.spec.capture.sampleRate = c.session->sampleRate;
            p.spec.capture.layout = c.session->tracks.front().layout;
            p.spec.capture.startFrame = c.session->playheadFrame;
            p.options = options.nativeOptions;
            p.options.monitoring = c.monitoring;
            p.options.bridge.generation = view.generation + 1;
            p.spec.capture.maximumCallbackFrames = p.options.bridge.maximumFrames;
            prepared = p;
            endpoint = options.factory(p);
            if (!endpoint)
                throw ProjectError(ErrorCode::InvalidState, "Recording factory returned no owner");
            if (interrupted(q.epoch)) {
                stopEndpoint();
                return;
            }
            acceptedModel = c.session;
            eventRevision = checkedRevision = 0;
            processed = false;
            bundle.reset();
            view.telemetry = {};
            view.preview.reset();
            view.job.reset();
            view.generation = p.options.bridge.generation;
            view.channels = p.spec.capture.layout.channels;
            view.sampleRate = p.spec.capture.sampleRate;
            view.monitoring = c.monitoring;
            view.desiredRevision = view.acceptedRevision = c.modelRevision;
            view.appliedRevision = view.appliedEventRevision = view.appliedFrame = 0;
            view.ports = std::make_shared<const std::vector<PipeWirePort>>(endpoint->ports());
            nextInventory = std::chrono::steady_clock::now() + std::chrono::milliseconds(250);
            view.phase = RecordingPhase::Ready;
        } else if (c.kind == RecordingCommandKind::Start) {
            if (!c.armed || !endpoint || view.phase != RecordingPhase::Ready ||
                c.inputs.size() != view.channels ||
                (view.monitoring == RecordingMonitor::PostEq ? c.outputs.size() != view.channels
                                                             : !c.outputs.empty()))
                throw ProjectError(ErrorCode::InvalidState,
                                   "Prepare, arm and select every required recording channel");
            reconcile(true);
            endpoint->connectInputs(c.inputs);
            if (view.monitoring == RecordingMonitor::PostEq)
                endpoint->connectOutputs(c.outputs);
            if (interrupted(q.epoch))
                return;
            started = true;
            endpoint->activate();
            view.job = endpoint->jobDirectory();
            view.phase = RecordingPhase::Recording;
        } else {
            if (endpoint)
                throw ProjectError(ErrorCode::InvalidState, "Stop recording setup before recovery");
            if (!c.session || !c.modelRevision || c.job.parent_path() != c.root / "media")
                throw ProjectError(ErrorCode::InvalidState,
                                   "Choose an owned recording job in this project's media folder");
            validate(*c.session);
            view.phase = c.kind == RecordingCommandKind::Inspect ? RecordingPhase::Inspecting
                                                                 : RecordingPhase::Recovering;
            publish();
            const auto inspected = inspectRecording(c.job);
            checkRecovery(c, inspected);
            if (!inspected.committedFrames)
                throw ProjectError(ErrorCode::InvalidState,
                                   "Recording has no verified frames to recover");
            if (c.kind == RecordingCommandKind::Inspect) {
                if (interrupted(q.epoch))
                    return;
                view.preview = inspected;
                view.job = c.job;
                ++view.previewSequence;
            } else {
                if (!view.preview || view.job != c.job ||
                    c.previewSequence != view.previewSequence || *view.preview != inspected)
                    throw ProjectError(ErrorCode::InvalidState,
                                       "Recording changed; inspect it again before recovery");
                if (interrupted(q.epoch))
                    return;
                auto recovered = recoverRecording(c.root, c.job);
                view.job = c.root / "media" / ("capture-" + recovered.spec.assetId.str());
                const auto copied = inspectRecording(*view.job);
                if (copied.committedFrames != inspected.committedFrames ||
                    copied.sampleSha256 != inspected.sampleSha256)
                    throw ProjectError(
                        ErrorCode::MediaMismatch,
                        "Source checkpoint changed during recovery; inspect the retained copy");
                retain(c.root, std::move(recovered));
                view.preview.reset();
            }
            view.phase = view.supported ? RecordingPhase::Idle : RecordingPhase::Unsupported;
        }
        ++view.completedCommands;
    }
    void run() override {
        while (!closing.load(std::memory_order_acquire)) {
            const auto epoch = stopEpoch.load(std::memory_order_acquire);
            if (epoch != seenStop) {
                const auto errors = view.errorSerial;
                stopEndpoint();
                seenStop = epoch;
                view.stopAcknowledged = epoch;
                view.phase =
                    view.errorSerial != errors
                        ? RecordingPhase::Fault
                        : (view.supported ? RecordingPhase::Idle : RecordingPhase::Unsupported);
            }
            if (view.take && acknowledged.load(std::memory_order_acquire) == view.take->sequence) {
                view.take.reset();
                acknowledged.store(0, std::memory_order_release);
            }
            std::optional<Queued> q;
            {
                QMutexLocker lock(&mutex);
                if (following) {
                    desired = std::move(following);
                    following.reset();
                }
                if (!commands.empty()) {
                    q = std::move(commands.front());
                    commands.pop_front();
                }
            }
            if (q && !interrupted(q->epoch)) {
                const bool wasStarted = started;
                try {
                    execute(*q);
                } catch (const ProjectError &e) {
                    error(e.code(), e.what());
                    if (!wasStarted) {
                        stopEndpoint();
                        view.phase = RecordingPhase::Fault;
                    }
                } catch (const std::exception &e) {
                    error(ErrorCode::Io, e.what());
                    if (!wasStarted) {
                        stopEndpoint();
                        view.phase = RecordingPhase::Fault;
                    }
                }
            }
            try {
                if (endpoint) {
                    update(endpoint->read());
                    if (view.telemetry.status == AudioBridgeStatus::Complete) {
                        view.phase = RecordingPhase::Complete; // Retain final monitor block until
                                                               // Stop/close.
                    } else if (!active(view.telemetry.status)) {
                        const auto cause = view.telemetry.status;
                        const auto errors = view.errorSerial;
                        stopEndpoint();
                        view.phase = RecordingPhase::Fault;
                        if (view.errorSerial == errors)
                            error(ErrorCode::Io, "Recording input/clock/capture stopped (status " +
                                                     std::to_string(static_cast<unsigned>(cause)) +
                                                     ")");
                    } else {
                        reconcile();
                        if (std::chrono::steady_clock::now() >= nextInventory) {
                            view.ports = std::make_shared<const std::vector<PipeWirePort>>(
                                endpoint->ports());
                            nextInventory =
                                std::chrono::steady_clock::now() + std::chrono::milliseconds(250);
                        }
                    }
                }
            } catch (const ProjectError &e) {
                error(e.code(), e.what());
                stopEndpoint();
                view.phase = RecordingPhase::Fault;
            } catch (const std::exception &e) {
                error(ErrorCode::Io, e.what());
                stopEndpoint();
                view.phase = RecordingPhase::Fault;
            }
            publish();
            QMutexLocker lock(&mutex);
            if (commands.empty() && !following && !closing.load(std::memory_order_acquire))
                wake.wait(&mutex, 2);
        }
        view.phase = RecordingPhase::Closing;
        publish();
        stopEndpoint();
        view.stopAcknowledged = stopEpoch.load(std::memory_order_acquire);
        view.phase = RecordingPhase::Closed;
        view.closed = true;
        publish();
    }
};
RecordingController::RecordingController(RecordingControllerOptions o)
    : state_(std::make_unique<State>(std::move(o))) {
    state_->start();
}
RecordingController::~RecordingController() {
    requestShutdown();
    state_->wait();
}
Admission RecordingController::submit(RecordingCommand c) {
    QMutexLocker lock(&state_->mutex);
    if (state_->closing.load(std::memory_order_acquire))
        return Admission::Closing;
    if (state_->commands.size() == capacity)
        return Admission::Full;
    state_->commands.push_back({std::move(c), state_->stopEpoch.load(std::memory_order_acquire)});
    state_->wake.wakeOne();
    return Admission::Accepted;
}
bool RecordingController::follow(std::filesystem::path root, std::shared_ptr<const Session> s,
                                 std::uint64_t r) {
    QMutexLocker lock(&state_->mutex);
    if (!s || !r || r <= state_->lastFollowed || state_->closing.load(std::memory_order_acquire))
        return false;
    state_->lastFollowed = r;
    state_->following = Following{std::move(root), std::move(s), r};
    state_->wake.wakeOne();
    return true;
}
std::uint64_t RecordingController::requestStop() noexcept {
    const auto token = state_->stopEpoch.fetch_add(1, std::memory_order_acq_rel) + 1;
    state_->wake.wakeOne();
    return token;
}
bool RecordingController::acknowledgeTake(std::uint64_t s) noexcept {
    QMutexLocker lock(&state_->mutex);
    if (!state_->latest->take || state_->latest->take->sequence != s)
        return false;
    state_->acknowledged.store(s, std::memory_order_release);
    state_->wake.wakeOne();
    return true;
}
void RecordingController::requestShutdown() noexcept {
    state_->closing.store(true, std::memory_order_release);
    state_->wake.wakeOne();
}
std::shared_ptr<const RecordingSnapshot> RecordingController::snapshot() const {
    QMutexLocker lock(&state_->mutex);
    return state_->latest;
}
} // namespace soundcurrent::daw::ui
