// SPDX-License-Identifier: GPL-3.0-only
#include "manual_recording_controller.hpp"
#include <algorithm>
#include <condition_variable>
#include <deque>
#include <mutex>
#include <thread>

namespace soundcurrent::daw::ui {
namespace {
constexpr std::size_t controlQueueCapacity = 16;
const Track *findTrack(const Session &s, const Id &id) {
    const auto found =
        std::find_if(s.tracks.begin(), s.tracks.end(), [&](const auto &t) { return t.id == id; });
    return found == s.tracks.end() ? nullptr : &*found;
}
void require(bool condition, const char *message) {
    if (!condition)
        throw ProjectError(ErrorCode::InvalidState, message);
}
bool terminal(DuplexStatus status) {
    return status != DuplexStatus::Ready && status != DuplexStatus::Running &&
           status != DuplexStatus::Underflow;
}
#ifdef SC_MANUAL_CONTROL_PIPEWIRE
class NativeEndpoint final : public ManualControlEndpoint {
    PipeWireManualRecording run_;

  public:
    explicit NativeEndpoint(const ManualControlPreparation &p)
        : run_(p.root, *p.session, p.plan, p.arms, p.options) {}
    std::vector<PipeWirePort> ports() override {
        return run_.ports();
    }
    void activate(const std::vector<PipeWirePort> &inputs,
                  const std::vector<PipeWirePort> &outputs) override {
        run_.connectInputs(inputs);
        run_.connectOutputs(outputs);
        run_.activate();
    }
    std::uint64_t prepareTake() override {
        return run_.prepareTake();
    }
    void abandonTake(std::uint64_t take) override {
        run_.abandonTake(take);
    }
    ManualPunchSubmit submit(ManualPunchCommand command) noexcept override {
        return run_.submit(command);
    }
    void service() override {
        run_.service();
    }
    bool acknowledgement(ManualPunchReceipt &r) noexcept override {
        return run_.acknowledgement(r);
    }
    bool takeGroup(ManualRecordedGroup &g) override {
        return run_.takeGroup(g);
    }
    void stop(bool cancel) override {
        cancel ? run_.cancel() : run_.stop();
    }
    void checkError() override {
        run_.checkError();
    }
    void checkReader() override {
        run_.checkReader();
    }
    DuplexStatus status() noexcept override {
        return run_.status();
    }
    Frame position() noexcept override {
        return run_.position();
    }
    std::size_t occupiedSlots() noexcept override {
        return run_.occupiedSlots();
    }
    MixEvent parameterEvent(const Session &s, const ParameterAddress &a) override {
        return run_.graph().parameterEvent(s, a, 0);
    }
    MixEvent enableEvent(const Id &track, bool enabled) override {
        return run_.graph().enableEvent(track, enabled, 0);
    }
    SubmitStatus submitParameter(const MixEvent &event, std::uint64_t revision) noexcept override {
        return run_.graph().submitImmediate(event, revision);
    }
    bool parameterAcknowledgement(std::size_t track,
                                  ImmediateAcknowledgement &r) noexcept override {
        return run_.graph().acknowledgement(track, r);
    }
};
#endif
} // namespace
struct ManualRecordingController::State {
    struct Queued {
        ManualControlCommand command;
        std::uint64_t sequence, epoch;
    };
    mutable std::mutex mutex;
    std::condition_variable wake;
    std::deque<Queued> queue;
    std::vector<std::uint64_t> commandAcks, punchAcks;
    std::vector<std::pair<std::uint64_t, std::uint64_t>> groupAcks;
    std::shared_ptr<ManualRecordingInterrupt> interrupt; // Accessed under mutex outside RT.
    std::shared_ptr<const ManualControlSnapshot> latest;
    std::uint64_t sequence = 0, stopEpoch = 0;
    std::size_t commandCredits = 0;
    bool closing = false;
    ManualControlSnapshot view;
    ManualControlOptions options;
    std::unique_ptr<ManualControlEndpoint> endpoint;
    struct Following {
        std::uint64_t generation, revision;
        std::filesystem::path root;
        std::shared_ptr<const Session> session;
    };
    std::optional<Following> following, desired;
    std::optional<ManualControlPreparation> prepared;
    std::shared_ptr<const Session> acceptedModel;
    struct Bundle {
        Following target;
        std::vector<MixEvent> events;
        std::vector<std::uint64_t> required;
        std::size_t submitted = 0;
    };
    std::optional<Bundle> bundle;
    std::vector<std::uint64_t> appliedParameters;
    std::uint64_t parameterRevision = 0, lastFollowed = 0, lastFollowingGeneration = 0,
                  checkedRevision = 0;
    std::chrono::steady_clock::time_point nextInventory;
    std::filesystem::path root;
    std::uint64_t seenStop = 0;
    std::size_t punchInFlight = 0;
    std::thread worker;
    explicit State(ManualControlOptions o) : options(std::move(o)) {
        view.commands.reserve(manualPunchCommands);
        view.punches.reserve(manualPunchCommands);
        view.groups.reserve(manualPunchSlots);
        commandAcks.reserve(manualPunchCommands);
        punchAcks.reserve(manualPunchCommands);
        groupAcks.reserve(manualPunchSlots);
#ifdef SC_MANUAL_CONTROL_PIPEWIRE
        if (!options.factory)
            options.factory = [](const auto &p) { return std::make_unique<NativeEndpoint>(p); };
#endif
        view.supported = bool(options.factory);
        view.phase = view.supported ? ManualControlPhase::Idle : ManualControlPhase::Unsupported;
        latest = std::make_shared<const ManualControlSnapshot>(view);
    }
    // Worker only, with mutex held. Final Close consumes the last GUI prefix
    // under the same lock that publishes closed, so no accepted ack is stranded.
    void consumeAcknowledgements() {
        std::erase_if(view.commands, [&](const auto &r) {
            if (std::find(commandAcks.begin(), commandAcks.end(), r.sequence) == commandAcks.end())
                return false;
            --commandCredits;
            return true;
        });
        std::erase_if(view.punches, [&](const auto &r) {
            return std::find(punchAcks.begin(), punchAcks.end(), r.command.revision) !=
                   punchAcks.end();
        });
        std::erase_if(view.groups, [&](const auto &g) {
            return std::find(groupAcks.begin(), groupAcks.end(),
                             std::pair{g.generation, g.group->take}) != groupAcks.end();
        });
    }
    void publish() {
        view.parametersPending =
            endpoint && (bundle || view.desiredRevision != view.appliedRevision);
        std::shared_ptr<const ManualControlSnapshot> copy;
        if (!view.closed)
            copy = std::make_shared<const ManualControlSnapshot>(view);
        std::lock_guard lock(mutex);
        if (view.closed) {
            consumeAcknowledgements();
            copy = std::make_shared<const ManualControlSnapshot>(view);
        }
        // Keep acknowledgement tombstones until the visible snapshot changes,
        // so a slow endpoint call cannot make an old receipt consumable twice.
        std::erase_if(commandAcks, [&](auto id) {
            return std::none_of(copy->commands.begin(), copy->commands.end(),
                                [&](const auto &r) { return r.sequence == id; });
        });
        std::erase_if(punchAcks, [&](auto id) {
            return std::none_of(copy->punches.begin(), copy->punches.end(),
                                [&](const auto &r) { return r.command.revision == id; });
        });
        std::erase_if(groupAcks, [&](const auto &id) {
            return std::none_of(copy->groups.begin(), copy->groups.end(), [&](const auto &g) {
                return std::pair{g.generation, g.group->take} == id;
            });
        });
        latest = std::move(copy);
    }
    void error(ErrorCode code, std::string detail) {
        if (!view.fault) {
            view.error = code;
            view.diagnostic = std::move(detail);
            ++view.errorSerial;
        }
        view.fault = true;
    }
    void reportException() {
        try {
            throw;
        } catch (const ProjectError &e) {
            error(e.code(), e.what());
        } catch (const std::exception &e) {
            error(ErrorCode::Io, e.what());
        } catch (...) {
            error(ErrorCode::Io, "Unknown manual recording error");
        }
    }
    void collect() {
        if (!endpoint)
            return;
        view.status = endpoint->status();
        view.position = endpoint->position();
        view.occupiedSlots = endpoint->occupiedSlots();
        for (std::size_t n = 0; n < appliedParameters.size(); ++n) {
            ImmediateAcknowledgement r;
            for (unsigned k = 0; k < 64 && endpoint->parameterAcknowledgement(n, r); ++k)
                if (r.generation == view.generation)
                    appliedParameters[n] = std::max(appliedParameters[n], r.revision);
        }
        if (bundle && bundle->submitted == bundle->events.size() &&
            std::equal(bundle->required.begin(), bundle->required.end(), appliedParameters.begin(),
                       [](auto required, auto applied) { return applied >= required; })) {
            view.appliedRevision = bundle->target.revision;
            bundle.reset();
        } else if (!bundle && prepared &&
                   view.position > prepared->options.run.playback.graph.startFrame &&
                   !view.appliedRevision)
            view.appliedRevision = view.acceptedRevision;
        ManualPunchReceipt receipt;
        while (view.punches.size() < manualPunchCommands && endpoint->acknowledgement(receipt)) {
            require(punchInFlight != 0, "Unexpected manual punch acknowledgement");
            --punchInFlight;
            view.punches.push_back(receipt);
        }
        ManualRecordedGroup group;
        while (view.groups.size() < manualPunchSlots && endpoint->takeGroup(group))
            view.groups.push_back({view.generation, root,
                                   std::make_shared<const ManualRecordedGroup>(std::move(group))});
        view.occupiedSlots = endpoint->occupiedSlots();
    }
    void reconcile() {
        if (!endpoint || !prepared || !desired || desired->generation != view.generation ||
            desired->revision < prepared->modelRevision)
            return;
        view.desiredRevision = desired->revision;
        if (checkedRevision != desired->revision) {
            require(desired->root == prepared->root &&
                        desired->session->id == prepared->session->id &&
                        desired->session->sampleRate == prepared->session->sampleRate,
                    "Prepared manual project changed");
            validate(*desired->session);
            require(desired->session->tracks.size() == prepared->session->tracks.size(),
                    "Prepared manual track inventory changed");
            for (std::size_t n = 0; n < prepared->session->tracks.size(); ++n)
                require(desired->session->tracks[n].id == prepared->session->tracks[n].id,
                        "Prepared manual track ordering changed");
            require(desired->session->master.has_value() == prepared->session->master.has_value() &&
                        (!prepared->session->master ||
                         (desired->session->master->id == prepared->session->master->id &&
                          desired->session->master->plan == prepared->session->master->plan)),
                    "Prepared manual mix structure changed");
            for (const auto &route : prepared->plan.tracks) {
                const auto *old = findTrack(*prepared->session, route.track);
                const auto *next = findTrack(*desired->session, route.track);
                require(old && next && old->layout == next->layout && old->eq.id == next->eq.id &&
                            old->eq.bands.size() == next->eq.bands.size(),
                        "Prepared manual track structure changed");
                for (std::size_t n = 0; n < old->eq.bands.size(); ++n)
                    require(old->eq.bands[n].id == next->eq.bands[n].id,
                            "Prepared manual EQ structure changed");
            }
            for (const auto &arm : prepared->arms) {
                const auto *old = findTrack(*prepared->session, arm.binding.track);
                const auto *next = findTrack(*desired->session, arm.binding.track);
                require(old && next && next->inputLatencyFrames == old->inputLatencyFrames &&
                            next->monitoring == old->monitoring,
                        "Prepared manual capture intent changed");
            }
            checkedRevision = desired->revision;
        }
        if (!bundle && desired->revision > view.acceptedRevision) {
            Bundle next{*desired, {}, std::vector<std::uint64_t>(prepared->plan.tracks.size()), 0};
            for (const auto &route : prepared->plan.tracks) {
                const auto &old = findTrack(*acceptedModel, route.track)->eq;
                const auto &track = *findTrack(*desired->session, route.track);
                for (std::size_t n = 0; n < old.bands.size(); ++n)
                    if (old.bands[n] != track.eq.bands[n])
                        next.events.push_back(endpoint->parameterEvent(
                            *desired->session,
                            {track.id, track.eq.id, track.eq.bands[n].id, BandParameter::GainDb}));
                if (old.enabled != track.eq.enabled)
                    next.events.push_back(endpoint->enableEvent(track.id, track.eq.enabled));
            }
            if (next.events.empty()) {
                acceptedModel = desired->session;
                view.acceptedRevision = desired->revision;
                if (view.position > prepared->options.run.playback.graph.startFrame)
                    view.appliedRevision = desired->revision;
            } else
                bundle = std::move(next);
        }
        if (!bundle)
            return;
        while (bundle->submitted < bundle->events.size()) {
            require(parameterRevision != UINT64_MAX, "Manual parameter revision exhausted");
            const auto &event = bundle->events[bundle->submitted];
            require(event.track < bundle->required.size(), "Manual parameter track unavailable");
            const auto result = endpoint->submitParameter(event, parameterRevision + 1);
            if (result == SubmitStatus::Full)
                return;
            require(result == SubmitStatus::Accepted, "Manual EQ update refused");
            bundle->required[event.track] = ++parameterRevision;
            ++bundle->submitted;
        }
        acceptedModel = bundle->target.session;
        view.acceptedRevision = bundle->target.revision;
    }
    void finish() {
        if (!endpoint)
            return;
        view.phase = ManualControlPhase::Finalizing;
        publish();
        std::shared_ptr<ManualRecordingInterrupt> token;
        {
            std::lock_guard lock(mutex);
            token = interrupt;
        }
        try {
            endpoint->stop(token && token->cancelRequested());
        } catch (...) {
            reportException();
        }
        try {
            endpoint->checkReader();
        } catch (...) {
            reportException();
        }
        try {
            endpoint->checkError();
        } catch (...) {
            reportException();
        }
        try {
            collect();
        } catch (...) {
            reportException();
        }
        // Controller reserves reliable inbox capacity BEFORE accepting work;
        // shutdown cannot silently discard a backend reply/result under pressure.
        require(!punchInFlight && !endpoint->occupiedSlots(),
                "Manual shutdown left undelivered replies/results");
        endpoint.reset();
        bundle.reset();
        prepared.reset();
        view.ports.clear();
        view.phase = view.supported ? ManualControlPhase::Idle : ManualControlPhase::Unsupported;
    }
    void execute(Queued q) {
        ManualControlReceipt r;
        r.sequence = q.sequence;
        r.kind = q.command.kind;
        r.generation = view.generation;
        bool constructing = false, activating = false;
        try {
            auto &c = q.command;
            {
                std::lock_guard lock(mutex);
                if (closing || q.epoch != stopEpoch) {
                    r.result = ManualControlResult::Stopped;
                    view.commands.push_back(std::move(r));
                    return;
                }
            }
            if (c.kind == ManualControlKind::Prepare) {
                require(view.supported && !endpoint && view.groups.empty() && view.punches.empty(),
                        "Resolve manual results and stop transport before preparing");
                auto &p = c.preparation;
                require(p.session && p.modelRevision && !p.root.empty() &&
                            view.generation != UINT64_MAX,
                        "Manual preparation requires an immutable project prefix");
                validate(*p.session);
                constructing = true;
                const auto token = std::make_shared<ManualRecordingInterrupt>();
                {
                    std::lock_guard lock(mutex);
                    interrupt = token;
                    if (closing || q.epoch != stopEpoch)
                        token->requestStop();
                }
                p.options.run.interrupt = token;
                p.options.run.playback.graph.resources = options.projectMemory;
                p.options.run.reader.resources = options.projectMemory;
                p.options.run.memoryBudgetBytes = options.projectMemory.usage().limitBytes;
                p.options.run.playback.graph.memoryBudgetBytes = p.options.run.memoryBudgetBytes;
                p.options.run.capture.memoryBudgetBytes = p.options.run.memoryBudgetBytes;
                for (auto &arm : p.arms)
                    arm.writer.resources = options.projectMemory;
                p.options.run.playback.graph.generation = ++view.generation;
                r.generation = view.generation;
                view.phase = ManualControlPhase::Preparing;
                view.fault = false;
                view.error.reset();
                view.diagnostic.clear();
                publish();
                endpoint = options.factory(p);
                require(bool(endpoint), "Manual endpoint factory returned no endpoint");
                root = p.root;
                prepared = p;
                acceptedModel = p.session;
                parameterRevision = checkedRevision = 0;
                appliedParameters.assign(p.plan.tracks.size(), 0);
                bundle.reset();
                view.desiredRevision = view.acceptedRevision = p.modelRevision;
                view.appliedRevision = 0;
                view.modelRevision = p.modelRevision;
                view.position = 0;
                view.status = endpoint->status();
                view.ports = endpoint->ports();
                nextInventory = std::chrono::steady_clock::now() + std::chrono::milliseconds(250);
                if (token->stopRequested()) {
                    finish();
                    r.result = ManualControlResult::Stopped;
                } else {
                    view.phase = ManualControlPhase::Ready;
                    r.result = ManualControlResult::Applied;
                }
            } else {
                require(endpoint && c.generation == view.generation,
                        "Manual command belongs to a stale or stopped generation");
                switch (c.kind) {
                case ManualControlKind::Activate:
                    require(view.phase == ManualControlPhase::Ready,
                            "Manual transport is already activated");
                    activating = true;
                    endpoint->activate(c.inputs, c.outputs);
                    view.phase = ManualControlPhase::Playing;
                    break;
                case ManualControlKind::PrepareTake:
                    require(view.groups.size() + endpoint->occupiedSlots() < manualPunchSlots,
                            "Resolve manual result groups before preparing another take");
                    r.take = endpoint->prepareTake();
                    break;
                case ManualControlKind::AbandonTake:
                    endpoint->abandonTake(c.take);
                    break;
                case ManualControlKind::Punch:
                    require(view.punches.size() + punchInFlight < manualPunchCommands,
                            "Acknowledge manual punch replies before submitting more");
                    require(endpoint->submit({c.action, c.frame, c.generation, q.sequence,
                                              c.take}) == ManualPunchSubmit::Accepted,
                            "Manual punch submission was refused");
                    ++punchInFlight;
                    break;
                case ManualControlKind::Prepare:
                    break;
                }
                r.result = ManualControlResult::Applied;
            }
        } catch (const ProjectError &e) {
            r.error = e.code();
            r.diagnostic = e.what();
        } catch (const std::exception &e) {
            r.error = ErrorCode::Io;
            r.diagnostic = e.what();
        } catch (...) {
            r.error = ErrorCode::Io;
            r.diagnostic = "Unknown manual control error";
        }
        if (r.error && (constructing || activating)) {
            error(*r.error, r.diagnostic);
            finish();
            if (!endpoint) {
                view.ports.clear();
                view.phase =
                    view.supported ? ManualControlPhase::Idle : ManualControlPhase::Unsupported;
            }
        }
        view.commands.push_back(std::move(r));
    }
    void run() {
        for (;;) {
            std::optional<Queued> command;
            std::uint64_t stop;
            bool close;
            {
                std::unique_lock lock(mutex);
                wake.wait_for(lock, std::chrono::milliseconds(5));
                consumeAcknowledgements();
                stop = stopEpoch;
                close = closing;
                if (following) {
                    desired = std::move(following);
                    following.reset();
                }
                if (!queue.empty()) {
                    command = std::move(queue.front());
                    queue.pop_front();
                }
            }
            if (stop != seenStop || close) {
                try {
                    finish();
                } catch (...) {
                    reportException();
                }
                seenStop = stop;
                view.stopAcknowledged = stop;
            }
            if (command)
                execute(std::move(*command));
            if (endpoint) {
                try {
                    reconcile();
                    endpoint->service();
                    endpoint->checkError();
                    collect();
                    if (std::chrono::steady_clock::now() >= nextInventory) {
                        view.ports = endpoint->ports();
                        nextInventory =
                            std::chrono::steady_clock::now() + std::chrono::milliseconds(250);
                    }
                    if (terminal(view.status)) {
                        if (view.status != DuplexStatus::Stopped &&
                            view.status != DuplexStatus::Complete)
                            error(ErrorCode::Io, "Manual audio transport stopped unexpectedly");
                        finish();
                    }
                } catch (...) {
                    reportException();
                    try {
                        finish();
                    } catch (...) {
                        reportException();
                    }
                }
            }
            bool drained;
            {
                std::lock_guard lock(mutex);
                drained = queue.empty();
            }
            if (close && drained && !endpoint) {
                view.phase = ManualControlPhase::Closed;
                view.closed = true;
            }
            publish();
            if (view.closed)
                return;
        }
    }
};
ManualRecordingController::ManualRecordingController(ManualControlOptions options)
    : state_(std::make_unique<State>(std::move(options))) {
    state_->worker = std::thread([this] { state_->run(); });
}
ManualRecordingController::~ManualRecordingController() {
    requestShutdown();
    state_->worker.join();
}
ManualControlSubmission ManualRecordingController::submit(ManualControlCommand c) {
    std::lock_guard lock(state_->mutex);
    if (state_->closing)
        return {ManualControlAdmission::Closing, 0};
    if (state_->queue.size() == controlQueueCapacity ||
        state_->commandCredits == manualPunchCommands || state_->sequence == UINT64_MAX)
        return {ManualControlAdmission::Full, 0};
    const auto sequence = state_->sequence + 1;
    state_->queue.push_back({std::move(c), sequence, state_->stopEpoch});
    state_->sequence = sequence;
    ++state_->commandCredits;
    state_->wake.notify_one();
    return {ManualControlAdmission::Accepted, sequence};
}
bool ManualRecordingController::follow(std::uint64_t generation, std::filesystem::path root,
                                       std::shared_ptr<const Session> session,
                                       std::uint64_t revision) {
    std::lock_guard lock(state_->mutex);
    if (state_->closing || !session || !revision || generation != state_->latest->generation ||
        (state_->following && revision <= state_->following->revision) ||
        (state_->lastFollowed >= revision && generation == state_->lastFollowingGeneration))
        return false;
    state_->lastFollowed = revision;
    state_->lastFollowingGeneration = generation;
    state_->following = State::Following{generation, revision, std::move(root), std::move(session)};
    state_->wake.notify_one();
    return true;
}
std::uint64_t ManualRecordingController::requestStop(bool cancel) noexcept {
    std::lock_guard lock(state_->mutex);
    if (state_->interrupt)
        cancel ? state_->interrupt->requestCancel() : state_->interrupt->requestStop();
    if (state_->stopEpoch != UINT64_MAX)
        ++state_->stopEpoch;
    state_->wake.notify_one();
    return state_->stopEpoch;
}
bool ManualRecordingController::acknowledgeCommand(std::uint64_t sequence) {
    std::lock_guard lock(state_->mutex);
    if (std::none_of(state_->latest->commands.begin(), state_->latest->commands.end(),
                     [&](const auto &r) { return r.sequence == sequence; }) ||
        std::find(state_->commandAcks.begin(), state_->commandAcks.end(), sequence) !=
            state_->commandAcks.end())
        return false;
    if (state_->latest->closed) {
        auto next = std::make_shared<ManualControlSnapshot>(*state_->latest);
        std::erase_if(next->commands, [&](const auto &r) { return r.sequence == sequence; });
        state_->latest = std::move(next);
        return true;
    }
    state_->commandAcks.push_back(sequence);
    state_->wake.notify_one();
    return true;
}
bool ManualRecordingController::acknowledgePunch(std::uint64_t revision) {
    std::lock_guard lock(state_->mutex);
    if (std::none_of(state_->latest->punches.begin(), state_->latest->punches.end(),
                     [&](const auto &r) { return r.command.revision == revision; }) ||
        std::find(state_->punchAcks.begin(), state_->punchAcks.end(), revision) !=
            state_->punchAcks.end())
        return false;
    if (state_->latest->closed) {
        auto next = std::make_shared<ManualControlSnapshot>(*state_->latest);
        std::erase_if(next->punches, [&](const auto &r) { return r.command.revision == revision; });
        state_->latest = std::move(next);
        return true;
    }
    state_->punchAcks.push_back(revision);
    state_->wake.notify_one();
    return true;
}
bool ManualRecordingController::acknowledgeGroup(std::uint64_t generation, std::uint64_t take) {
    std::lock_guard lock(state_->mutex);
    const auto key = std::pair{generation, take};
    if (std::none_of(
            state_->latest->groups.begin(), state_->latest->groups.end(),
            [&](const auto &g) { return g.generation == generation && g.group->take == take; }) ||
        std::find(state_->groupAcks.begin(), state_->groupAcks.end(), key) !=
            state_->groupAcks.end())
        return false;
    if (state_->latest->closed) {
        auto next = std::make_shared<ManualControlSnapshot>(*state_->latest);
        std::erase_if(next->groups, [&](const auto &g) {
            return g.generation == generation && g.group->take == take;
        });
        state_->latest = std::move(next);
        return true;
    }
    state_->groupAcks.push_back(key);
    state_->wake.notify_one();
    return true;
}
void ManualRecordingController::requestShutdown() noexcept {
    std::lock_guard lock(state_->mutex);
    state_->closing = true;
    if (state_->interrupt)
        state_->interrupt->requestStop();
    if (state_->stopEpoch != UINT64_MAX)
        ++state_->stopEpoch;
    state_->wake.notify_one();
}
std::shared_ptr<const ManualControlSnapshot> ManualRecordingController::snapshot() const {
    std::lock_guard lock(state_->mutex);
    return state_->latest;
}
} // namespace soundcurrent::daw::ui
