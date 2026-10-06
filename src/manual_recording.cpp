// SPDX-License-Identifier: GPL-3.0-only
#include <soundcurrent/manual_recording.hpp>
#include <algorithm>
#include <array>
#include <set>

namespace soundcurrent::daw {
namespace {
void require(bool value, const char *message) {
    if (!value)
        throw ProjectError(ErrorCode::InvalidState, message);
}
bool running(DuplexStatus s) noexcept {
    return s == DuplexStatus::Ready || s == DuplexStatus::Running || s == DuplexStatus::Underflow;
}
bool persistedSpec(const RecordingSpec &a, const RecordingSpec &b) noexcept {
    // Recovery deliberately chooses a new runtime slab size; journal identity
    // comparisons include only persisted recording metadata.
    return a.projectId == b.projectId && a.trackId == b.trackId && a.assetId == b.assetId &&
           a.capture.sampleRate == b.capture.sampleRate && a.capture.layout == b.capture.layout &&
           a.capture.startFrame == b.capture.startFrame &&
           a.inputLatencyFrames == b.inputLatencyFrames && a.recoveredFrom == b.recoveredFrom;
}
} // namespace
bool ManualRecordedGroup::complete() const noexcept {
    return !canceled && beginFrame >= 0 && endFrame > beginFrame && !lanes.empty() &&
           std::all_of(lanes.begin(), lanes.end(), [&](const auto &lane) {
               return lane.outcome == ManualLaneOutcome::Complete && lane.result && !lane.error &&
                      !lane.verificationError && lane.origin &&
                      lane.endReason == CaptureEndReason::RangeComplete &&
                      lane.capturedFrames == endFrame - beginFrame &&
                      lane.result->asset.frames == lane.capturedFrames;
           });
}
Session withManualRecording(const Session &s, const ManualRecordedGroup &group, bool partial) {
    require(!group.canceled && (group.complete() || partial),
            "Incomplete manual group requires explicit partial adoption");
    auto copy = s;
    std::size_t added = 0;
    for (const auto &lane : group.lanes)
        if (lane.result && !lane.error && !lane.verificationError &&
            lane.outcome == ManualLaneOutcome::Complete) {
            require(lane.result->spec == lane.spec &&
                        lane.spec.capture.startFrame - lane.spec.inputLatencyFrames ==
                            group.beginFrame &&
                        lane.result->asset.frames == lane.capturedFrames,
                    "Manual group alignment/identity mismatch");
            attachRecording(copy, *lane.result);
            ++added;
        }
    require(added != 0, "Manual group has no verified takes to adopt");
    return copy;
}
struct ManualRecordingRun::State {
    struct Consumer {
        std::unique_ptr<RecordingWorker> worker;
        bool attempted = false, joined = false, constructionFailed = false;
    };
    struct Slot {
        ManualPunchTake *take = nullptr;
        ManualRecordedGroup group;
        std::vector<Consumer> consumers;
        std::size_t pendingStarts = 0;
        bool ready = false;
    };
    std::filesystem::path root;
    Session session;
    std::vector<ManualRecordingArm> arms;
    ManualRecordingOptions options;
    std::unique_ptr<MixPlaybackRun> playback;
    std::unique_ptr<ManualPunchBridge> bridge;
    std::array<std::optional<Slot>, manualPunchSlots> slots;
    SpscQueue<ManualPunchReceipt, manualPunchCommands> replies;
    std::size_t outstanding = 0;
    std::exception_ptr firstError, readerError;
    bool stopped = false, canceled = false;

    State(std::filesystem::path r, const Session &s, MixPlan plan,
          std::vector<ManualRecordingArm> bindings, ManualRecordingOptions o)
        : root(std::move(r)), session(s), arms(std::move(bindings)), options(std::move(o)) {
        validate(session);
        const auto saved = ProjectStore(root).load();
        require(saved.id == s.id && saved.sampleRate == s.sampleRate,
                "Manual recording directory belongs to another project");
        require(!arms.empty() && arms.size() <= 256, "Invalid manual recording arms");
        require(options.capture.startFrame == 0 &&
                    options.capture.sampleRate == session.sampleRate &&
                    options.capture.maximumCallbackFrames >= options.playback.graph.maximumFrames,
                "Invalid manual capture template");
        std::vector<ManualPunchArm> routes;
        for (std::size_t n = 0; n < arms.size(); ++n) {
            auto &writer = arms[n].writer;
            const Frame interval = writer.checkpointFrames ? writer.checkpointFrames : s.sampleRate;
            require(writer.checkpointFrames >= 0 && interval <= Frame(s.sampleRate) * 60 &&
                        writer.firstCheckpointFrames >= 0 &&
                        writer.firstCheckpointFrames <= interval,
                    "Invalid manual writer options");
            if (options.staggerCheckpoints && !writer.firstCheckpointFrames)
                writer.firstCheckpointFrames =
                    std::max<Frame>(1, interval * Frame(n + 1) / Frame(arms.size()));
            routes.push_back(arms[n].binding);
        }
        playback = std::make_unique<MixPlaybackRun>(root, session, std::move(plan),
                                                    options.playback, options.reader);
        bridge = std::make_unique<ManualPunchBridge>(*playback, session, std::move(routes),
                                                     options.nativeInputs, options.backend,
                                                     options.memoryBudgetBytes);
    }
    Slot *find(std::uint64_t id) noexcept {
        for (auto &slot : slots)
            if (slot && slot->group.take == id)
                return &*slot;
        return nullptr;
    }
    void remember(std::exception_ptr error) noexcept {
        if (!firstError)
            firstError = error;
    }
    void collectReplies() noexcept {
        ManualPunchReceipt r;
        while (bridge->acknowledgement(r)) {
            if (r.command.action == ManualPunchAction::In) {
                auto *slot = find(r.command.take);
                if (!slot || !slot->pendingStarts)
                    std::terminate();
                --slot->pendingStarts;
            }
            if (!replies.tryPush(r))
                std::terminate(); // Owner credit retained until application acknowledgement.
        }
    }
    void start(Slot &slot, std::size_t n) {
        auto &consumer = slot.consumers[n];
        auto &lane = slot.group.lanes[n];
        auto &pipe = slot.take->pipe(n);
        const auto resolved = pipe.recordingConfig();
        if (consumer.attempted || !resolved)
            return;
        lane.spec.capture = *resolved;
        consumer.attempted = true;
        const auto extent = slot.take->retiredFrames(n);
        if (extent && !*extent) {
            consumer.joined = true;
            lane.outcome = canceled ? ManualLaneOutcome::Canceled : ManualLaneOutcome::Empty;
            return;
        }
        // A canceled take may have accepted a bounded raw prefix before its
        // first service. Native/audio is now quiescent: drain that prefix with
        // a new worker instead of reclaiming its only copy. Cancellation still
        // marks the result group, so retained media is never silently adopted.
        try {
            consumer.worker =
                std::make_unique<RecordingWorker>(pipe, root, lane.spec, arms[n].writer);
            lane.job = consumer.worker->jobDirectory();
        } catch (...) {
            lane.error = std::current_exception();
            consumer.constructionFailed = consumer.joined = true;
            lane.outcome = ManualLaneOutcome::Failed;
            remember(lane.error);
            bridge->requestFault(DuplexStatus::CaptureFailed);
            // A constructor may leave an independently inspectable journal/job.
            const auto job = root / "media" / ("capture-" + lane.spec.assetId.str());
            std::error_code e;
            if (std::filesystem::is_directory(job, e))
                lane.job = job;
        }
    }
    void join(Slot &slot, std::size_t n, bool blocking) {
        auto &consumer = slot.consumers[n];
        auto &lane = slot.group.lanes[n];
        if (!consumer.worker || (!blocking && !consumer.worker->complete()))
            return;
        try {
            lane.result = consumer.worker->wait();
        } catch (...) {
            lane.error = std::current_exception();
        }
        lane.writtenFrames = consumer.worker->writtenFrames();
        consumer.worker.reset(); // Joins and releases leases before verification/reclamation.
        consumer.joined = true;
        // A zero-frame stopped writer can fail finalize without a disk fault.
        // Defer that classification until audio publishes its retired extent.
        if (lane.error && slot.take->pipe(n).timingOrigin()) {
            remember(lane.error);
            bridge->requestFault(DuplexStatus::CaptureFailed);
        }
    }
    void verify(Slot &slot, std::size_t n) {
        auto &lane = slot.group.lanes[n];
        const auto &consumer = slot.consumers[n];
        auto &pipe = slot.take->pipe(n);
        lane.capturedFrames = *slot.take->retiredFrames(n);
        lane.captureStatus = pipe.status();
        lane.endReason = pipe.endReason();
        lane.origin = pipe.timingOrigin();
        if (canceled)
            lane.outcome = ManualLaneOutcome::Canceled;
        else if (consumer.constructionFailed || (lane.error && lane.capturedFrames)) {
            lane.outcome = ManualLaneOutcome::Failed;
            remember(lane.error);
        } else if (!lane.capturedFrames)
            lane.outcome = ManualLaneOutcome::Empty;
        else
            lane.outcome = ManualLaneOutcome::Complete;
        try {
            if (lane.job) {
                lane.checkpoint = inspectRecording(*lane.job, {}, true);
                require(persistedSpec(lane.checkpoint->spec, lane.spec) &&
                            lane.checkpoint->committedFrames <= lane.capturedFrames &&
                            (lane.checkpoint->timingOrigin == lane.origin ||
                             (!lane.checkpoint->committedFrames && !lane.checkpoint->finalized &&
                              !lane.checkpoint->timingOrigin)),
                        "Manual recording checkpoint identity/extent/origin mismatch");
            }
            if (lane.result) {
                require(lane.origin && lane.checkpoint && lane.checkpoint->finalized &&
                            lane.result->spec == lane.spec &&
                            lane.result->asset.id == lane.spec.assetId &&
                            lane.result->asset.layout == lane.spec.capture.layout &&
                            lane.result->asset.sampleRate == lane.spec.capture.sampleRate &&
                            lane.result->asset.frames == lane.capturedFrames &&
                            lane.checkpoint->committedFrames == lane.capturedFrames &&
                            lane.checkpoint->observedFrames == lane.capturedFrames &&
                            lane.checkpoint->endReason == lane.endReason &&
                            hashMediaFile(*lane.job / "take.wav") == lane.result->asset.sha256,
                        "Manual recording finalized media differs from capture/journal");
            } else if (lane.capturedFrames && !lane.error && !canceled)
                require(false, "Manual recording has captured frames without a result");
        } catch (...) {
            lane.verificationError = std::current_exception();
            remember(lane.verificationError);
            if (!canceled && lane.outcome != ManualLaneOutcome::Empty)
                lane.outcome = ManualLaneOutcome::Failed;
        }
    }
    void service(bool blocking = false) {
        collectReplies();
        for (auto &entry : slots) {
            if (!entry || !entry->take)
                continue;
            auto &slot = *entry;
            if (slot.take->phase() == ManualTakePhase::Prepared) {
                if (!running(bridge->status()) && !slot.pendingStarts) {
                    bridge->releaseTake(*slot.take);
                    entry.reset();
                }
                continue;
            }
            for (std::size_t n = 0; n < arms.size(); ++n) {
                start(slot, n);
                join(slot, n, blocking);
            }
            if (slot.take->phase() != ManualTakePhase::Retired || slot.pendingStarts ||
                !std::all_of(slot.consumers.begin(), slot.consumers.end(),
                             [](const auto &c) { return c.joined; }))
                continue;
            slot.group.beginFrame = *slot.take->startFrame();
            slot.group.endFrame = *slot.take->endFrame();
            slot.group.canceled = canceled;
            for (std::size_t n = 0; n < arms.size(); ++n)
                verify(slot, n);
            bridge->releaseTake(*slot.take); // Every disk consumer joined above.
            slot.take = nullptr;
            slot.ready = true; // Bounded receipt occupies its slot until explicitly taken.
        }
    }
    void finish(bool cancel) {
        if (stopped)
            return;
        canceled = cancel;
        if (cancel)
            for (auto &slot : slots)
                if (slot)
                    for (auto &consumer : slot->consumers)
                        if (consumer.worker)
                            consumer.worker->cancel();
        bridge->requestStop();
        bridge->finishQuiescent(); // Caller has joined the native callback owner.
        try {
            playback->waitReader();
        } catch (...) {
            readerError = std::current_exception();
        }
        service(true);
        stopped = true;
    }
    ~State() {
        if (bridge)
            finish(false);
    }
};
ManualRecordingRun::ManualRecordingRun(std::filesystem::path root, const Session &s, MixPlan plan,
                                       std::vector<ManualRecordingArm> arms,
                                       ManualRecordingOptions o)
    : state_(std::make_unique<State>(std::move(root), s, std::move(plan), std::move(arms),
                                     std::move(o))) {}
ManualRecordingRun::~ManualRecordingRun() = default;
std::uint64_t ManualRecordingRun::prepareTake() {
    auto &s = *state_;
    require(!s.stopped && running(status()), "Manual recording preparation is closed");
    auto slot = std::find_if(s.slots.begin(), s.slots.end(), [](const auto &p) { return !p; });
    require(slot != s.slots.end(), "Manual recording result slots full; consume groups first");
    State::Slot candidate;
    candidate.group.lanes.resize(s.arms.size());
    candidate.consumers.resize(s.arms.size());
    std::set<std::string> identities;
    for (const auto &asset : s.session.assets)
        identities.insert(asset.id.str());
    for (const auto &occupied : s.slots)
        if (occupied)
            for (const auto &lane : occupied->group.lanes)
                identities.insert(lane.spec.assetId.str());
    for (std::size_t n = 0; n < s.arms.size(); ++n) {
        auto &spec = candidate.group.lanes[n].spec;
        require(identities.insert(spec.assetId.str()).second,
                "Manual recording identity collision");
        spec.projectId = s.session.id;
        spec.trackId = s.arms[n].binding.track;
        spec.inputLatencyFrames = s.arms[n].binding.inputLatencyFrames;
    }
    candidate.take = &s.bridge->prepareTake(s.options.capture);
    candidate.group.take = candidate.take->id();
    const auto id = candidate.group.take;
    *slot = std::move(candidate);
    return id;
}
void ManualRecordingRun::abandonTake(std::uint64_t id) {
    auto &s = *state_;
    auto *slot = s.find(id);
    require(slot && slot->take && !slot->pendingStarts &&
                slot->take->phase() == ManualTakePhase::Prepared,
            "Manual recording slot is still referenced");
    s.bridge->releaseTake(*slot->take);
    for (auto &entry : s.slots)
        if (entry && &*entry == slot) {
            entry.reset();
            break;
        }
}
ManualPunchSubmit ManualRecordingRun::submit(ManualPunchCommand c) noexcept {
    auto &s = *state_;
    if (s.outstanding == manualPunchCommands)
        return ManualPunchSubmit::Full;
    auto *slot = c.action == ManualPunchAction::In ? s.find(c.take) : nullptr;
    const auto result = s.bridge->submit(c);
    if (result == ManualPunchSubmit::Accepted) {
        ++s.outstanding;
        if (slot)
            ++slot->pendingStarts;
    }
    return result;
}
bool ManualRecordingRun::acknowledgement(ManualPunchReceipt &r) noexcept {
    if (!state_->replies.tryPop(r))
        return false;
    --state_->outstanding;
    return true;
}
void ManualRecordingRun::service() {
    state_->service();
}
bool ManualRecordingRun::takeGroup(ManualRecordedGroup &group) {
    for (auto &slot : state_->slots)
        if (slot && slot->ready) {
            group = std::move(slot->group);
            slot.reset();
            return true;
        }
    return false;
}
DuplexStatus ManualRecordingRun::process(const DeviceBlockClock &clock,
                                         std::span<const float *const> input,
                                         std::span<float *const> output,
                                         std::uint32_t capacity) noexcept {
    return state_->bridge->process(clock, input, output, capacity);
}
void ManualRecordingRun::requestFault(DuplexStatus s) noexcept {
    state_->bridge->requestFault(s);
}
void ManualRecordingRun::requestStop() noexcept {
    state_->bridge->requestStop();
}
void ManualRecordingRun::stop() {
    state_->finish(false);
}
void ManualRecordingRun::cancel() {
    state_->finish(true);
}
void ManualRecordingRun::checkError() const {
    if (state_->firstError)
        std::rethrow_exception(state_->firstError);
}
void ManualRecordingRun::checkReader() const {
    require(state_->stopped, "Join manual playback before checking reader error");
    if (state_->readerError)
        std::rethrow_exception(state_->readerError);
}
DuplexStatus ManualRecordingRun::status() const noexcept {
    return state_->bridge->status();
}
std::optional<DuplexCallbackFault> ManualRecordingRun::callbackFault() const noexcept {
    return state_->bridge->callbackFault();
}
PreparedMixGraph &ManualRecordingRun::graph() noexcept {
    return state_->playback->graph();
}
Frame ManualRecordingRun::position() const noexcept {
    return state_->playback->position();
}
std::size_t ManualRecordingRun::occupiedSlots() const noexcept {
    return std::count_if(state_->slots.begin(), state_->slots.end(),
                         [](const auto &s) { return bool(s); });
}
} // namespace soundcurrent::daw
