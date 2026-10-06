// SPDX-License-Identifier: GPL-3.0-only
#include <soundcurrent/manual_punch.hpp>
#include <algorithm>
#include <array>
#include <limits>
#include <exception>

namespace soundcurrent::daw {
namespace {
bool running(DuplexStatus s) noexcept {
    return s == DuplexStatus::Ready || s == DuplexStatus::Running || s == DuplexStatus::Underflow;
}
DuplexStatus playbackStatus(PlaybackStatus s) noexcept {
    switch (s) {
    case PlaybackStatus::Running:
        return DuplexStatus::Running;
    case PlaybackStatus::Underflow:
        return DuplexStatus::Underflow;
    case PlaybackStatus::Complete:
        return DuplexStatus::Complete;
    case PlaybackStatus::Stopped:
        return DuplexStatus::Stopped;
    case PlaybackStatus::ReaderFailed:
        return DuplexStatus::ReaderFailed;
    case PlaybackStatus::InvalidBuffer:
        return DuplexStatus::BufferUnavailable;
    case PlaybackStatus::TimingError:
        return DuplexStatus::ClockDiscontinuity;
    case PlaybackStatus::ProcessorFailed:
        return DuplexStatus::ProcessorFailed;
    }
    return DuplexStatus::ProcessorFailed;
}
CaptureEndReason endReason(DuplexStatus s, CaptureStatus capture) noexcept {
    switch (s) {
    case DuplexStatus::Complete:
        return CaptureEndReason::RangeComplete;
    case DuplexStatus::RateChanged:
        return CaptureEndReason::RateChanged;
    case DuplexStatus::QuantumExceeded:
        return CaptureEndReason::QuantumExceeded;
    case DuplexStatus::ClockDiscontinuity:
        return CaptureEndReason::ClockDiscontinuity;
    case DuplexStatus::BufferUnavailable:
    case DuplexStatus::DeviceLost:
        return CaptureEndReason::DeviceLost;
    case DuplexStatus::CaptureFailed:
        return capture == CaptureStatus::WriterFailed ? CaptureEndReason::WriterFailed
                                                      : CaptureEndReason::CaptureFailed;
    case DuplexStatus::ReaderFailed:
    case DuplexStatus::ProcessorFailed:
        return CaptureEndReason::ProcessorFailed;
    default:
        return CaptureEndReason::UserStop;
    }
}
void require(bool value, const char *message) {
    if (!value)
        throw ProjectError(ErrorCode::InvalidState, message);
}
} // namespace
struct ManualPunchTake::State {
    std::uint64_t id;
    std::vector<std::unique_ptr<CapturePipe>> pipes;
    std::vector<bool> originSet;
    std::vector<Frame> retiredFrames;
    Frame begin = 0, end = 0;
    std::atomic<std::uint32_t> phase{0}, beginReady{0}, endReady{0};
    // Control-only reservation and pending command references.
    std::size_t payload = 0, pendingStarts = 0;
    State(std::uint64_t n, std::vector<CaptureConfig> configs)
        : id(n), originSet(configs.size(), false), retiredFrames(configs.size(), 0) {
        for (const auto &c : configs)
            pipes.push_back(std::make_unique<CapturePipe>(c));
    }
};
ManualPunchTake::ManualPunchTake(std::uint64_t n, std::vector<CaptureConfig> c)
    : state_(std::make_unique<State>(n, std::move(c))) {}
ManualPunchTake::~ManualPunchTake() = default;
std::uint64_t ManualPunchTake::id() const noexcept {
    return state_->id;
}
ManualTakePhase ManualPunchTake::phase() const noexcept {
    return static_cast<ManualTakePhase>(state_->phase.load(std::memory_order_acquire));
}
std::optional<Frame> ManualPunchTake::startFrame() const noexcept {
    if (!state_->beginReady.load(std::memory_order_acquire))
        return {};
    return state_->begin;
}
std::optional<Frame> ManualPunchTake::endFrame() const noexcept {
    if (!state_->endReady.load(std::memory_order_acquire))
        return {};
    return state_->end;
}
std::optional<Frame> ManualPunchTake::retiredFrames(std::size_t n) const {
    require(n < state_->pipes.size(), "Manual capture lane missing");
    if (phase() != ManualTakePhase::Retired)
        return {};
    return state_->retiredFrames[n];
}
CapturePipe &ManualPunchTake::pipe(std::size_t n) {
    require(n < state_->pipes.size(), "Manual capture lane missing");
    return *state_->pipes[n];
}
std::size_t ManualPunchTake::lanes() const noexcept {
    return state_->pipes.size();
}
struct ManualPunchBridge::State {
    struct Arm {
        ManualPunchArm binding;
        std::size_t mixOrdinal = 0;
        ChannelLayout layout;
        std::vector<const float *> input, captured;
        explicit Arm(ManualPunchArm a)
            : binding(std::move(a)), input(binding.inputChannels.size()), captured(input.size()) {}
    };
    MixPlaybackRun &run;
    std::vector<Arm> arms;
    std::vector<LiveMixInput> live;
    std::array<float *, 256> output{};
    std::uint32_t inputs;
    CaptureBackend backend;
    std::size_t budget, reserved;
    Frame latestOut;
    std::array<std::unique_ptr<ManualPunchTake>, manualPunchSlots> owned;
    std::array<ManualPunchTake *, manualPunchSlots> active{}; // Audio-only references.
    ManualPunchTake *recording = nullptr;
    std::uint64_t nextTake = 1;
    std::size_t outstanding = 0; // Control reply credit; only ack releases it.
    struct Queued {
        ManualPunchCommand command;
        ManualPunchTake *take = nullptr;
    };
    SpscQueue<Queued, manualPunchCommands> commands;
    SpscQueue<ManualPunchReceipt, manualPunchCommands> replies;
    std::atomic<std::uint32_t> terminal{0}, faultReady{0};
    DeviceBlockClock previous{};
    DuplexCallbackFault fault{};
    bool started = false;
    State(MixPlaybackRun &r, std::uint32_t n, CaptureBackend b, std::size_t bytes,
          std::size_t reserve)
        : run(r), inputs(n), backend(b), budget(bytes), reserved(reserve),
          latestOut(r.config().endFrame) {}
    ManualPunchTake *find(std::uint64_t n) const noexcept { // Control only.
        for (const auto &p : owned)
            if (p && p->id() == n)
                return p.get();
        return nullptr;
    }
    DuplexStatus status() const noexcept {
        return static_cast<DuplexStatus>(terminal.load(std::memory_order_acquire));
    }
    void recordFault(const DeviceBlockClock &clock, DuplexStatus s, Frame at, std::size_t in,
                     std::size_t out, std::uint32_t capacity,
                     std::size_t lane = SIZE_MAX) noexcept {
        if (faultReady.load(std::memory_order_relaxed))
            return;
        fault = {clock,
                 previous,
                 s,
                 at,
                 run.config().graph.generation,
                 run.sampleRate(),
                 run.config().graph.maximumFrames,
                 capacity,
                 inputs,
                 run.graph().plan().output.channels,
                 in,
                 out,
                 lane,
                 started};
        faultReady.store(1, std::memory_order_release);
    }
    void close(Frame at) noexcept {
        auto &t = *recording->state_;
        t.end = at;
        t.endReady.store(1, std::memory_order_release);
        t.phase.store(static_cast<std::uint32_t>(ManualTakePhase::Postroll),
                      std::memory_order_release);
        recording = nullptr;
    }
    void retire(std::size_t slot) noexcept {
        auto *p = active[slot];
        for (std::size_t n = 0; n < p->state_->pipes.size(); ++n) {
            const auto &pipe = *p->state_->pipes[n];
            const auto config = pipe.recordingConfig();
            p->state_->retiredFrames[n] = config ? pipe.nextFrame() - config->startFrame : 0;
        }
        active[slot] = nullptr; // Drop callback references BEFORE retirement publication.
        p->state_->phase.store(static_cast<std::uint32_t>(ManualTakePhase::Retired),
                               std::memory_order_release);
        // Do not dereference p after publication: control may now join/reclaim it.
    }
    void reply(const ManualPunchCommand &c, Frame at, std::uint64_t id,
               ManualPunchResult result) noexcept {
        // Every accepted command owns one reserved credit until control pops it.
        // Thus outstanding replies + queued commands never exceed this queue.
        const bool published = replies.tryPush({c, at, id, result});
        if (!published)
            std::terminate(); // Internal credit invariant, never a lossy acknowledgement.
    }
    DuplexStatus finish(DuplexStatus desired) noexcept {
        auto prior = terminal.load(std::memory_order_acquire);
        if (running(static_cast<DuplexStatus>(prior)))
            terminal.compare_exchange_strong(prior, static_cast<std::uint32_t>(desired),
                                             std::memory_order_acq_rel);
        const auto s = status();
        if (running(s))
            return s;
        const auto at = run.position();
        if (recording)
            close(at);
        for (std::size_t slot = 0; slot < active.size(); ++slot) {
            if (!active[slot])
                continue;
            for (auto &pipe : active[slot]->state_->pipes)
                pipe->finish(endReason(s, pipe->status()));
            retire(slot);
        }
        run.requestStop();
        run.cancelReader();
        Queued queued;
        const auto count = commands.consumerAvailable();
        for (std::uint32_t n = 0; n < count && commands.tryPop(queued); ++n)
            reply(queued.command, at, queued.command.take, ManualPunchResult::TransportStopped);
        return s;
    }
    ManualPunchResult apply(const ManualPunchCommand &c, ManualPunchTake *take, Frame at) noexcept {
        if (c.generation != run.config().graph.generation)
            return ManualPunchResult::WrongGeneration;
        if (c.frame >= 0 && c.frame < at)
            return ManualPunchResult::Late;
        if (at > latestOut || (c.action == ManualPunchAction::In && at == latestOut))
            return ManualPunchResult::InvalidFrame;
        if (c.action == ManualPunchAction::Out) {
            if (!recording)
                return ManualPunchResult::NotRecording;
            if (at <= recording->state_->begin)
                return ManualPunchResult::InvalidFrame;
            close(at);
            return ManualPunchResult::Applied;
        }
        if (recording)
            return ManualPunchResult::AlreadyRecording;
        if (!take || take->phase() != ManualTakePhase::Prepared)
            return ManualPunchResult::TakeUnavailable;
        auto slot = std::find(active.begin(), active.end(), nullptr);
        if (slot == active.end())
            return ManualPunchResult::TakeUnavailable;
        // All lane arithmetic and pipe state checked before publishing any start.
        auto &t = *take->state_;
        for (std::size_t n = 0; n < arms.size(); ++n) {
            const auto &p = *t.pipes[n];
            const auto latency = arms[n].binding.inputLatencyFrames;
            if (at > std::numeric_limits<Frame>::max() - latency)
                return ManualPunchResult::InvalidFrame;
            if (p.recordingConfig() || p.producerDone() || p.status() != CaptureStatus::Running)
                return ManualPunchResult::TakeUnavailable;
        }
        *slot = take;
        for (std::size_t n = 0; n < arms.size(); ++n)
            if (!t.pipes[n]->beginAt(at + arms[n].binding.inputLatencyFrames))
                return ManualPunchResult::CaptureFailed;
        t.begin = at;
        t.beginReady.store(1, std::memory_order_release);
        recording = take;
        t.phase.store(static_cast<std::uint32_t>(ManualTakePhase::Recording),
                      std::memory_order_release);
        return ManualPunchResult::Applied;
    }
};
ManualPunchBridge::ManualPunchBridge(MixPlaybackRun &run, const Session &s,
                                     std::vector<ManualPunchArm> arms, std::uint32_t inputs,
                                     CaptureBackend backend, std::size_t budget) {
    validate(s);
    require(!arms.empty() && arms.size() <= 256 && inputs && inputs <= 256 &&
                backend <= CaptureBackend::Asio && budget && budget <= 256 * 1024 * 1024 &&
                s.sampleRate == run.sampleRate() && run.position() == run.config().graph.startFrame,
            "Invalid manual punch preparation");
    std::size_t payload = sizeof(State) + 8192 + run.config().graph.memoryBudgetBytes;
    for (const auto &a : arms)
        payload += sizeof(State::Arm) + a.inputChannels.size() * 2 * sizeof(float *) +
                   sizeof(LiveMixInput);
    require(payload <= budget, "Manual punch playback/bridge reservation exceeds budget");
    state_ = std::make_unique<State>(run, inputs, backend, budget, payload);
    auto &st = *state_;
    Frame maximumLatency = 0;
    for (auto &a : arms) {
        const auto track = std::find_if(s.tracks.begin(), s.tracks.end(),
                                        [&](const auto &t) { return t.id == a.track; });
        const auto &routes = run.graph().plan().tracks;
        const auto route = std::find_if(routes.begin(), routes.end(),
                                        [&](const auto &t) { return t.track == a.track; });
        require(track != s.tracks.end() && a.inputChannels.size() == track->layout.channels &&
                    a.inputLatencyFrames >= 0 && a.inputLatencyFrames <= Frame(s.sampleRate) * 60 &&
                    validRecordingMonitor(a.monitoring) && route != routes.end() &&
                    run.graph().prepared(std::size_t(route - routes.begin())).channels() ==
                        a.inputChannels.size() &&
                    std::none_of(a.inputChannels.begin(), a.inputChannels.end(),
                                 [&](auto n) { return n >= inputs; }) &&
                    std::none_of(st.arms.begin(), st.arms.end(),
                                 [&](const auto &old) { return old.binding.track == a.track; }),
                "Invalid manual punch arm");
        maximumLatency = std::max(maximumLatency, a.inputLatencyFrames);
        st.arms.emplace_back(std::move(a));
        st.arms.back().mixOrdinal = std::size_t(route - routes.begin());
        st.arms.back().layout = track->layout;
    }
    require(maximumLatency < run.config().endFrame - run.config().graph.startFrame,
            "Manual punch has no room for delayed capture");
    st.latestOut -= maximumLatency;
    st.live.resize(st.arms.size());
}
ManualPunchBridge::~ManualPunchBridge() = default;
ManualPunchTake &ManualPunchBridge::prepareTake(CaptureConfig config) {
    auto &s = *state_;
    require(running(status()) && config.startFrame == 0 &&
                config.sampleRate == s.run.sampleRate() &&
                config.maximumCallbackFrames >= s.run.config().graph.maximumFrames,
            "Invalid manual take preparation");
    auto slot = std::find_if(s.owned.begin(), s.owned.end(), [](const auto &p) { return !p; });
    require(slot != s.owned.end() && s.nextTake && s.nextTake != UINT64_MAX,
            "Manual take slot unavailable");
    std::vector<CaptureConfig> configs;
    std::size_t payload = sizeof(ManualPunchTake) + sizeof(ManualPunchTake::State) + 8192;
    for (const auto &a : s.arms) {
        auto c = config;
        c.layout = a.layout;
        c.deferredStart = true;
        c = prepareCaptureConfig(c);
        const auto bytes = armedCapturePayloadBytes(c, a.input.size()) + sizeof(CaptureConfig) +
                           sizeof(void *) + sizeof(Frame);
        require(payload <= s.budget - s.reserved && bytes <= s.budget - s.reserved - payload,
                "Manual takes exceed aggregate payload reservation");
        payload += bytes;
        configs.push_back(c);
    }
    auto take =
        std::unique_ptr<ManualPunchTake>(new ManualPunchTake(s.nextTake, std::move(configs)));
    take->state_->payload = payload;
    s.reserved += payload;
    ++s.nextTake;
    *slot = std::move(take);
    return **slot;
}
void ManualPunchBridge::releaseTake(ManualPunchTake &take) {
    auto &s = *state_;
    auto slot = std::find_if(s.owned.begin(), s.owned.end(),
                             [&](const auto &p) { return p.get() == &take; });
    require(
        slot != s.owned.end() && !take.state_->pendingStarts &&
            (take.phase() == ManualTakePhase::Retired || take.phase() == ManualTakePhase::Prepared),
        "Manual take still referenced by audio or commands");
    s.reserved -= take.state_->payload;
    slot->reset(); // Caller has joined all disk consumers. Never on audio.
}
ManualPunchSubmit ManualPunchBridge::submit(ManualPunchCommand c) noexcept {
    auto &s = *state_;
    if (!running(status()))
        return ManualPunchSubmit::Stopped;
    if (c.action > ManualPunchAction::Out || c.frame < -1 || c.frame > s.run.config().endFrame ||
        !c.revision || (c.action == ManualPunchAction::Out && c.take))
        return ManualPunchSubmit::Invalid;
    auto *take = c.action == ManualPunchAction::In ? s.find(c.take) : nullptr;
    if (c.action == ManualPunchAction::In && !take)
        return ManualPunchSubmit::Invalid;
    if (s.outstanding == manualPunchCommands || !s.commands.producerHasSpace())
        return ManualPunchSubmit::Full;
    // Queue publication happens last; control counters need no audio access.
    ++s.outstanding;
    if (take)
        ++take->state_->pendingStarts;
    if (!s.commands.tryPush({c, take}))
        std::terminate();
    return ManualPunchSubmit::Accepted;
}
bool ManualPunchBridge::acknowledgement(ManualPunchReceipt &r) noexcept {
    auto &s = *state_;
    if (!s.replies.tryPop(r))
        return false;
    --s.outstanding;
    if (r.command.action == ManualPunchAction::In) {
        auto *take = s.find(r.command.take);
        if (!take || !take->state_->pendingStarts)
            std::terminate();
        --take->state_->pendingStarts;
    }
    return true;
}
DuplexStatus ManualPunchBridge::status() const noexcept {
    return state_->status();
}
std::optional<DuplexCallbackFault> ManualPunchBridge::callbackFault() const noexcept {
    if (!state_->faultReady.load(std::memory_order_acquire))
        return {};
    return state_->fault;
}
const ManualPunchArm &ManualPunchBridge::arm(std::size_t n) const {
    require(n < state_->arms.size(), "Manual arm missing");
    return state_->arms[n].binding;
}
Frame ManualPunchBridge::latestPunchOut() const noexcept {
    return state_->latestOut;
}
void ManualPunchBridge::requestFault(DuplexStatus desired) noexcept {
    if (running(desired) || desired == DuplexStatus::Complete ||
        desired > DuplexStatus::CaptureFailed)
        return;
    auto &s = *state_;
    auto prior = s.terminal.load(std::memory_order_acquire);
    while (running(static_cast<DuplexStatus>(prior)))
        if (s.terminal.compare_exchange_strong(prior, static_cast<std::uint32_t>(desired),
                                               std::memory_order_acq_rel)) {
            s.run.requestStop();
            return;
        }
}
void ManualPunchBridge::requestStop() noexcept {
    requestFault(DuplexStatus::Stopped);
}
void ManualPunchBridge::finishQuiescent() noexcept {
    state_->finish(running(status()) ? DuplexStatus::Stopped : status());
}
DuplexStatus ManualPunchBridge::process(const DeviceBlockClock &clock,
                                        std::span<const float *const> input,
                                        std::span<float *const> output,
                                        std::uint32_t capacity) noexcept {
    auto &s = *state_;
    const auto silence = [&] {
        for (auto *p : output)
            if (p)
                std::fill_n(p, capacity, 0.f);
    };
    const auto fail = [&](DuplexStatus next, std::size_t lane = SIZE_MAX) {
        s.recordFault(clock, next, s.run.position(), input.size(), output.size(), capacity, lane);
        silence();
        return s.finish(next);
    };
    auto next = status();
    if (!running(next)) {
        silence();
        return s.finish(next);
    }
    const auto &c = s.run.config().graph;
    if (!clock.duration || clock.duration > c.maximumFrames || clock.duration > capacity)
        return fail(DuplexStatus::QuantumExceeded);
    if (clock.rateNumerator != 1 || clock.rateDenominator != s.run.sampleRate())
        return fail(DuplexStatus::RateChanged);
    if (input.size() != s.inputs || output.size() != s.run.graph().plan().output.channels)
        return fail(DuplexStatus::BufferUnavailable);
    const auto maximumOffsetNs = (clock.duration - 1) * 1000000000ULL / s.run.sampleRate();
    if (clock.monotonicNs && clock.monotonicNs > UINT64_MAX - maximumOffsetNs)
        return fail(DuplexStatus::ClockDiscontinuity);
    if (clock.xrun || clock.discontinuity || clock.position > UINT64_MAX - clock.duration ||
        (s.started && (clock.id != s.previous.id ||
                       clock.position != s.previous.position + s.previous.duration)))
        return fail(DuplexStatus::ClockDiscontinuity);
    if (std::any_of(input.begin(), input.end(), [](auto *p) { return !p; }) ||
        std::any_of(output.begin(), output.end(), [](auto *p) { return !p; })) {
        if (!s.started && std::all_of(input.begin(), input.end(), [](auto *p) { return !p; }) &&
            std::all_of(output.begin(), output.end(), [](auto *p) { return !p; }))
            return next;
        return fail(DuplexStatus::BufferUnavailable);
    }
    for (std::size_t n = 0; n < output.size(); ++n)
        for (std::size_t prior = 0; prior < n; ++prior)
            if (output[n] == output[prior])
                return fail(DuplexStatus::BufferUnavailable);
    const auto blockStart = s.run.position();
    const auto blockEnd =
        blockStart + std::min<Frame>(Frame(clock.duration), s.run.config().endFrame - blockStart);
    auto commandBudget = s.commands.consumerAvailable();
    const auto applyCommands = [&](Frame at) noexcept {
        State::Queued queued;
        while (commandBudget && s.commands.tryPeek(queued) &&
               (queued.command.frame < 0 || queued.command.frame <= at)) {
            s.commands.tryPop(queued);
            --commandBudget;
            const auto &command = queued.command;
            // The command's release/acquire publication transfers its immutable
            // take pointer; its pending-start credit prevents control reclamation.
            auto *take = queued.take;
            const auto id = command.action == ManualPunchAction::In ? command.take
                            : s.recording                           ? s.recording->id()
                                                                    : 0;
            const auto result = s.apply(command, take, at);
            s.reply(command, at, id, result);
            if (result == ManualPunchResult::CaptureFailed)
                return false;
        }
        return true;
    };
    while (s.run.position() < blockEnd) {
        const auto at = s.run.position();
        if (!applyCommands(at))
            return fail(DuplexStatus::CaptureFailed);
        if (s.recording && at == s.latestOut)
            s.close(at);
        State::Queued queued;
        Frame end = blockEnd;
        if (commandBudget && s.commands.tryPeek(queued) && queued.command.frame > at)
            end = std::min(end, queued.command.frame);
        if (s.recording)
            end = std::min(end, s.latestOut);
        const auto count = static_cast<std::uint32_t>(end - at);
        const auto blockOffset = static_cast<std::uint32_t>(at - blockStart);
        for (auto &arm : s.arms)
            for (std::size_t channel = 0; channel < arm.input.size(); ++channel)
                arm.input[channel] = input[arm.binding.inputChannels[channel]] + blockOffset;
        // Preflight every origin's timestamp arithmetic before any raw publication.
        for (auto *take : s.active)
            if (take) {
                auto &t = *take->state_;
                for (std::size_t lane = 0; lane < s.arms.size(); ++lane) {
                    const auto latency = s.arms[lane].binding.inputLatencyFrames;
                    const auto begin = std::max(at, t.begin + latency);
                    const auto stop = t.endReady.load(std::memory_order_relaxed)
                                          ? t.end + latency
                                          : s.run.config().endFrame;
                    const auto finish = std::min(end, stop);
                    if (begin < finish && !t.originSet[lane]) {
                        const auto ns =
                            std::uint64_t(begin - blockStart) * 1000000000ULL / s.run.sampleRate();
                        if (clock.monotonicNs && clock.monotonicNs > UINT64_MAX - ns)
                            return fail(DuplexStatus::ClockDiscontinuity, lane);
                    }
                }
            }
        std::size_t failedLane = SIZE_MAX;
        for (std::size_t slot = 0; slot < s.active.size(); ++slot) {
            auto *take = s.active[slot];
            if (!take)
                continue;
            auto &t = *take->state_;
            bool finished = t.endReady.load(std::memory_order_relaxed);
            for (std::size_t lane = 0; lane < s.arms.size(); ++lane) {
                auto &arm = s.arms[lane];
                auto &pipe = *t.pipes[lane];
                const auto latency = arm.binding.inputLatencyFrames;
                const auto begin = std::max(at, t.begin + latency);
                const auto stop = t.endReady.load(std::memory_order_relaxed)
                                      ? t.end + latency
                                      : s.run.config().endFrame;
                const auto finish = std::min(end, stop);
                if (begin < finish) {
                    const auto offset = static_cast<std::uint32_t>(begin - blockStart);
                    if (!t.originSet[lane]) {
                        const auto ns = std::uint64_t(offset) * 1000000000ULL / s.run.sampleRate();
                        if (!pipe.setTimingOrigin({s.backend, clock.position + offset,
                                                   clock.monotonicNs ? clock.monotonicNs + ns : 0,
                                                   c.generation, clock.id, clock.cycle,
                                                   clock.rateNumerator, clock.rateDenominator,
                                                   clock.delay}))
                            return fail(DuplexStatus::CaptureFailed, lane);
                        t.originSet[lane] = true;
                    }
                    for (std::size_t channel = 0; channel < arm.captured.size(); ++channel)
                        arm.captured[channel] = input[arm.binding.inputChannels[channel]] + offset;
                    const auto report =
                        pipe.push(arm.captured, static_cast<std::uint32_t>(finish - begin), begin);
                    if (report.status != CaptureStatus::Running && failedLane == SIZE_MAX)
                        failedLane = lane;
                }
                if (t.endReady.load(std::memory_order_relaxed) && end >= stop) {
                    // Earlier lanes may have finished in a previous callback while
                    // another lane is still capturing latency postroll.
                    if (pipe.status() == CaptureStatus::Running)
                        pipe.finish(CaptureEndReason::RangeComplete);
                } else {
                    finished = false;
                }
                if (pipe.status() != CaptureStatus::Running &&
                    !(pipe.producerDone() && pipe.status() == CaptureStatus::Stopped) &&
                    failedLane == SIZE_MAX)
                    failedLane = lane;
            }
            if (finished && failedLane == SIZE_MAX)
                s.retire(slot);
        }
        if (failedLane != SIZE_MAX)
            return fail(DuplexStatus::CaptureFailed, failedLane);
        std::size_t liveCount = 0;
        for (auto &arm : s.arms)
            if (arm.binding.monitoring == RecordingMonitor::PostEq ||
                (arm.binding.monitoring == RecordingMonitor::AutoRecording && s.recording))
                s.live[liveCount++] = {arm.mixOrdinal, arm.input};
        for (std::size_t channel = 0; channel < output.size(); ++channel)
            s.output[channel] = output[channel] + blockOffset;
        const auto report =
            s.run.process({s.output.data(), output.size()}, count, {s.live.data(), liveCount});
        next = playbackStatus(report.status);
        if (!running(next) && next != DuplexStatus::Complete)
            return fail(next);
        if (!running(status())) {
            silence();
            return s.finish(status());
        }
    }
    // Commands at the exact callback/prepared end still own reliable receipts.
    // A new take can publish its start here; its origin/sample arrives next block.
    if (!applyCommands(blockEnd))
        return fail(DuplexStatus::CaptureFailed);
    for (std::size_t slot = 0; slot < s.active.size(); ++slot) {
        auto *take = s.active[slot];
        if (!take || !take->state_->endReady.load(std::memory_order_relaxed))
            continue;
        auto &t = *take->state_;
        bool complete = true;
        for (std::size_t lane = 0; lane < s.arms.size(); ++lane) {
            auto &pipe = *t.pipes[lane];
            if (pipe.nextFrame() >= t.end + s.arms[lane].binding.inputLatencyFrames) {
                if (pipe.status() == CaptureStatus::Running)
                    pipe.finish(CaptureEndReason::RangeComplete);
            } else {
                complete = false;
            }
        }
        if (complete)
            s.retire(slot);
    }
    s.previous = clock;
    s.started = true;
    for (auto *p : output)
        std::fill(p + (blockEnd - blockStart), p + capacity, 0.f);
    return s.finish(next);
}
} // namespace soundcurrent::daw
