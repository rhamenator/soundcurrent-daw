// SPDX-License-Identifier: GPL-3.0-only
#include <soundcurrent/duplex_recording.hpp>
#include <algorithm>
#include <exception>
#include <limits>
#include <set>

namespace soundcurrent::daw {
static_assert(std::atomic<bool>::is_always_lock_free);
MusicalPunchPlan prepareMusicalPunch(PunchRange timeline, std::uint32_t rate,
                                     std::span<const DuplexRecordingLane> arms) {
    if (timeline.begin < 0 || timeline.begin >= timeline.end || rate < 8000 || rate > 384000 ||
        arms.empty() || arms.size() > 256)
        throw ProjectError(ErrorCode::InvalidState, "Invalid musical punch plan");
    MusicalPunchPlan result{timeline, timeline.end, {}};
    std::set<std::string> tracks;
    result.lanes.reserve(arms.size());
    for (const auto &lane : arms) {
        const auto &r = lane.spec;
        const auto latency = r.inputLatencyFrames;
        if (r.capture.sampleRate != rate || latency < 0 || latency > Frame(rate) * 60 ||
            timeline.end > std::numeric_limits<Frame>::max() - latency ||
            !tracks.insert(r.trackId.str()).second)
            throw ProjectError(ErrorCode::InvalidState, "Invalid musical punch lane");
        const PunchRange raw{timeline.begin + latency, timeline.end + latency};
        result.requiredPlaybackEnd = std::max(result.requiredPlaybackEnd, raw.end);
        result.lanes.push_back({r.trackId, latency, raw});
    }
    return result;
}
namespace {
void directoryMatches(const std::filesystem::path &root, const Session &s) {
    const auto persisted = ProjectStore(root).load();
    if (persisted.id != s.id || persisted.sampleRate != s.sampleRate)
        throw ProjectError(ErrorCode::InvalidState,
                           "Recording directory belongs to another project");
}
void admit(const Session &s, const MixPlan &plan, std::vector<DuplexRecordingLane> &lanes,
           const DuplexRecordingOptions &o, std::span<const PreparedPunchLane> musical) {
    validate(s);
    if (o.punch && (o.punch->begin < o.playback.graph.startFrame ||
                    o.punch->begin >= o.punch->end || o.punch->end > o.playback.endFrame))
        throw ProjectError(ErrorCode::InvalidState, "Invalid prepared punch range");
    if (lanes.empty() || lanes.size() > 256 || !o.nativeInputs || o.nativeInputs > 256 ||
        o.backend > CaptureBackend::Asio || !o.memoryBudgetBytes ||
        o.memoryBudgetBytes > 256 * 1024 * 1024 ||
        o.playback.graph.memoryBudgetBytes > o.memoryBudgetBytes)
        throw ProjectError(ErrorCode::InvalidState, "Invalid duplex recording admission");
    std::set<std::string> occupied{s.id.str()}, armed;
    if (s.master)
        occupied.insert(s.master->id.str());
    for (const auto &a : s.assets)
        occupied.insert(a.id.str());
    for (const auto &t : s.tracks) {
        occupied.insert(t.id.str());
        occupied.insert(t.eq.id.str());
        for (const auto &b : t.eq.bands)
            occupied.insert(b.id.str());
        for (const auto &c : t.clips)
            occupied.insert(c.id.str());
    }
    auto bytes = o.playback.graph.memoryBudgetBytes;
    std::size_t ordinal = 0;
    for (auto &lane : lanes) {
        auto &r = lane.spec;
        const auto captureStart = musical.empty()
                                      ? (o.punch ? o.punch->begin : o.playback.graph.startFrame)
                                      : musical[ordinal].capture.begin;
        r.capture = prepareCaptureConfig(r.capture);
        const auto t = std::find_if(s.tracks.begin(), s.tracks.end(),
                                    [&](const auto &t) { return t.id == r.trackId; });
        if (r.projectId != s.id || r.capture.sampleRate != s.sampleRate || t == s.tracks.end() ||
            r.capture.layout != t->layout || r.capture.startFrame != captureStart ||
            r.capture.maximumCallbackFrames < o.playback.graph.maximumFrames ||
            r.inputLatencyFrames < 0 || r.inputLatencyFrames > Frame(s.sampleRate) * 60 ||
            r.recoveredFrom || !occupied.insert(r.assetId.str()).second ||
            !armed.insert(r.trackId.str()).second ||
            lane.inputChannels.size() != t->layout.channels ||
            std::any_of(
                lane.inputChannels.begin(), lane.inputChannels.end(),
                [&](auto c) { return c >= o.nativeInputs; }) ||
            !validRecordingMonitor(lane.monitoring) || lane.writer.checkpointFrames < 0 ||
            lane.writer.checkpointFrames > Frame(s.sampleRate) * 60 ||
            lane.writer.firstCheckpointFrames < 0 ||
            lane.writer.firstCheckpointFrames > (lane.writer.checkpointFrames
                                                     ? lane.writer.checkpointFrames
                                                     : Frame(s.sampleRate)) ||
            (lane.monitoring != RecordingMonitor::Off &&
             std::none_of(plan.tracks.begin(), plan.tracks.end(),
                          [&](const auto &p) { return p.track == r.trackId; })))
            throw ProjectError(ErrorCode::InvalidState, "Invalid duplex recording lane");
        if (o.staggerCheckpoints && !lane.writer.firstCheckpointFrames) {
            const auto interval =
                lane.writer.checkpointFrames ? lane.writer.checkpointFrames : Frame(s.sampleRate);
            lane.writer.firstCheckpointFrames =
                std::max<Frame>(1, interval * Frame(ordinal + 1) / Frame(lanes.size()));
        }
        ++ordinal;
        const auto payload = armedCapturePayloadBytes(r.capture, lane.inputChannels.size());
        if (payload > o.memoryBudgetBytes - bytes)
            throw ProjectError(ErrorCode::InvalidState, "Duplex capture payload exceeds budget");
        bytes += payload;
    }
}
} // namespace
struct DuplexRecordingRun::State {
    struct Lane {
        DuplexRecordingLane binding;
        std::unique_ptr<CapturePipe> pipe;
        std::unique_ptr<RecordingWorker> writer;
        std::optional<RecordingResult> result;
        std::optional<std::filesystem::path> job;
        Frame joinedWritten = 0;
        bool joined = false;
        std::exception_ptr error;
        std::optional<PunchRange> range;
        Lane(DuplexRecordingLane b, std::optional<PunchRange> r) : binding(std::move(b)), range(r) {
            pipe = std::make_unique<CapturePipe>(binding.spec.capture);
        }
    };
    std::filesystem::path root;
    Session session;
    std::vector<std::unique_ptr<Lane>> lanes;
    std::unique_ptr<MixPlaybackRun> playback;
    std::unique_ptr<DuplexBridge> bridge;
    std::exception_ptr activationError, readerError;
    std::atomic<bool> writersReady{false};
    bool attempted = false, stopped = false;
    State(std::filesystem::path r, const Session &s, MixPlan plan,
          std::vector<DuplexRecordingLane> arms, DuplexRecordingOptions options)
        : root(std::move(r)), session(s) {
        std::vector<PreparedPunchLane> musical;
        if (options.musicalPunch) {
            if (options.punch || options.musicalPunch->begin < options.playback.graph.startFrame ||
                options.musicalPunch->end > options.playback.endFrame)
                throw ProjectError(ErrorCode::InvalidState, "Conflicting or outside musical punch");
            auto prepared = prepareMusicalPunch(*options.musicalPunch, s.sampleRate, arms);
            options.playback.endFrame =
                std::max(options.playback.endFrame, prepared.requiredPlaybackEnd);
            musical = std::move(prepared.lanes);
            for (std::size_t n = 0; n < arms.size(); ++n)
                arms[n].spec.capture.startFrame = musical[n].capture.begin;
        }
        admit(s, plan, arms, options, musical); // Entire admission BEFORE creating any pool.
        directoryMatches(root, s);
        playback = std::make_unique<MixPlaybackRun>(root, s, std::move(plan), options.playback,
                                                    std::move(options.reader));
        std::vector<ArmedCapture> bindings;
        for (auto &arm : arms) {
            const auto range = musical.empty()
                                   ? options.punch
                                   : std::optional<PunchRange>(musical[lanes.size()].capture);
            auto lane = std::make_unique<Lane>(std::move(arm), range);
            bindings.push_back({lane->binding.spec.trackId, lane->pipe.get(),
                                lane->binding.inputChannels, lane->binding.monitoring,
                                musical.empty() ? std::optional<PunchRange>{} : range,
                                lane->binding.monitoring == RecordingMonitor::AutoRecording
                                    ? options.musicalPunch
                                    : std::optional<PunchRange>{}});
            lanes.push_back(std::move(lane));
        }
        bridge = std::make_unique<DuplexBridge>(*playback, s, std::move(bindings),
                                                options.nativeInputs, options.backend,
                                                options.memoryBudgetBytes, options.punch);
    }
    Lane &lane(std::size_t n) const {
        if (n >= lanes.size())
            throw ProjectError(ErrorCode::InvalidState, "Unknown armed recording lane");
        return *lanes[n];
    }
    void stop(bool cancel) noexcept {
        if (stopped)
            return;
        for (const auto &l : lanes)
            if (l->pipe->status() == CaptureStatus::WriterFailed)
                bridge->requestFault(DuplexStatus::CaptureFailed);
        bridge->requestStop();
        // Publish every cancellation before finishing pipes. A native terminal
        // callback may already have finished them, so the receipt policy below
        // also handles a disk owner that wins that finalization race.
        if (cancel)
            for (const auto &l : lanes)
                if (l->writer)
                    l->writer->cancel();
        bridge->finishQuiescent();
        writersReady.store(false, std::memory_order_release);
        try {
            playback->waitReader();
        } catch (...) {
            readerError = std::current_exception();
        }
        for (auto &l : lanes)
            if (l->writer) {
                try {
                    auto result = l->writer->wait();
                    if (cancel)
                        throw ProjectError(
                            ErrorCode::Canceled,
                            "Recording canceled; completed media/checkpoint retained");
                    l->result = std::move(result);
                } catch (...) {
                    l->error = std::current_exception();
                }
                l->joinedWritten = l->writer->writtenFrames();
                l->joined = true;
                // Canceled workers can retain a file/lease after joining. Retire
                // each now while keeping its immutable receipt/error/job path.
                l->writer.reset();
            }
        stopped = true;
    }
    ~State() {
        // Constructor failure destroys member owners directly; their destructors join.
        if (bridge)
            stop(false);
    }
};
DuplexRecordingRun::DuplexRecordingRun(std::filesystem::path root, const Session &s, MixPlan plan,
                                       std::vector<DuplexRecordingLane> arms,
                                       DuplexRecordingOptions options)
    : state_(std::make_unique<State>(std::move(root), s, std::move(plan), std::move(arms),
                                     std::move(options))) {}
DuplexRecordingRun::~DuplexRecordingRun() = default;
void DuplexRecordingRun::startWriters() {
    auto &s = *state_;
    if (s.attempted || s.stopped || s.bridge->status() != DuplexStatus::Ready)
        throw ProjectError(ErrorCode::InvalidState, "Duplex recording activation is closed");
    s.attempted = true;
    try {
        directoryMatches(s.root, s.session);
        for (auto &l : s.lanes)
            try {
                l->writer = std::make_unique<RecordingWorker>(*l->pipe, s.root, l->binding.spec,
                                                              l->binding.writer);
                l->job = l->writer->jobDirectory();
            } catch (...) {
                l->error = std::current_exception();
                // A constructor may have created an independently inspectable job
                // before journal publication failed. Keep that path without masking
                // the original activation exception; all checks are off RT.
                const auto candidate =
                    s.root / "media" / ("capture-" + l->binding.spec.assetId.str());
                std::error_code directoryError;
                if (std::filesystem::is_directory(candidate, directoryError))
                    l->job = candidate;
                throw;
            }
        s.writersReady.store(true, std::memory_order_release);
    } catch (...) {
        s.activationError = std::current_exception();
        s.bridge->requestFault(DuplexStatus::CaptureFailed);
        s.stop(false); // No callbacks have been activated yet.
        std::rethrow_exception(s.activationError);
    }
}
void DuplexRecordingRun::checkActivation() const {
    if (state_->activationError)
        std::rethrow_exception(state_->activationError);
}
DuplexStatus DuplexRecordingRun::process(const DeviceBlockClock &clock,
                                         std::span<const float *const> input,
                                         std::span<float *const> output,
                                         std::uint32_t capacity) noexcept {
    auto &s = *state_;
    if (!s.writersReady.load(std::memory_order_acquire)) {
        const auto count = std::min<std::uint64_t>(
            {clock.duration, capacity, s.playback->config().graph.maximumFrames});
        for (auto *plane : output)
            if (plane)
                std::fill_n(plane, count, 0.0f);
        return s.bridge->status();
    }
    return s.bridge->process(clock, input, output, capacity);
}
void DuplexRecordingRun::requestFault(DuplexStatus status) noexcept {
    state_->bridge->requestFault(status);
}
void DuplexRecordingRun::requestStop() noexcept {
    state_->bridge->requestStop();
}
void DuplexRecordingRun::stop() noexcept {
    state_->stop(false);
}
void DuplexRecordingRun::cancel() noexcept {
    state_->stop(true);
}
const RecordingResult &DuplexRecordingRun::result(std::size_t n) const {
    const auto &l = state_->lane(n);
    if (!state_->stopped)
        throw ProjectError(ErrorCode::InvalidState, "Stop recording before retrieving takes");
    if (l.error)
        std::rethrow_exception(l.error);
    if (!l.result)
        throw ProjectError(ErrorCode::InvalidState, "Recording lane produced no take");
    return *l.result;
}
const RecordingSpec &DuplexRecordingRun::spec(std::size_t n) const {
    return state_->lane(n).binding.spec;
}
std::optional<PunchRange> DuplexRecordingRun::captureRange(std::size_t n) const {
    return state_->lane(n).range;
}
Frame DuplexRecordingRun::playbackEnd() const noexcept {
    return state_->playback->config().endFrame;
}
std::optional<std::filesystem::path> DuplexRecordingRun::jobDirectory(std::size_t n) const {
    const auto &l = state_->lane(n);
    return l.job;
}
DuplexCaptureSnapshot DuplexRecordingRun::capture(std::size_t n) const {
    const auto &l = state_->lane(n);
    return {state_->bridge->capturedFrames(n),
            l.writer ? l.writer->writtenFrames() : l.joinedWritten,
            l.pipe->status(),
            l.pipe->endReason(),
            l.pipe->rejectedFrames(),
            l.pipe->invalidInputSamples(),
            l.joined || (l.writer && l.writer->complete()),
            l.pipe->timingOrigin()};
}
std::size_t DuplexRecordingRun::lanes() const noexcept {
    return state_->lanes.size();
}
DuplexStatus DuplexRecordingRun::status() const noexcept {
    for (const auto &l : state_->lanes)
        if (l->pipe->status() == CaptureStatus::WriterFailed)
            state_->bridge->requestFault(DuplexStatus::CaptureFailed);
    return state_->bridge->status();
}
Frame DuplexRecordingRun::position() const noexcept {
    return state_->playback->position();
}
std::uint64_t DuplexRecordingRun::missingTrackFrames() const noexcept {
    return state_->playback->missingTrackFrames();
}
std::optional<CaptureTimingOrigin> DuplexRecordingRun::timingOrigin() const noexcept {
    return state_->bridge->timingOrigin();
}
std::optional<DuplexCallbackFault> DuplexRecordingRun::callbackFault() const noexcept {
    return state_->bridge->callbackFault();
}
PreparedMixGraph &DuplexRecordingRun::graph() noexcept {
    return state_->playback->graph();
}
bool DuplexRecordingRun::observation(DuplexObservation &observation) noexcept {
    return state_->bridge->observation(observation);
}
std::uint64_t DuplexRecordingRun::droppedObservations() const noexcept {
    return state_->bridge->droppedObservations();
}
void DuplexRecordingRun::checkReader() const {
    if (!state_->stopped)
        throw ProjectError(ErrorCode::InvalidState, "Join recording before checking reader result");
    if (state_->readerError)
        std::rethrow_exception(state_->readerError);
}
} // namespace soundcurrent::daw
