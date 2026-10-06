// SPDX-License-Identifier: GPL-3.0-only
#include <soundcurrent/pipewire_recording.hpp>
#include <algorithm>
#include <exception>
namespace soundcurrent::daw {
namespace {
RecordingSpec checked(const Session &session, RecordingSpec spec) {
    validate(session);
    spec.capture = prepareCaptureConfig(spec.capture);
    auto track = std::find_if(session.tracks.begin(), session.tracks.end(),
                              [&](const auto &t) { return t.id == spec.trackId; });
    if (spec.capture.deferredStart || session.id != spec.projectId ||
        session.sampleRate != spec.capture.sampleRate || track == session.tracks.end() ||
        track->layout != spec.capture.layout || spec.inputLatencyFrames < 0 ||
        spec.inputLatencyFrames > Frame(session.sampleRate) * 60 ||
        std::any_of(session.assets.begin(), session.assets.end(),
                    [&](const auto &a) { return a.id == spec.assetId; }))
        throw ProjectError(ErrorCode::InvalidState,
                           "Recording project/track/specification mismatch");
    return spec;
}
bool active(AudioBridgeStatus status) noexcept {
    return status == AudioBridgeStatus::Ready || status == AudioBridgeStatus::Running;
}
} // namespace
struct PipeWireRecording::State {
    std::filesystem::path root;
    RecordingSpec spec;
    PipeWireRecordingOptions options;
    CapturePipe pipe;
    AudioBridge bridge;
    std::vector<float> discarded;
    std::array<float *, 256> discardViews{};
    std::unique_ptr<PipeWireFilter> filter;
    std::unique_ptr<RecordingWorker> writer;
    std::optional<RecordingResult> result;
    std::exception_ptr error;
    bool inputRouted = false, outputRouted = false, activated = false, stopped = false;
    State(std::filesystem::path r, const Session &s, RecordingSpec specValue,
          PipeWireRecordingOptions o)
        : root(std::move(r)), spec(checked(s, std::move(specValue))), options(std::move(o)),
          pipe(spec.capture), bridge(s, spec.trackId, pipe, nativeOptions(options.bridge)) {
        if (!validRecordingMonitor(options.monitoring))
            throw ProjectError(ErrorCode::InvalidState, "Unknown recording monitor mode");
        if (options.writer.checkpointFrames < 0 ||
            options.writer.checkpointFrames > Frame(s.sampleRate) * 60)
            throw ProjectError(ErrorCode::InvalidState,
                               "Recording checkpoint interval is out of range");
        const auto persisted = ProjectStore(root).load();
        if (persisted.id != s.id || persisted.sampleRate != s.sampleRate)
            throw ProjectError(ErrorCode::InvalidState,
                               "Recording directory belongs to another project");
        if (options.monitoring == RecordingMonitor::Off) {
            discarded.resize(std::size_t(bridge.prepared().channels()) *
                             options.bridge.maximumFrames);
            for (std::uint32_t c = 0; c < bridge.prepared().channels(); ++c)
                discardViews[c] = discarded.data() + std::size_t(c) * options.bridge.maximumFrames;
        }
    }
    static AudioBridgeOptions nativeOptions(AudioBridgeOptions o) {
        o.backend = CaptureBackend::PipeWire;
        return o;
    }
    static void process(void *context, const DeviceBlockClock &clock,
                        std::span<const float *const> input, std::span<float *const> output,
                        std::uint32_t capacity) noexcept {
        auto &s = *static_cast<State *>(context);
        if (s.options.monitoring == RecordingMonitor::Off)
            s.bridge.process(clock, input, {s.discardViews.data(), s.bridge.prepared().channels()},
                             std::min(capacity, s.options.bridge.maximumFrames));
        else
            s.bridge.process(clock, input, output, capacity);
    }
    static void unavailable(void *context, AudioBridgeStatus reason) noexcept {
        static_cast<State *>(context)->bridge.requestFault(reason);
    }
    static void begin(void *context) noexcept {
        auto &audit = static_cast<State *>(context)->options.audit;
        if (audit.begin)
            audit.begin(audit.context);
    }
    static void end(void *context) noexcept {
        auto &audit = static_cast<State *>(context)->options.audit;
        if (audit.end)
            audit.end(audit.context);
    }
    void stop(bool cancel) noexcept {
        if (stopped)
            return;
        if (pipe.status() == CaptureStatus::WriterFailed)
            bridge.requestFault(AudioBridgeStatus::CaptureFailed);
        bridge.requestStop();
        if (filter)
            filter->stop();
        bridge.finishQuiescent();
        if (writer) {
            if (cancel)
                writer->cancel();
            try {
                result = writer->wait();
            } catch (...) {
                error = std::current_exception();
            }
        }
        stopped = true;
    }
    ~State() {
        stop(false);
    }
};
PipeWireRecording::PipeWireRecording(std::filesystem::path root, const Session &session,
                                     RecordingSpec spec, PipeWireRecordingOptions options)
    : state_(
          std::make_unique<State>(std::move(root), session, std::move(spec), std::move(options))) {
    auto &s = *state_;
    const auto channels = s.spec.capture.layout.channels;
    s.filter = std::make_unique<PipeWireFilter>(
        PipeWireFilterOptions{"sc-daw-recording-" + Id::generate().str(), channels,
                              s.options.monitoring == RecordingMonitor::Off ? 0 : channels},
        PipeWireCallbacks{&s, State::process, State::unavailable, State::begin, State::end});
    if (!s.filter->waitReady(s.options.readyTimeout))
        throw ProjectError(ErrorCode::Io, "PipeWire recording ports are not ready");
}
PipeWireRecording::~PipeWireRecording() = default;
std::vector<PipeWirePort> PipeWireRecording::ports() const {
    return state_->filter->ports();
}
void PipeWireRecording::connectInputs(const std::vector<PipeWirePort> &ports) {
    if (state_->stopped || state_->activated)
        throw ProjectError(ErrorCode::InvalidState, "Recording route is closed");
    state_->filter->connectInputs(ports);
    state_->inputRouted = true;
}
void PipeWireRecording::connectOutputs(const std::vector<PipeWirePort> &ports) {
    if (state_->stopped || state_->activated || state_->options.monitoring == RecordingMonitor::Off)
        throw ProjectError(ErrorCode::InvalidState, "Recording monitor route is unavailable");
    state_->filter->connectOutputs(ports);
    state_->outputRouted = true;
}
void PipeWireRecording::activate() {
    auto &s = *state_;
    if (s.stopped || s.activated || !active(s.bridge.status()) || !s.inputRouted ||
        (s.options.monitoring != RecordingMonitor::Off && !s.outputRouted))
        throw ProjectError(
            ErrorCode::InvalidState,
            "Select recording input and required monitoring outputs before activation");
    try {
        s.writer = std::make_unique<RecordingWorker>(s.pipe, s.root, s.spec, s.options.writer);
        s.filter->activate();
        s.activated = true;
    } catch (...) {
        const auto activationError = std::current_exception();
        s.bridge.requestFault(s.writer ? AudioBridgeStatus::DeviceLost
                                       : AudioBridgeStatus::CaptureFailed);
        s.stop(false);
        s.error = activationError;
        throw;
    }
}
void PipeWireRecording::stop() noexcept {
    state_->stop(false);
}
void PipeWireRecording::cancel() noexcept {
    state_->stop(true);
}
const RecordingResult &PipeWireRecording::result() const {
    if (!state_->stopped)
        throw ProjectError(ErrorCode::InvalidState, "Stop recording before retrieving its result");
    if (state_->error)
        std::rethrow_exception(state_->error);
    if (!state_->result)
        throw ProjectError(ErrorCode::InvalidState, "Recording was never activated");
    return *state_->result;
}
const RecordingSpec &PipeWireRecording::spec() const noexcept {
    return state_->spec;
}
std::optional<std::filesystem::path> PipeWireRecording::jobDirectory() const {
    if (!state_->writer)
        return {};
    return state_->writer->jobDirectory();
}
bool PipeWireRecording::writerComplete() const noexcept {
    return state_->writer && state_->writer->complete();
}
Frame PipeWireRecording::writtenFrames() const noexcept {
    return state_->writer ? state_->writer->writtenFrames() : 0;
}
AudioBridgeStatus PipeWireRecording::status() const noexcept {
    if (state_->pipe.status() == CaptureStatus::WriterFailed)
        state_->bridge.requestFault(AudioBridgeStatus::CaptureFailed);
    return state_->bridge.status();
}
Frame PipeWireRecording::capturedFrames() const noexcept {
    return state_->bridge.capturedFrames();
}
CaptureStatus PipeWireRecording::captureStatus() const noexcept {
    return state_->pipe.status();
}
std::uint64_t PipeWireRecording::rejectedFrames() const noexcept {
    return state_->pipe.rejectedFrames();
}
std::uint64_t PipeWireRecording::invalidInputSamples() const noexcept {
    return state_->pipe.invalidInputSamples();
}
std::optional<CaptureTimingOrigin> PipeWireRecording::timingOrigin() const noexcept {
    return state_->pipe.timingOrigin();
}
CaptureEndReason PipeWireRecording::endReason() const noexcept {
    return state_->pipe.endReason();
}
std::uint32_t PipeWireRecording::nodeId() const noexcept {
    return state_->filter->nodeId();
}
bool PipeWireRecording::memoryLocked() const noexcept {
    return state_->filter->memoryLocked();
}
PreparedEq &PipeWireRecording::prepared() noexcept {
    return state_->bridge.prepared();
}
SubmitStatus PipeWireRecording::submitImmediate(const EqEvent &event,
                                                std::uint64_t revision) noexcept {
    if (state_->stopped || !active(status()))
        return SubmitStatus::Invalid;
    return state_->bridge.submitImmediate(event, revision);
}
bool PipeWireRecording::acknowledgement(ImmediateAcknowledgement &receipt) noexcept {
    return state_->bridge.acknowledgement(receipt);
}
std::uint64_t PipeWireRecording::droppedAcknowledgements() const noexcept {
    return state_->bridge.droppedAcknowledgements();
}
bool PipeWireRecording::observation(BackendObservation &observation) noexcept {
    return state_->bridge.observation(observation);
}
std::uint64_t PipeWireRecording::droppedObservations() const noexcept {
    return state_->bridge.droppedObservations();
}
} // namespace soundcurrent::daw
