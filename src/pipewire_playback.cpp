// SPDX-License-Identifier: GPL-3.0-only
#include <soundcurrent/pipewire_playback.hpp>

namespace soundcurrent::daw {
struct PipeWirePlayback::State {
    MixPlaybackRun run;
    PlaybackBridge bridge;
    std::unique_ptr<PipeWireFilter> filter;
    std::exception_ptr readerError;
    PlaybackCallbackInstrumentation audit;
    bool stopped = false, routed = false;
    State(std::filesystem::path root, const Session &s, MixPlan plan, MixPlaybackConfig config,
          ReadAheadOptions options)
        : run(std::move(root), s, std::move(plan), config, std::move(options)),
          bridge(run, CaptureBackend::PipeWire) {}
    static void process(void *p, const DeviceBlockClock &clock, std::span<const float *const>,
                        std::span<float *const> out, std::uint32_t n) noexcept {
        static_cast<State *>(p)->bridge.process(clock, out, n);
    }
    static void begin(void *p) noexcept {
        auto &a = static_cast<State *>(p)->audit;
        if (a.begin)
            a.begin(a.context);
    }
    static void end(void *p) noexcept {
        auto &a = static_cast<State *>(p)->audit;
        if (a.end)
            a.end(a.context);
    }
    static void unavailable(void *p, AudioBridgeStatus reason) noexcept {
        static_cast<State *>(p)->bridge.requestFault(reason == AudioBridgeStatus::QuantumExceeded
                                                         ? PlaybackBridgeStatus::QuantumExceeded
                                                         : PlaybackBridgeStatus::DeviceLost);
    }
    void stop() noexcept {
        if (stopped)
            return;
        bridge.requestStop();
        if (filter)
            filter->stop();
        bridge.finishQuiescent();
        run.cancelReader();
        try {
            run.waitReader();
        } catch (...) {
            readerError = std::current_exception();
        }
        stopped = true;
    }
    ~State() {
        stop();
    }
};
namespace {
MixPlan singlePlan(const Session &s, const Id &track, const PlaybackConfig &c) {
    if (c.sampleRate != s.sampleRate)
        throw ProjectError(ErrorCode::InvalidState, "Playback rate differs from the project");
    return identityMix(s, std::span(&track, 1), c.layout);
}
MixPlaybackConfig singleConfig(const PlaybackConfig &c) {
    const auto prepared = preparePlaybackConfig(c);
    return {{prepared.maximumCallbackFrames, prepared.startFrame, prepared.generation,
             prepared.memoryBudgetBytes},
            prepared.endFrame,
            prepared.slabFrames};
}
} // namespace
PipeWirePlayback::PipeWirePlayback(std::filesystem::path root, const Session &s, const Id &track,
                                   PlaybackConfig config, ReadAheadOptions options,
                                   std::chrono::milliseconds timeout,
                                   PlaybackCallbackInstrumentation audit)
    : PipeWirePlayback(std::move(root), s, singlePlan(s, track, config), singleConfig(config),
                       std::move(options), timeout, audit) {}
PipeWirePlayback::PipeWirePlayback(std::filesystem::path root, const Session &s, MixPlan plan,
                                   MixPlaybackConfig config, ReadAheadOptions options,
                                   std::chrono::milliseconds timeout,
                                   PlaybackCallbackInstrumentation audit)
    : state_(std::make_unique<State>(std::move(root), s, std::move(plan), config,
                                     std::move(options))) {
    state_->audit = audit;
    const auto channels = state_->run.graph().plan().output.channels;
    state_->filter = std::make_unique<PipeWireFilter>(
        PipeWireFilterOptions{"sc-daw-playback-" + Id::generate().str(), 0,
                              static_cast<std::uint32_t>(channels)},
        PipeWireCallbacks{state_.get(), State::process, State::unavailable, State::begin,
                          State::end});
    if (!state_->filter->waitReady(timeout))
        throw ProjectError(ErrorCode::Io, "PipeWire playback ports are not ready");
}
PipeWirePlayback::~PipeWirePlayback() = default;
std::vector<PipeWirePort> PipeWirePlayback::ports() const {
    return state_->filter->ports();
}
void PipeWirePlayback::connectOutputs(const std::vector<PipeWirePort> &ports) {
    if (state_->stopped)
        throw ProjectError(ErrorCode::InvalidState, "Playback owner has stopped");
    state_->filter->connectOutputs(ports);
    state_->routed = true;
}
void PipeWirePlayback::activate() {
    if (state_->stopped)
        throw ProjectError(ErrorCode::InvalidState, "Playback owner has stopped");
    if (!state_->routed)
        throw ProjectError(ErrorCode::InvalidState, "Select playback outputs before activation");
    state_->filter->activate();
}
void PipeWirePlayback::stop() noexcept {
    state_->stop();
}
void PipeWirePlayback::checkReader() {
    if (!state_->stopped)
        throw ProjectError(ErrorCode::InvalidState, "Stop playback before checking its reader");
    if (state_->readerError)
        std::rethrow_exception(state_->readerError);
}
std::uint32_t PipeWirePlayback::nodeId() const noexcept {
    return state_->filter->nodeId();
}
bool PipeWirePlayback::memoryLocked() const noexcept {
    return state_->filter->memoryLocked();
}
PreparedEq &PipeWirePlayback::prepared() noexcept {
    return state_->run.graph().prepared(0);
}
SubmitStatus PipeWirePlayback::submitImmediate(const EqEvent &e, std::uint64_t revision) noexcept {
    return submitImmediate(MixEvent{0, e}, revision);
}
PreparedMixGraph &PipeWirePlayback::graph() noexcept {
    return state_->run.graph();
}
SubmitStatus PipeWirePlayback::submitImmediate(const MixEvent &e, std::uint64_t revision) noexcept {
    const auto status = state_->bridge.status();
    if (state_->stopped ||
        (status != PlaybackBridgeStatus::Ready && status != PlaybackBridgeStatus::Running &&
         status != PlaybackBridgeStatus::Underflow))
        return SubmitStatus::Invalid;
    return state_->run.graph().submitImmediate(e, revision);
}
bool PipeWirePlayback::acknowledgement(ImmediateAcknowledgement &a) noexcept {
    return acknowledgement(0, a);
}
bool PipeWirePlayback::acknowledgement(std::size_t track, ImmediateAcknowledgement &a) noexcept {
    return state_->run.graph().acknowledgement(track, a);
}
std::uint64_t PipeWirePlayback::droppedAcknowledgements() const noexcept {
    return state_->run.graph().droppedAcknowledgements();
}
PlaybackBridgeStatus PipeWirePlayback::status() const noexcept {
    return state_->bridge.status();
}
Frame PipeWirePlayback::position() const noexcept {
    return state_->run.position();
}
std::uint64_t PipeWirePlayback::missingFrames() const noexcept {
    return state_->run.missingTrackFrames();
}
bool PipeWirePlayback::observation(PlaybackObservation &o) noexcept {
    return state_->bridge.observation(o);
}
std::uint64_t PipeWirePlayback::droppedObservations() const noexcept {
    return state_->bridge.droppedObservations();
}
std::optional<CaptureTimingOrigin> PipeWirePlayback::timingOrigin() const noexcept {
    return state_->bridge.timingOrigin();
}
std::optional<PlaybackCallbackFault> PipeWirePlayback::callbackFault() const noexcept {
    return state_->bridge.callbackFault();
}
} // namespace soundcurrent::daw
