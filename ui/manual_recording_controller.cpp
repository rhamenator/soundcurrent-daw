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
    void publish() {
        auto copy = std::make_shared<const ManualControlSnapshot>(view);
        std::lock_guard lock(mutex);
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
                view.modelRevision = p.modelRevision;
                view.position = 0;
                view.status = endpoint->status();
                view.ports = endpoint->ports();
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
                std::erase_if(view.commands, [&](const auto &r) {
                    if (std::find(commandAcks.begin(), commandAcks.end(), r.sequence) ==
                        commandAcks.end())
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
                stop = stopEpoch;
                close = closing;
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
                    endpoint->service();
                    endpoint->checkError();
                    collect();
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
