// SPDX-License-Identifier: GPL-3.0-only
#include <soundcurrent/wasapi_playback.hpp>
namespace soundcurrent::daw {
namespace {
bool active(PlaybackBridgeStatus status) noexcept {
    return status == PlaybackBridgeStatus::Ready || status == PlaybackBridgeStatus::Running ||
           status == PlaybackBridgeStatus::Underflow;
}
PlaybackBridgeStatus converted(PlaybackStatus status) noexcept {
    switch (status) {
    case PlaybackStatus::Running: return PlaybackBridgeStatus::Running;
    case PlaybackStatus::Underflow: return PlaybackBridgeStatus::Underflow;
    case PlaybackStatus::Complete: return PlaybackBridgeStatus::Running; // Native tail still queued.
    case PlaybackStatus::Stopped: return PlaybackBridgeStatus::Stopped;
    case PlaybackStatus::ReaderFailed: return PlaybackBridgeStatus::ReaderFailed;
    case PlaybackStatus::InvalidBuffer: return PlaybackBridgeStatus::BufferUnavailable;
    case PlaybackStatus::TimingError: return PlaybackBridgeStatus::ClockDiscontinuity;
    case PlaybackStatus::ProcessorFailed: return PlaybackBridgeStatus::ProcessorFailed;
    }
    return PlaybackBridgeStatus::ProcessorFailed;
}
}
struct WasapiPlayback::State {
    MixPlaybackRun run;
    PreparedWasapiOutput output;
    PlaybackCallbackInstrumentation audit;
    std::unique_ptr<WasapiRenderStream> stream;
    std::exception_ptr readerError;
    SpscQueue<WasapiPlaybackObservation, 64> observations;
    std::atomic<std::uint64_t> dropped{0};
    std::atomic<PlaybackBridgeStatus> phase{PlaybackBridgeStatus::Ready};
    std::atomic<bool> complete{false};
    bool stopped = false;
    State(std::filesystem::path root, const Session &s, MixPlan plan, MixPlaybackConfig config,
          WasapiOutputConfig out, ReadAheadOptions reader, PlaybackCallbackInstrumentation a)
        : run(std::move(root), s, std::move(plan), config, std::move(reader)),
          output(run, std::move(out)), audit(a) {}
    static WasapiRenderAction fill(void *p, float *data, std::uint32_t frames,
                                   const WasapiRenderClock &clock) noexcept {
        auto &s = *static_cast<State *>(p);
        if (!active(s.phase.load(std::memory_order_acquire))) return WasapiRenderAction::Abort;
        if (s.audit.begin) s.audit.begin(s.audit.context);
        const auto report = s.output.process({data, std::size_t(frames) * s.nativeChannels}, frames);
        const auto next = converted(report.status);
        auto prior = s.phase.load(std::memory_order_acquire);
        if (active(prior)) s.phase.compare_exchange_strong(prior, next, std::memory_order_acq_rel);
        if (!s.observations.tryPush({clock, report})) s.dropped.fetch_add(1, std::memory_order_relaxed);
        if (s.audit.end) s.audit.end(s.audit.context);
        if (!active(s.phase.load(std::memory_order_acquire))) return WasapiRenderAction::Abort;
        if (report.status == PlaybackStatus::Complete) {
            s.complete.store(true, std::memory_order_release);
            return WasapiRenderAction::Finish;
        }
        return WasapiRenderAction::Continue;
    }
    std::uint32_t nativeChannels = 0;
    static void unavailable(void *p, std::int32_t) noexcept {
        auto &s = *static_cast<State *>(p);
        auto prior = s.phase.load(std::memory_order_acquire);
        if (active(prior)) s.phase.compare_exchange_strong(prior, PlaybackBridgeStatus::DeviceLost,
                                                          std::memory_order_acq_rel);
        s.run.requestStop();
    }
    PlaybackBridgeStatus status() const noexcept {
        const auto current = phase.load(std::memory_order_acquire);
        if (active(current) && complete.load(std::memory_order_acquire) && stream && stream->drained())
            return PlaybackBridgeStatus::Complete;
        return current;
    }
    void stop() noexcept {
        if (stopped) return;
        const auto before = status();
        if (before == PlaybackBridgeStatus::Complete) phase.store(before, std::memory_order_release);
        else {
            auto prior = phase.load(std::memory_order_acquire);
            while (active(prior) && !phase.compare_exchange_weak(prior, PlaybackBridgeStatus::Stopped,
                                                                std::memory_order_acq_rel)) {}
        }
        run.requestStop();
        if (stream) stream->stop();
        run.cancelReader();
        try { run.waitReader(); } catch (...) { readerError = std::current_exception(); }
        stopped = true;
    }
    ~State() { stop(); }
};
WasapiPlayback::WasapiPlayback(std::filesystem::path root, const Session &s, MixPlan plan,
                               MixPlaybackConfig config, WasapiRenderOptions native,
                               WasapiOutputConfig out, ReadAheadOptions reader,
                               PlaybackCallbackInstrumentation audit) {
    if (native.sampleRate != s.sampleRate || native.maximumFrames != config.graph.maximumFrames ||
        native.channels != out.nativeChannels)
        throw ProjectError(ErrorCode::InvalidState, "Playback engine/native preparation mismatch");
    state_ = std::make_unique<State>(std::move(root), s, std::move(plan), config,
                                    std::move(out), std::move(reader), audit);
    state_->nativeChannels = native.channels;
    state_->stream = std::make_unique<WasapiRenderStream>(std::move(native),
        WasapiRenderCallbacks{state_.get(), State::fill, State::unavailable});
}
WasapiPlayback::~WasapiPlayback() = default;
void WasapiPlayback::activate() {
    if (state_->stopped) throw ProjectError(ErrorCode::InvalidState, "Playback owner has stopped");
    state_->stream->activate();
}
void WasapiPlayback::stop() noexcept { state_->stop(); }
void WasapiPlayback::checkReader() {
    if (!state_->stopped) throw ProjectError(ErrorCode::InvalidState, "Stop playback before checking reader");
    if (state_->readerError) std::rethrow_exception(state_->readerError);
}
PreparedMixGraph &WasapiPlayback::graph() noexcept { return state_->run.graph(); }
SubmitStatus WasapiPlayback::submitImmediate(const MixEvent &e, std::uint64_t revision) noexcept {
    if (state_->stopped || !active(state_->status()) || state_->complete.load(std::memory_order_acquire))
        return SubmitStatus::Invalid;
    return graph().submitImmediate(e, revision);
}
bool WasapiPlayback::acknowledgement(std::size_t track, ImmediateAcknowledgement &a) noexcept {
    return graph().acknowledgement(track, a);
}
std::uint64_t WasapiPlayback::droppedAcknowledgements() const noexcept { return state_->run.graph().droppedAcknowledgements(); }
PlaybackBridgeStatus WasapiPlayback::status() const noexcept { return state_->status(); }
Frame WasapiPlayback::position() const noexcept { return state_->run.position(); }
bool WasapiPlayback::observation(WasapiPlaybackObservation &o) noexcept { return state_->observations.tryPop(o); }
std::uint64_t WasapiPlayback::droppedObservations() const noexcept { return state_->dropped.load(std::memory_order_relaxed); }
std::uint64_t WasapiPlayback::missingFrames() const noexcept { return state_->run.missingTrackFrames(); }
bool WasapiPlayback::drained() const noexcept { return state_->stream->drained(); }
std::uint64_t WasapiPlayback::submittedFrames() const noexcept { return state_->stream->submittedFrames(); }
std::uint64_t WasapiPlayback::emptyQueueObservations() const noexcept { return state_->stream->emptyQueueObservations(); }
std::uint32_t WasapiPlayback::bufferFrames() const noexcept { return state_->stream->bufferFrames(); }
std::optional<WasapiStreamFailure> WasapiPlayback::failure() const noexcept { return state_->stream->failure(); }
} // namespace soundcurrent::daw
