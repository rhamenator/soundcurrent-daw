// SPDX-License-Identifier: GPL-3.0-only
#include "recording_controller.hpp"
#include "track_view.hpp"
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
        latest_.firstFault = owner_.firstFault();
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
class NativeDuplexEndpoint : public RecordingEndpoint {
    PipeWireDuplexRecording owner_;
    Id firstTrack_;
    RecordingTelemetry latest_;
    static AudioBridgeStatus legacy(DuplexStatus s) {
        switch (s) {
        case DuplexStatus::Ready:
            return AudioBridgeStatus::Ready;
        case DuplexStatus::Running:
        case DuplexStatus::Underflow:
            return AudioBridgeStatus::Running;
        case DuplexStatus::Complete:
            return AudioBridgeStatus::Complete;
        case DuplexStatus::Stopped:
            return AudioBridgeStatus::Stopped;
        case DuplexStatus::RateChanged:
            return AudioBridgeStatus::RateChanged;
        case DuplexStatus::QuantumExceeded:
            return AudioBridgeStatus::QuantumExceeded;
        case DuplexStatus::ClockDiscontinuity:
            return AudioBridgeStatus::ClockDiscontinuity;
        case DuplexStatus::BufferUnavailable:
            return AudioBridgeStatus::BufferUnavailable;
        case DuplexStatus::DeviceLost:
            return AudioBridgeStatus::DeviceLost;
        case DuplexStatus::CaptureFailed:
            return AudioBridgeStatus::CaptureFailed;
        default:
            return AudioBridgeStatus::ProcessorFailed;
        }
    }

  public:
    explicit NativeDuplexEndpoint(const RecordingPreparation &p)
        : owner_(p.root, *p.session, p.plan, p.lanes, p.duplexOptions),
          firstTrack_(p.spec.trackId) {}
    std::vector<PipeWirePort> ports() override {
        auto result = owner_.ports();
        std::erase_if(result, [&](const auto &p) { return p.nodeId == owner_.nodeId(); });
        return result;
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
    void checkReader() override {
        owner_.run().checkReader();
    }
    Frame preparedEndFrame() override {
        return owner_.run().playbackEnd();
    }
    RecordingResult result() override {
        return laneResult(0);
    }
    RecordingResult laneResult(std::size_t n) override {
        return owner_.run().result(n);
    }
    std::optional<std::filesystem::path> jobDirectory() override {
        return laneJob(0);
    }
    std::optional<std::filesystem::path> laneJob(std::size_t n) override {
        return owner_.run().jobDirectory(n);
    }
    EqEvent event(const Session &s, const ParameterAddress &a) override {
        return mixEvent(s, a).event;
    }
    EqEvent enable(bool enabled) override {
        return mixEnable(firstTrack_, enabled).event;
    }
    MixEvent mixEvent(const Session &s, const ParameterAddress &a) override {
        return owner_.run().graph().parameterEvent(s, a, 0);
    }
    MixEvent mixEnable(const Id &id, bool enabled) override {
        return owner_.run().graph().enableEvent(id, enabled, 0);
    }
    SubmitStatus submit(const EqEvent &e, std::uint64_t r) noexcept override {
        return submitMix({0, e}, r);
    }
    SubmitStatus submitMix(const MixEvent &e, std::uint64_t r) noexcept override {
        const auto s = owner_.run().status();
        if (s != DuplexStatus::Ready && s != DuplexStatus::Running && s != DuplexStatus::Underflow)
            return SubmitStatus::Invalid;
        return owner_.run().graph().submitImmediate(e, r);
    }
    RecordingTelemetry read() override {
        auto &run = owner_.run();
        DuplexObservation o;
        for (unsigned n = 0; n < 64 && run.observation(o); ++n) {
            latest_.outputPeak = o.playback.mix.peak;
            latest_.processed = true;
        }
        latest_.receipt.reset();
        latest_.receipts.clear();
        for (std::size_t n = 0; n < run.graph().plan().tracks.size(); ++n) {
            ImmediateAcknowledgement r;
            for (unsigned k = 0; k < 64 && run.graph().acknowledgement(n, r); ++k)
                latest_.receipts.push_back({n, r});
        }
        latest_.lanes.clear();
        latest_.rejectedFrames = latest_.invalidSamples = 0;
        latest_.capturedFrames = latest_.writtenFrames = std::numeric_limits<Frame>::max();
        for (std::size_t n = 0; n < run.lanes(); ++n) {
            const auto c = run.capture(n);
            latest_.lanes.push_back(c);
            latest_.capturedFrames = std::min(latest_.capturedFrames, c.captured);
            latest_.writtenFrames = std::min(latest_.writtenFrames, c.written);
            latest_.rejectedFrames += c.rejected;
            latest_.invalidSamples += c.invalidSamples;
        }
        latest_.duplexStatus = run.status();
        latest_.status = legacy(latest_.duplexStatus);
        latest_.droppedMeters = run.droppedObservations();
        latest_.droppedReceipts = run.graph().droppedAcknowledgements();
        latest_.callbackFault = run.callbackFault();
        latest_.missingTrackFrames = run.missingTrackFrames();
        return latest_;
    }
};
#endif
const Track *findTrack(const Session &s, const Id &id) {
    const auto i =
        std::find_if(s.tracks.begin(), s.tracks.end(), [&](const auto &t) { return t.id == id; });
    return i == s.tracks.end() ? nullptr : &*i;
}
struct Following {
    std::filesystem::path root;
    std::shared_ptr<const Session> session;
    std::uint64_t revision = 0;
};
// Selected-track route/monitoring intent is separate from the prepared connections.
// Scalar EQ follows the captured track ID; unrelated tracks and recorded clips
// do not alter raw capture. Project/rate/layout/processor identity changes stop it.
bool compatible(const Session &a, const Session &b) {
    if (a.tracks.empty() || b.tracks.empty())
        return false;
    const auto &t = a.tracks.front(), &u = b.tracks.front();
    if (a.id != b.id || a.sampleRate != b.sampleRate || a.playheadFrame != b.playheadFrame ||
        a.punch != b.punch || t.id != u.id || t.layout != u.layout || t.eq.id != u.eq.id ||
        t.inputLatencyFrames != u.inputLatencyFrames || t.eq.bands.size() != u.eq.bands.size())
        return false;
    for (std::size_t n = 0; n < t.eq.bands.size(); ++n)
        if (t.eq.bands[n].id != u.eq.bands[n].id)
            return false;
    return true;
}
bool compatible(const RecordingPreparation &p, const Session &b) {
    if (!p.projectMix)
        return compatible(*p.session, b);
    const auto &a = *p.session;
    if (a.id != b.id || a.sampleRate != b.sampleRate || a.playheadFrame != b.playheadFrame ||
        a.punch != b.punch || a.tracks.size() != b.tracks.size() ||
        bool(a.master) != bool(b.master) ||
        (a.master && (a.master->id != b.master->id || a.master->plan != b.master->plan)))
        return false;
    for (const auto &lane : p.plan.tracks) {
        const auto *t = findTrack(a, lane.track), *u = findTrack(b, lane.track);
        if (!t || !u || t->layout != u->layout || t->clips != u->clips || t->eq.id != u->eq.id ||
            t->eq.bands.size() != u->eq.bands.size())
            return false;
        for (std::size_t n = 0; n < t->eq.bands.size(); ++n)
            if (t->eq.bands[n].id != u->eq.bands[n].id)
                return false;
        for (const auto &clip : t->clips) {
            const auto old = std::find_if(a.assets.begin(), a.assets.end(),
                                          [&](const auto &v) { return v.id == clip.assetId; });
            const auto now = std::find_if(b.assets.begin(), b.assets.end(),
                                          [&](const auto &v) { return v.id == clip.assetId; });
            if (old == a.assets.end() || now == b.assets.end() || *old != *now)
                return false;
        }
    }
    for (const auto &lane : p.lanes) {
        const auto *t = findTrack(b, lane.spec.trackId);
        if (!t || t->layout != lane.spec.capture.layout ||
            t->inputLatencyFrames != lane.spec.inputLatencyFrames)
            return false;
    }
    return true;
}
void checkRecovery(const RecordingCommand &c, const RecordingRecovery &r) {
    if (!c.session || c.session->tracks.empty() || c.job.parent_path() != c.root / "media" ||
        c.job.filename() != utf8Path("capture-" + r.spec.assetId.str()) ||
        c.session->id != r.spec.projectId || c.session->sampleRate != r.spec.capture.sampleRate ||
        std::none_of(c.session->tracks.begin(), c.session->tracks.end(),
                     [&](const auto &t) {
                         return t.id == r.spec.trackId && t.layout == r.spec.capture.layout;
                     }) ||
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
    std::array<std::uint64_t, 256> appliedByLane{};
    struct Bundle {
        Following target;
        std::vector<MixEvent> events;
        std::array<std::uint64_t, 256> required{};
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
        if (!options.duplexFactory) {
#ifdef SC_UI_PIPEWIRE
            options.duplexFactory = [](const auto &p) {
                return std::make_unique<NativeDuplexEndpoint>(p);
            };
#endif
        }
        view.duplexSupported = bool(options.duplexFactory);
        view.supported = bool(options.factory) || view.duplexSupported;
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
        view.backendFaultDiagnostic = false;
        view.errorCode = code;
        view.diagnostic = std::move(detail);
        ++view.errorSerial;
    }
    void update(RecordingTelemetry t) {
        if (t.receipt && t.receipt->generation == view.generation) {
            appliedByLane[0] = std::max(appliedByLane[0], t.receipt->revision);
            view.appliedEventRevision = t.receipt->revision;
            view.appliedFrame = t.receipt->frame;
        }
        for (const auto &r : t.receipts)
            if (r.track < appliedByLane.size() && r.applied.generation == view.generation) {
                appliedByLane[r.track] = std::max(appliedByLane[r.track], r.applied.revision);
                if (r.applied.revision >= view.appliedEventRevision) {
                    view.appliedEventRevision = r.applied.revision;
                    view.appliedFrame = r.applied.frame;
                }
            }
        processed = processed || t.processed;
        view.telemetry = std::move(t);
        if (bundle && bundle->submitted == bundle->events.size() &&
            std::equal(bundle->required.begin(), bundle->required.end(), appliedByLane.begin(),
                       [](auto required, auto applied) { return applied >= required; })) {
            view.appliedRevision = bundle->target.revision;
            bundle.reset();
        } else if (!bundle && prepared && processed && !view.appliedRevision) {
            view.appliedRevision = view.acceptedRevision;
            view.appliedFrame = prepared->spec.capture.startFrame;
        }
    }
    void retain(std::filesystem::path root, RecordingResult r) {
        retain(std::move(root), std::vector<RecordingResult>{std::move(r)});
    }
    void retain(std::filesystem::path root, std::vector<RecordingResult> r) {
        if (view.take || takeSequence == UINT64_MAX)
            throw ProjectError(ErrorCode::InvalidState,
                               "Unacknowledged take or exhausted take sequence");
        if (r.empty() || r.size() > 256)
            throw ProjectError(ErrorCode::InvalidState, "Invalid recorded take group");
        auto receipts = std::make_shared<const std::vector<RecordingResult>>(std::move(r));
        view.take =
            PendingTake{std::move(root), std::make_shared<const RecordingResult>(receipts->front()),
                        ++takeSequence, std::move(receipts)};
    }
    void stopEndpoint(bool preserveDiagnostic = false) {
        if (!endpoint)
            return;
        view.phase = RecordingPhase::Finalizing;
        publish();
        endpoint->stop();
        auto report = [&](ErrorCode code, const char *message) {
            if (!preserveDiagnostic) {
                error(code, message);
                preserveDiagnostic = true;
            }
        };
        try {
            update(endpoint->read());
            endpoint->checkReader();
        } catch (const ProjectError &e) {
            report(e.code(), e.what());
        } catch (const std::exception &e) {
            report(ErrorCode::Io, e.what());
        }
        std::vector<RecordingResult> takes;
        for (std::size_t n = 0; n < view.lanes.size(); ++n) {
            auto &lane = view.lanes[n];
            try {
                lane.job = endpoint->laneJob(n);
                if (lane.job && (!view.job || lane.errorCode))
                    view.job = lane.job;
                if (started)
                    takes.push_back(endpoint->laneResult(n));
            } catch (const ProjectError &e) {
                lane.errorCode = e.code();
                lane.diagnostic = e.what();
                if (lane.job)
                    view.job = lane.job;
                report(e.code(), e.what());
            } catch (const std::exception &e) {
                lane.errorCode = ErrorCode::Io;
                lane.diagnostic = e.what();
                if (lane.job)
                    view.job = lane.job;
                report(ErrorCode::Io, e.what());
            }
        }
        if (!takes.empty()) {
            try {
                retain(prepared->root, std::move(takes));
            } catch (const ProjectError &e) {
                report(e.code(), e.what());
            } catch (const std::exception &e) {
                report(ErrorCode::Io, e.what());
            }
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
            const auto target = prepared->projectMix
                                    ? desired->session
                                    : sessionForTrack(desired->session, prepared->spec.trackId,
                                                      options.projectMemory);
            if (!target)
                throw ProjectError(ErrorCode::InvalidState, "Prepared track no longer exists");
            desired->session = target;
            validate(*desired->session);
            if (desired->root != prepared->root || !compatible(*prepared, *desired->session))
                throw ProjectError(ErrorCode::InvalidState,
                                   "Project structure changed; recording stopped");
            checkedRevision = desired->revision;
        }
        if (view.phase == RecordingPhase::Ready && !admitReady)
            return;
        if (!bundle && desired->revision > view.acceptedRevision) {
            Bundle next;
            next.target = *desired;
            for (const auto &lane : prepared->plan.tracks) {
                const auto &old = findTrack(*acceptedModel, lane.track)->eq;
                const auto &t = *findTrack(*desired->session, lane.track);
                for (std::size_t n = 0; n < t.eq.bands.size(); ++n)
                    if (t.eq.bands[n] != old.bands[n])
                        next.events.push_back(
                            endpoint->mixEvent(*desired->session, {t.id, t.eq.id, t.eq.bands[n].id,
                                                                   BandParameter::GainDb}));
                if (t.eq.enabled != old.enabled)
                    next.events.push_back(endpoint->mixEnable(t.id, t.eq.enabled));
            }
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
            const auto &event = bundle->events[bundle->submitted];
            if (event.track >= prepared->plan.tracks.size())
                throw ProjectError(ErrorCode::InvalidState, "Recording event lane is invalid");
            const auto admitted = endpoint->submitMix(event, revision);
            if (admitted == SubmitStatus::Full)
                return;
            if (admitted != SubmitStatus::Accepted)
                throw ProjectError(ErrorCode::InvalidState,
                                   "Recording rejected a prepared EQ change");
            eventRevision = revision;
            bundle->lastEvent = revision;
            bundle->required[event.track] = revision;
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
            const bool mix = !c.armedTracks.empty();
            if (!(mix ? options.duplexFactory : options.factory) || !c.session ||
                !c.modelRevision || c.session->tracks.empty())
                throw ProjectError(ErrorCode::InvalidState,
                                   "Recording backend/project/track unavailable");
            validate(*c.session);
            if (c.session->punch.enabled && !mix)
                throw ProjectError(ErrorCode::InvalidState,
                                   "Punch recording requires shared project playback");
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
            p.spec.inputLatencyFrames = c.session->tracks.front().inputLatencyFrames;
            p.options = options.nativeOptions;
            p.options.bridge.resources = options.projectMemory;
            p.options.writer.resources = options.projectMemory;
            p.spec.capture.memoryBudgetBytes = options.projectMemory.usage().limitBytes;
            p.options.monitoring = c.monitoring;
            p.options.bridge.generation = view.generation + 1;
            p.spec.capture.maximumCallbackFrames = p.options.bridge.maximumFrames;
            p.projectMix = mix;
            if (mix) {
                if (c.armedTracks.size() > 256 || c.recordFrames <= 0 ||
                    c.recordFrames > Frame(c.session->sampleRate) * 86400 ||
                    c.session->playheadFrame > std::numeric_limits<Frame>::max() - c.recordFrames)
                    throw ProjectError(
                        ErrorCode::InvalidState,
                        "Choose a recording range of up to 24 hours and 1..256 armed tracks");
                std::vector<Id> ids;
                for (const auto &t : c.session->tracks)
                    ids.push_back(t.id);
                p.plan =
                    c.plan ? *c.plan
                           : (c.session->master
                                  ? c.session->master->plan
                                  : identityMix(*c.session, ids, c.session->tracks.front().layout));
                p.duplexOptions.audit = p.options.audit;
                p.duplexOptions.auditClock = options.duplexAuditClock;
                auto &cfg = p.duplexOptions.run;
                cfg.playback.graph.resources = options.projectMemory;
                cfg.reader.resources = options.projectMemory;
                cfg.memoryBudgetBytes = options.projectMemory.usage().limitBytes;
                cfg.playback.graph.memoryBudgetBytes = cfg.memoryBudgetBytes;
                cfg.nativeInputs = 0;
                cfg.playback.graph.startFrame = c.session->playheadFrame;
                cfg.playback.graph.maximumFrames = p.options.bridge.maximumFrames;
                cfg.playback.graph.generation = p.options.bridge.generation;
                cfg.playback.endFrame = c.session->playheadFrame + c.recordFrames;
                if (c.session->punch.enabled) {
                    cfg.musicalPunch =
                        PunchRange{c.session->punch.startFrame, c.session->punch.endFrame};
                    if (cfg.musicalPunch->begin < cfg.playback.graph.startFrame ||
                        cfg.musicalPunch->end > cfg.playback.endFrame)
                        throw ProjectError(ErrorCode::InvalidState,
                                           "Punch locators must fit the prepared recording range");
                }
                for (const auto &id : c.armedTracks) {
                    const auto *t = findTrack(*c.session, id);
                    if (!t ||
                        std::any_of(p.lanes.begin(), p.lanes.end(),
                                    [&](const auto &l) { return l.spec.trackId == id; }) ||
                        t->layout.channels > 256 - cfg.nativeInputs)
                        throw ProjectError(
                            ErrorCode::InvalidState,
                            "Invalid/duplicate armed track or more than 256 packed channels");
                    DuplexRecordingLane lane;
                    lane.spec.projectId = c.session->id;
                    lane.spec.trackId = id;
                    lane.spec.capture.memoryBudgetBytes = cfg.memoryBudgetBytes;
                    lane.spec.capture.sampleRate = c.session->sampleRate;
                    lane.spec.capture.layout = t->layout;
                    lane.spec.capture.startFrame = c.session->playheadFrame;
                    lane.spec.inputLatencyFrames = t->inputLatencyFrames;
                    lane.spec.capture.maximumCallbackFrames = p.options.bridge.maximumFrames;
                    lane.spec.capture =
                        withCaptureReserve(lane.spec.capture, c.storageReserveMilliseconds);
                    lane.monitoring = t->monitoring;
                    lane.writer = p.options.writer;
                    for (std::uint32_t ch = 0; ch < t->layout.channels; ++ch)
                        lane.inputChannels.push_back(cfg.nativeInputs++);
                    p.lanes.push_back(std::move(lane));
                }
                p.spec = p.lanes.front().spec;
            } else {
                p.spec.capture = withCaptureReserve(p.spec.capture, c.storageReserveMilliseconds);
                p.plan =
                    identityMix(*c.session, std::span(&p.spec.trackId, 1), p.spec.capture.layout);
            }
            acceptedModel = c.session;
            eventRevision = checkedRevision = 0;
            processed = false;
            appliedByLane.fill(0);
            bundle.reset();
            view.telemetry = {};
            view.preview.reset();
            view.job.reset();
            view.generation = p.options.bridge.generation;
            view.projectMix = mix;
            view.channels = mix ? p.duplexOptions.run.nativeInputs : p.spec.capture.layout.channels;
            view.outputChannels = mix ? p.plan.output.channels
                                      : (c.monitoring != RecordingMonitor::Off ? view.channels : 0);
            view.endFrame = mix ? p.duplexOptions.run.playback.endFrame : 0;
            view.lanes.clear();
            if (mix) {
                for (const auto &lane : p.lanes)
                    view.lanes.push_back({lane.spec.trackId,
                                          lane.spec.capture.layout,
                                          lane.monitoring,
                                          lane.inputChannels.front(),
                                          {},
                                          {},
                                          {}});
            } else
                view.lanes.push_back(
                    {p.spec.trackId, p.spec.capture.layout, c.monitoring, 0, {}, {}, {}});
            view.sampleRate = p.spec.capture.sampleRate;
            view.monitoring = c.monitoring;
            view.desiredRevision = view.acceptedRevision = c.modelRevision;
            view.appliedRevision = view.appliedEventRevision = view.appliedFrame = 0;
            prepared = p;
            endpoint = (mix ? options.duplexFactory : options.factory)(p);
            if (!endpoint)
                throw ProjectError(ErrorCode::InvalidState, "Recording factory returned no owner");
            if (mix) {
                const auto end = endpoint->preparedEndFrame();
                if ((c.session->punch.enabled && end <= 0) ||
                    (end && (end < p.duplexOptions.run.playback.endFrame ||
                             end - p.duplexOptions.run.playback.endFrame >
                                 Frame(c.session->sampleRate) * 60)))
                    throw ProjectError(ErrorCode::InvalidState,
                                       "Endpoint reported invalid prepared recording bounds");
                if (end)
                    view.endFrame = end;
            }
            if (interrupted(q.epoch)) {
                stopEndpoint();
                return;
            }
            view.ports = std::make_shared<const std::vector<PipeWirePort>>(endpoint->ports());
            nextInventory = std::chrono::steady_clock::now() + std::chrono::milliseconds(250);
            view.phase = RecordingPhase::Ready;
        } else if (c.kind == RecordingCommandKind::Start) {
            if (!c.armed || !endpoint || view.phase != RecordingPhase::Ready ||
                c.inputs.size() != view.channels || c.outputs.size() != view.outputChannels)
                throw ProjectError(ErrorCode::InvalidState,
                                   "Prepare, arm and select every required recording channel");
            reconcile(true);
            endpoint->connectInputs(c.inputs);
            if (view.outputChannels)
                endpoint->connectOutputs(c.outputs);
            if (interrupted(q.epoch))
                return;
            started = true;
            endpoint->activate();
            view.job = endpoint->jobDirectory();
            for (std::size_t n = 0; n < view.lanes.size(); ++n)
                view.lanes[n].job = endpoint->laneJob(n);
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
            const auto cancellation = [&] {
                if (interrupted(q.epoch))
                    throw ProjectError(ErrorCode::Canceled, "Recording recovery canceled");
            };
            const auto inspected = inspectRecording(c.job, cancellation, true);
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
                auto recovered = recoverRecording(c.root, c.job, cancellation);
                view.job = c.root / "media" / ("capture-" + recovered.spec.assetId.str());
                const auto copied = inspectRecording(*view.job, cancellation, true);
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
                    if (e.code() == ErrorCode::Canceled && interrupted(q->epoch))
                        continue;
                    error(e.code(), e.what());
                    if (!wasStarted) {
                        stopEndpoint(true);
                        view.phase = RecordingPhase::Fault;
                    }
                } catch (const std::exception &e) {
                    error(ErrorCode::Io, e.what());
                    if (!wasStarted) {
                        stopEndpoint(true);
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
                        if (view.errorSerial == errors) {
                            error(ErrorCode::Io, "Recording input/clock/capture stopped (status " +
                                                     std::to_string(static_cast<unsigned>(cause)) +
                                                     ")");
                            view.backendFaultDiagnostic = true;
                        }
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
                stopEndpoint(true);
                view.phase = RecordingPhase::Fault;
            } catch (const std::exception &e) {
                error(ErrorCode::Io, e.what());
                stopEndpoint(true);
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
