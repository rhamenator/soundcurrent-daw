// SPDX-License-Identifier: GPL-3.0-only
#include "playback_controller.hpp"
#include "track_view.hpp"
#include <QMutex>
#include <QThread>
#include <QWaitCondition>
#include <algorithm>
#include <deque>
#include <limits>
#ifdef SC_UI_PIPEWIRE
#include <soundcurrent/pipewire_playback.hpp>
#endif

namespace soundcurrent::daw::ui {
namespace {
constexpr std::size_t capacity = 16;
#ifdef SC_UI_PIPEWIRE
class NativeEndpoint : public PlaybackEndpoint {
    PipeWirePlayback owner_;
    PlaybackTelemetry latest_;

  public:
    explicit NativeEndpoint(const PlaybackPreparation &p, PlaybackCallbackInstrumentation audit)
        : owner_(p.root, *p.session, p.plan,
                 {{p.config.maximumCallbackFrames, p.config.startFrame, p.config.generation,
                   p.config.memoryBudgetBytes},
                  p.config.endFrame,
                  p.config.slabFrames},
                 {}, std::chrono::seconds(3), audit) {}
    std::vector<PipeWirePort> ports() override {
        auto ports = owner_.ports();
        std::erase_if(ports, [](const auto &p) { return !p.input; });
        return ports;
    }
    void connect(const std::vector<PipeWirePort> &p) override {
        owner_.connectOutputs(p);
    }
    void activate() override {
        owner_.activate();
    }
    void stop() noexcept override {
        owner_.stop();
    }
    void checkReader() override {
        owner_.checkReader();
    }
    MixEvent event(const Session &s, const ParameterAddress &a) override {
        return owner_.graph().parameterEvent(s, a, 0);
    }
    MixEvent enable(const Id &id, bool value) override {
        return owner_.graph().enableEvent(id, value, 0);
    }
    SubmitStatus submit(const MixEvent &e, std::uint64_t revision) noexcept override {
        return owner_.submitImmediate(e, revision);
    }
    PlaybackTelemetry read() override {
        PlaybackObservation o;
        for (std::size_t n = 0; n < 64 && owner_.observation(o); ++n) {
            latest_.peak = o.playback.peak;
            latest_.processed = true;
        }
        ImmediateAcknowledgement receipt;
        latest_.receipts.clear();
        for (std::size_t t = 0; t < owner_.graph().plan().tracks.size(); ++t) {
            std::optional<ImmediateAcknowledgement> last;
            for (std::size_t n = 0; n < 64 && owner_.acknowledgement(t, receipt); ++n)
                last = receipt;
            if (last)
                latest_.receipts.push_back({t, *last});
        }
        latest_.status = owner_.status();
        latest_.position = owner_.position();
        latest_.missingFrames = owner_.missingFrames();
        latest_.droppedMeters = owner_.droppedObservations();
        latest_.droppedReceipts = owner_.droppedAcknowledgements();
        return latest_;
    }
};
#endif
bool running(PlaybackBridgeStatus s) {
    return s == PlaybackBridgeStatus::Ready || s == PlaybackBridgeStatus::Running ||
           s == PlaybackBridgeStatus::Underflow;
}
struct Following {
    std::filesystem::path root;
    std::shared_ptr<const Session> session;
    std::uint64_t revision = 0;
};
const Track *findTrack(const Session &s, const Id &id) {
    const auto it =
        std::find_if(s.tracks.begin(), s.tracks.end(), [&](const auto &t) { return t.id == id; });
    return it == s.tracks.end() ? nullptr : &*it;
}
bool compatible(const PlaybackPreparation &p, const Session &updated) {
    const auto &prepared = *p.session;
    if (prepared.id != updated.id || prepared.sampleRate != updated.sampleRate ||
        prepared.playheadFrame != updated.playheadFrame ||
        (p.projectMix && prepared.tracks.size() != updated.tracks.size()))
        return false;
    for (const auto &lane : p.plan.tracks) {
        const auto *t = findTrack(prepared, lane.track), *u = findTrack(updated, lane.track);
        if (!t || !u || t->layout != u->layout || t->clips != u->clips || t->eq.id != u->eq.id ||
            t->eq.bands.size() != u->eq.bands.size())
            return false;
        for (std::size_t n = 0; n < t->eq.bands.size(); ++n)
            if (t->eq.bands[n].id != u->eq.bands[n].id)
                return false;
        for (const auto &clip : t->clips) {
            const auto old = std::find_if(prepared.assets.begin(), prepared.assets.end(),
                                          [&](const auto &a) { return a.id == clip.assetId; });
            const auto now = std::find_if(updated.assets.begin(), updated.assets.end(),
                                          [&](const auto &a) { return a.id == clip.assetId; });
            if (old == prepared.assets.end() || now == updated.assets.end() || *old != *now)
                return false;
        }
    }
    return true;
}
} // namespace
struct PlaybackController::State : QThread {
    struct Queued {
        PlaybackCommand command;
        std::uint64_t epoch;
    };
    mutable QMutex mutex;
    QWaitCondition wake;
    std::deque<Queued> commands;
    std::optional<Following> following;
    std::uint64_t lastFollowed = 0;
    std::atomic<std::uint64_t> stopEpoch{0};
    std::atomic<bool> closing{false};
    std::shared_ptr<const PlaybackSnapshot> latest;
    PlaybackSnapshot view;
    PlaybackControllerOptions options;
    std::unique_ptr<PlaybackEndpoint> endpoint;
    std::optional<PlaybackPreparation> prepared;
    std::optional<Following> desired;
    std::shared_ptr<const Session> acceptedModel;
    std::uint64_t seenStop = 0, eventRevision = 0, checkedRevision = 0;
    bool processed = false;
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
    explicit State(PlaybackControllerOptions o) : options(std::move(o)) {
#ifdef SC_UI_PIPEWIRE
        if (!options.factory)
            options.factory = [audit = options.nativeAudit](const auto &p) {
                return std::make_unique<NativeEndpoint>(p, audit);
            };
#endif
        view.supported = bool(options.factory);
        view.phase = view.supported ? PlaybackPhase::Idle : PlaybackPhase::Unsupported;
        latest = std::make_shared<const PlaybackSnapshot>(view);
    }
    void publish() {
        view.pending =
            bundle || (view.acceptedRevision && view.desiredRevision != view.appliedRevision);
        auto next = std::make_shared<const PlaybackSnapshot>(view);
        QMutexLocker lock(&mutex);
        latest = std::move(next);
    }
    void error(ErrorCode code, std::string diagnostic) {
        view.errorCode = code;
        view.diagnostic = std::move(diagnostic);
        ++view.errorSerial;
    }
    void stopEndpoint() {
        if (!endpoint)
            return;
        endpoint->stop();
        try {
            update(endpoint->read());
            endpoint->checkReader();
        } catch (const ProjectError &e) {
            error(e.code(), e.what());
        } catch (const std::exception &e) {
            error(ErrorCode::Io, e.what());
        }
        endpoint.reset();
        bundle.reset();
        prepared.reset();
        acceptedModel.reset();
        view.ports.reset();
        view.peak = 0;
    }
    void update(const PlaybackTelemetry &t) {
        view.nativeStatus = t.status;
        view.position = t.position;
        view.peak = t.peak;
        view.missingFrames = t.missingFrames;
        view.droppedMeters = t.droppedMeters;
        view.droppedReceipts = t.droppedReceipts;
        processed = processed || t.processed;
        for (const auto &receipt : t.receipts)
            if (receipt.track < appliedByLane.size() &&
                receipt.applied.generation == view.generation) {
                appliedByLane[receipt.track] =
                    std::max(appliedByLane[receipt.track], receipt.applied.revision);
                if (receipt.applied.revision >= view.appliedEventRevision) {
                    view.appliedEventRevision = receipt.applied.revision;
                    view.appliedFrame = receipt.applied.frame;
                }
            }
        if (bundle && bundle->submitted == bundle->events.size() &&
            std::equal(bundle->required.begin(), bundle->required.end(), appliedByLane.begin(),
                       [](auto required, auto applied) { return applied >= required; })) {
            view.appliedRevision = bundle->target.revision;
            bundle.reset();
        } else if (!bundle && prepared && processed && !view.appliedRevision) {
            view.appliedRevision = view.acceptedRevision;
            view.appliedFrame = prepared->config.startFrame;
        }
    }
    void reconcile(bool admitReady = false) {
        if (!prepared || !desired || !endpoint)
            return;
        if (desired->revision < prepared->modelRevision)
            return;
        view.desiredRevision = desired->revision;
        if (desired->revision != checkedRevision) {
            const auto target = prepared->projectMix
                                    ? desired->session
                                    : sessionForTrack(desired->session, prepared->track);
            if (!target)
                throw ProjectError(ErrorCode::InvalidState, "Prepared track no longer exists");
            desired->session = target;
            validate(*desired->session);
            if (desired->root != prepared->root || !compatible(*prepared, *desired->session))
                throw ProjectError(ErrorCode::InvalidState,
                                   "Project structure changed; prepare playback again");
            checkedRevision = desired->revision;
        }
        if (view.phase == PlaybackPhase::Ready && !admitReady)
            return; // Retain only the latest silent edit, not an audible queued undo history.
        if (!bundle && desired->revision > view.acceptedRevision) {
            Bundle next;
            next.target = *desired;
            for (const auto &lane : prepared->plan.tracks) {
                const auto &oldEq = findTrack(*acceptedModel, lane.track)->eq;
                const auto &track = *findTrack(*desired->session, lane.track);
                for (std::size_t n = 0; n < track.eq.bands.size(); ++n)
                    if (track.eq.bands[n] != oldEq.bands[n])
                        next.events.push_back(endpoint->event(
                            *desired->session,
                            {track.id, track.eq.id, track.eq.bands[n].id, BandParameter::GainDb}));
                if (track.eq.enabled != oldEq.enabled)
                    next.events.push_back(endpoint->enable(track.id, track.eq.enabled));
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
                throw ProjectError(ErrorCode::InvalidState, "Playback event revision exhausted");
            if (bundle->events[bundle->submitted].track >= prepared->plan.tracks.size())
                throw ProjectError(ErrorCode::InvalidState, "Playback parameter lane is invalid");
            const auto revision = eventRevision + 1;
            const auto status = endpoint->submit(bundle->events[bundle->submitted], revision);
            if (status == SubmitStatus::Full)
                return; // Retain exact prepared suffix for retry.
            if (status != SubmitStatus::Accepted)
                throw ProjectError(ErrorCode::InvalidState,
                                   "Playback did not admit a prepared parameter change");
            eventRevision = revision;
            bundle->lastEvent = revision;
            bundle->required[bundle->events[bundle->submitted].track] = revision;
            ++bundle->submitted;
        }
        view.acceptedRevision = bundle->target.revision;
        acceptedModel = bundle->target.session;
    }
    void prepare(PlaybackCommand &c, std::uint64_t epoch) {
        if (!options.factory)
            throw ProjectError(ErrorCode::InvalidState,
                               "Native playback is unavailable in this build");
        if (!c.session || !c.modelRevision)
            throw ProjectError(ErrorCode::InvalidState, "Playback project/revision missing");
        validate(*c.session);
        if (c.session->tracks.empty())
            throw ProjectError(ErrorCode::InvalidState, "Project has no audio track");
        if (endpoint) {
            view.phase = PlaybackPhase::Stopping;
            publish();
            stopEndpoint();
        }
        view.phase = PlaybackPhase::Preparing;
        publish();
        if (options.beforePrepare)
            options.beforePrepare();
        if (closing.load(std::memory_order_acquire) ||
            stopEpoch.load(std::memory_order_acquire) != epoch)
            return;
        if (view.generation == UINT64_MAX)
            throw ProjectError(ErrorCode::InvalidState, "Playback generation exhausted");
        PlaybackPreparation next{c.root, c.session, c.modelRevision, c.session->tracks.front().id,
                                 {},     {},        bool(c.plan)};
        next.config.sampleRate = c.session->sampleRate;
        next.plan = c.plan ? *c.plan
                           : identityMix(*c.session, std::span(&next.track, 1),
                                         c.session->tracks.front().layout);
        next.config.layout = next.plan.output;
        next.config.generation = view.generation + 1;
        next.config.startFrame = c.session->playheadFrame;
        mixPayloadBytes(*c.session, next.plan,
                        {next.config.maximumCallbackFrames, next.config.startFrame,
                         next.config.generation, next.config.memoryBudgetBytes});
        for (const auto &lane : next.plan.tracks)
            for (const auto &clip : findTrack(*c.session, lane.track)->clips)
                next.config.endFrame =
                    std::max(next.config.endFrame, clip.startFrame + clip.lengthFrames);
        if (next.config.endFrame <= next.config.startFrame)
            throw ProjectError(ErrorCode::InvalidState,
                               "Prepared track has no audio beyond the playhead");
        auto candidate = options.factory(next);
        if (!candidate)
            throw ProjectError(ErrorCode::InvalidState, "Playback factory returned no owner");
        endpoint = std::move(candidate);
        if (closing.load(std::memory_order_acquire) ||
            stopEpoch.load(std::memory_order_acquire) != epoch) {
            stopEndpoint();
            return;
        }
        prepared = std::move(next);
        acceptedModel = prepared->session;
        bundle.reset();
        eventRevision = 0;
        checkedRevision = 0;
        processed = false;
        appliedByLane.fill(0);
        view.tracks = static_cast<std::uint32_t>(prepared->plan.tracks.size());
        view.projectMix = prepared->projectMix;
        view.channels = prepared->config.layout.channels;
        view.sampleRate = prepared->config.sampleRate;
        view.generation = prepared->config.generation;
        view.desiredRevision = view.acceptedRevision = c.modelRevision;
        view.appliedRevision = view.appliedEventRevision = 0;
        view.appliedFrame = 0;
        view.missingFrames = view.droppedMeters = view.droppedReceipts = 0;
        view.peak = 0;
        view.position = prepared->config.startFrame;
        view.ports = std::make_shared<const std::vector<PipeWirePort>>(endpoint->ports());
        nextInventory = std::chrono::steady_clock::now() + std::chrono::milliseconds(250);
        view.nativeStatus = PlaybackBridgeStatus::Ready;
        view.phase = PlaybackPhase::Ready;
    }
    void execute(Queued &q) {
        if (q.command.kind == PlaybackCommandKind::Prepare)
            prepare(q.command, q.epoch);
        else {
            if (view.phase != PlaybackPhase::Ready || !endpoint)
                throw ProjectError(ErrorCode::InvalidState,
                                   "Prepare playback before choosing outputs");
            if (q.command.outputs.size() != view.channels)
                throw ProjectError(ErrorCode::InvalidState, "Select every output channel");
            reconcile(true);
            endpoint->connect(q.command.outputs);
            if (closing.load(std::memory_order_acquire) ||
                stopEpoch.load(std::memory_order_acquire) != q.epoch)
                return;
            endpoint->activate();
            view.phase = PlaybackPhase::Playing;
        }
        ++view.completedCommands;
    }
    void run() override {
        while (!closing.load(std::memory_order_acquire)) {
            const auto epoch = stopEpoch.load(std::memory_order_acquire);
            if (epoch != seenStop) {
                seenStop = epoch;
                view.phase = PlaybackPhase::Stopping;
                publish();
                stopEndpoint();
                view.phase = view.supported ? PlaybackPhase::Idle : PlaybackPhase::Unsupported;
            }
            std::optional<Queued> command;
            {
                QMutexLocker lock(&mutex);
                if (following) {
                    desired = std::move(following);
                    following.reset();
                }
                if (!commands.empty()) {
                    command = std::move(commands.front());
                    commands.pop_front();
                }
            }
            try {
                if (command && command->epoch == stopEpoch.load(std::memory_order_acquire) &&
                    !closing.load(std::memory_order_acquire))
                    execute(*command);
                if (endpoint) {
                    update(endpoint->read());
                    if (view.nativeStatus == PlaybackBridgeStatus::Complete &&
                        !view.missingFrames) {
                        // Keep the terminal, silent native graph until Stop/reprepare/close.
                        // Destroying links here can outrun downstream delivery of the final block.
                        view.phase = PlaybackPhase::Complete;
                        view.peak = 0;
                    } else if (!running(view.nativeStatus)) {
                        view.phase = PlaybackPhase::Stopping;
                        publish();
                        stopEndpoint();
                        view.phase = PlaybackPhase::Fault;
                        error(ErrorCode::Io, "Playback stopped or contains missing frames");
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
                view.phase = PlaybackPhase::Stopping;
                publish();
                stopEndpoint();
                view.phase = PlaybackPhase::Fault;
            } catch (const std::exception &e) {
                error(ErrorCode::Io, e.what());
                view.phase = PlaybackPhase::Stopping;
                publish();
                stopEndpoint();
                view.phase = PlaybackPhase::Fault;
            }
            publish();
            QMutexLocker lock(&mutex);
            if (commands.empty() && !following && !closing.load(std::memory_order_acquire))
                wake.wait(&mutex, 2);
        }
        view.phase = PlaybackPhase::Closing;
        publish();
        stopEndpoint();
        view.phase = PlaybackPhase::Closed;
        view.closed = true;
        publish();
    }
};
PlaybackController::PlaybackController(PlaybackControllerOptions o)
    : state_(std::make_unique<State>(std::move(o))) {
    state_->start();
}
PlaybackController::~PlaybackController() {
    requestShutdown();
    state_->wait();
}
Admission PlaybackController::submit(PlaybackCommand c) {
    QMutexLocker lock(&state_->mutex);
    if (state_->closing.load(std::memory_order_acquire))
        return Admission::Closing;
    if (state_->commands.size() == capacity)
        return Admission::Full;
    state_->commands.push_back({std::move(c), state_->stopEpoch.load(std::memory_order_acquire)});
    state_->wake.wakeOne();
    return Admission::Accepted;
}
bool PlaybackController::follow(std::filesystem::path root, std::shared_ptr<const Session> session,
                                std::uint64_t revision) {
    QMutexLocker lock(&state_->mutex);
    if (!session || !revision || revision <= state_->lastFollowed ||
        state_->closing.load(std::memory_order_acquire))
        return false;
    state_->lastFollowed = revision;
    state_->following = Following{std::move(root), std::move(session), revision};
    state_->wake.wakeOne();
    return true;
}
void PlaybackController::requestStop() noexcept {
    state_->stopEpoch.fetch_add(1, std::memory_order_acq_rel);
    state_->wake.wakeOne();
}
void PlaybackController::requestShutdown() noexcept {
    state_->closing.store(true, std::memory_order_release);
    state_->wake.wakeOne();
}
std::shared_ptr<const PlaybackSnapshot> PlaybackController::snapshot() const {
    QMutexLocker lock(&state_->mutex);
    return state_->latest;
}
} // namespace soundcurrent::daw::ui
