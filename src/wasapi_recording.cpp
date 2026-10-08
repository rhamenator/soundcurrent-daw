// SPDX-License-Identifier: GPL-3.0-only
#include <soundcurrent/wasapi_recording.hpp>
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
struct WasapiRecording::State {
    std::filesystem::path root;
    RecordingSpec spec;
    PipeWireRecordingOptions options;
    CapturePipe pipe;
    AudioBridge bridge;
    std::unique_ptr<PreparedWasapiInput> input;
    std::unique_ptr<WasapiCaptureStream> stream;
    std::unique_ptr<RecordingWorker> writer;
    std::optional<RecordingResult> result;
    std::exception_ptr error;
    std::exception_ptr faultStorageError;
    bool inputRouted = false, activated = false, stopped = false;
    State(std::filesystem::path r, const Session &s, RecordingSpec specValue,
          PipeWireRecordingOptions o)
        : root(std::move(r)), spec(checked(s, std::move(specValue))), options(std::move(o)),
          pipe(spec.capture, options.bridge.resources),
          bridge(s, spec.trackId, pipe, nativeOptions(options.bridge)) {
        if (!options.writer.resources)
            options.writer.resources = options.bridge.resources;
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
        if (options.monitoring != RecordingMonitor::Off)
            throw ProjectError(ErrorCode::InvalidState,
                               "Windows monitoring is not available in this preview; choose Off");
    }
    static AudioBridgeOptions nativeOptions(AudioBridgeOptions o) {
        o.backend = CaptureBackend::Wasapi;
        return o;
    }
    static void packet(void *context, const WasapiPacket &packet) noexcept {
        auto &s = *static_cast<State *>(context);
        if (s.options.audit.begin) s.options.audit.begin(s.options.audit.context);
        s.input->consume(packet);
        if (s.options.audit.end) s.options.audit.end(s.options.audit.context);
    }
    static void unavailable(void *context, std::int32_t) noexcept {
        static_cast<State *>(context)->bridge.requestFault(AudioBridgeStatus::DeviceLost);
    }
    void stop(bool cancel) noexcept {
        if (stopped)
            return;
        if (pipe.status() == CaptureStatus::WriterFailed)
            bridge.requestFault(AudioBridgeStatus::CaptureFailed);
        bridge.requestStop();
        if (stream)
            stream->stop();
        bridge.finishQuiescent();
        if (writer) {
            if (cancel)
                writer->cancel();
            try {
                result = writer->wait();
            } catch (...) {
                error = std::current_exception();
            }
            if (const auto fault = bridge.firstFault()) {
                try {
                    persistRecordingFault(writer->jobDirectory(), spec, *fault);
                } catch (...) {
                    faultStorageError = std::current_exception();
                }
            }
        }
        stopped = true;
    }
    ~State() {
        stop(false);
    }
};
WasapiRecording::WasapiRecording(std::filesystem::path root, const Session &session,
                                     RecordingSpec spec, PipeWireRecordingOptions options)
    : state_(
          std::make_unique<State>(std::move(root), session, std::move(spec), std::move(options))) {}

WasapiRecording::~WasapiRecording() = default;
std::string WasapiRecording::faultStorageDiagnostic() const {
    if (!state_->faultStorageError)
        return {};
    try {
        std::rethrow_exception(state_->faultStorageError);
    } catch (const std::exception &e) {
        return e.what();
    } catch (...) {
        return "Unknown recording fault storage error";
    }
}
std::vector<PipeWirePort> WasapiRecording::ports() const {
    return describeWasapiPorts(wasapiEndpoints(), true, false);
}
void WasapiRecording::connectInputs(const std::vector<PipeWirePort> &ports) {
    auto &s = *state_;
    if (s.stopped || s.activated)
        throw ProjectError(ErrorCode::InvalidState, "Recording route is closed");
    const auto current = this->ports();
    const auto selected = selectWasapiPorts(ports, current, s.spec.capture.layout.channels,
                                            s.spec.capture.sampleRate, false);
    // Retire an older inactive stream before replacing callback context. No
    // job has been created and no packets can have been processed yet.
    if (s.stream) s.stream->stop();
    s.stream.reset(); s.input.reset(); s.inputRouted = false;
    auto input = std::make_unique<PreparedWasapiInput>(s.bridge,
        WasapiInputConfig{selected.nativeChannels,
            std::min<std::uint32_t>(65536, s.options.bridge.maximumFrames * 16), 1,
            selected.channels, s.options.bridge.resources});
    s.input = std::move(input);
    auto stream = std::make_unique<WasapiCaptureStream>(
        WasapiCaptureOptions{selected.endpointId, selected.sampleRate, selected.nativeChannels,
            std::min<std::uint32_t>(65536, s.options.bridge.maximumFrames * 16), selected.loopback,
            nullptr, s.options.bridge.resources},
        WasapiCaptureCallbacks{&s, State::packet, State::unavailable});
    s.stream = std::move(stream); s.inputRouted = true;
}
void WasapiRecording::connectOutputs(const std::vector<PipeWirePort> &ports) {
    if (state_->stopped || state_->activated || !ports.empty())
        throw ProjectError(ErrorCode::InvalidState, "Windows monitoring is not available in this preview");
}
void WasapiRecording::activate() {
    auto &s = *state_;
    if (s.stopped || s.activated || !active(s.bridge.status()) || !s.inputRouted)
        throw ProjectError(
            ErrorCode::InvalidState,
            "Select recording input and required monitoring outputs before activation");
    try {
        s.writer = std::make_unique<RecordingWorker>(s.pipe, s.root, s.spec, s.options.writer);
        s.stream->activate();
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
void WasapiRecording::stop() noexcept {
    state_->stop(false);
}
void WasapiRecording::cancel() noexcept {
    state_->stop(true);
}
const RecordingResult &WasapiRecording::result() const {
    if (!state_->stopped)
        throw ProjectError(ErrorCode::InvalidState, "Stop recording before retrieving its result");
    if (state_->error)
        std::rethrow_exception(state_->error);
    if (!state_->result)
        throw ProjectError(ErrorCode::InvalidState, "Recording was never activated");
    return *state_->result;
}
const RecordingSpec &WasapiRecording::spec() const noexcept {
    return state_->spec;
}
std::optional<std::filesystem::path> WasapiRecording::jobDirectory() const {
    if (!state_->writer)
        return {};
    return state_->writer->jobDirectory();
}
bool WasapiRecording::writerComplete() const noexcept {
    return state_->writer && state_->writer->complete();
}
Frame WasapiRecording::writtenFrames() const noexcept {
    return state_->writer ? state_->writer->writtenFrames() : 0;
}
AudioBridgeStatus WasapiRecording::status() const noexcept {
    if (state_->pipe.status() == CaptureStatus::WriterFailed)
        state_->bridge.requestFault(AudioBridgeStatus::CaptureFailed);
    return state_->bridge.status();
}
Frame WasapiRecording::capturedFrames() const noexcept {
    return state_->bridge.capturedFrames();
}
CaptureStatus WasapiRecording::captureStatus() const noexcept {
    return state_->pipe.status();
}
std::uint64_t WasapiRecording::rejectedFrames() const noexcept {
    return state_->pipe.rejectedFrames();
}
std::uint64_t WasapiRecording::invalidInputSamples() const noexcept {
    return state_->pipe.invalidInputSamples();
}
std::optional<CaptureTimingOrigin> WasapiRecording::timingOrigin() const noexcept {
    return state_->pipe.timingOrigin();
}
CaptureEndReason WasapiRecording::endReason() const noexcept {
    return state_->pipe.endReason();
}
PreparedEq &WasapiRecording::prepared() noexcept {
    return state_->bridge.prepared();
}
SubmitStatus WasapiRecording::submitImmediate(const EqEvent &event,
                                                std::uint64_t revision) noexcept {
    if (state_->stopped || !active(status()))
        return SubmitStatus::Invalid;
    return state_->bridge.submitImmediate(event, revision);
}
bool WasapiRecording::acknowledgement(ImmediateAcknowledgement &receipt) noexcept {
    return state_->bridge.acknowledgement(receipt);
}
std::uint64_t WasapiRecording::droppedAcknowledgements() const noexcept {
    return state_->bridge.droppedAcknowledgements();
}
bool WasapiRecording::observation(BackendObservation &observation) noexcept {
    return state_->bridge.observation(observation);
}
std::optional<AudioBridgeFault> WasapiRecording::firstFault() const noexcept {
    return state_->bridge.firstFault();
}
std::uint64_t WasapiRecording::droppedObservations() const noexcept {
    return state_->bridge.droppedObservations();
}
} // namespace soundcurrent::daw
