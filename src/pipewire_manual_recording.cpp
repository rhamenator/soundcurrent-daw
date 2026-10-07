// SPDX-License-Identifier: GPL-3.0-only
#include <soundcurrent/pipewire_manual_recording.hpp>

namespace soundcurrent::daw {
struct PipeWireManualRecording::State {
    ManualRecordingRun run;
    RecordingCallbackInstrumentation audit;
    void (*auditClock)(void *, const DeviceBlockClock &) noexcept;
    std::unique_ptr<PipeWireFilter> filter;
    std::exception_ptr activationError;
    bool inputRouted = false, outputRouted = false, attempted = false, stopped = false;
    static ManualRecordingOptions native(ManualRecordingOptions o) {
        o.backend = CaptureBackend::PipeWire;
        return o;
    }
    State(std::filesystem::path root, const Session &s, MixPlan plan,
          std::vector<ManualRecordingArm> arms, const PipeWireManualRecordingOptions &o)
        : run(std::move(root), s, std::move(plan), std::move(arms), native(o.run)), audit(o.audit),
          auditClock(o.auditClock) {}
    static void process(void *context, const DeviceBlockClock &clock,
                        std::span<const float *const> input, std::span<float *const> output,
                        std::uint32_t capacity) noexcept {
        static_cast<State *>(context)->run.process(clock, input, output, capacity);
    }
    static void unavailable(void *context, AudioBridgeStatus reason) noexcept {
        static_cast<State *>(context)->run.requestFault(reason == AudioBridgeStatus::QuantumExceeded
                                                            ? DuplexStatus::QuantumExceeded
                                                            : DuplexStatus::DeviceLost);
    }
    static void begin(void *context) noexcept {
        const auto &a = static_cast<State *>(context)->audit;
        if (a.begin)
            a.begin(a.context);
    }
    static void end(void *context) noexcept {
        const auto &a = static_cast<State *>(context)->audit;
        if (a.end)
            a.end(a.context);
    }
    static void clock(void *context, const DeviceBlockClock &c) noexcept {
        auto &s = *static_cast<State *>(context);
        if (s.auditClock)
            s.auditClock(s.audit.context, c);
    }
    void finish(bool canceled) {
        if (stopped)
            return;
        run.requestStop();
        if (filter)
            filter->stop(); // Callback/control notification join precedes all producer retirement.
        if (canceled)
            run.cancel();
        else
            run.stop();
        stopped = true;
    }
    ~State() {
        finish(false);
    }
};
PipeWireManualRecording::PipeWireManualRecording(std::filesystem::path root, const Session &s,
                                                 MixPlan plan, std::vector<ManualRecordingArm> arms,
                                                 PipeWireManualRecordingOptions options)
    : state_(
          std::make_unique<State>(std::move(root), s, std::move(plan), std::move(arms), options)) {
    auto &st = *state_;
    st.filter = std::make_unique<PipeWireFilter>(
        PipeWireFilterOptions{"sc-daw-recording-manual-" + Id::generate().str(),
                              options.run.nativeInputs, st.run.graph().plan().output.channels},
        PipeWireCallbacks{&st, State::process, State::unavailable, State::begin, State::end,
                          State::clock});
    if (!st.filter->waitReady(options.readyTimeout))
        throw ProjectError(ErrorCode::Io, "PipeWire manual recording ports are not ready");
}
PipeWireManualRecording::~PipeWireManualRecording() = default;
std::vector<PipeWirePort> PipeWireManualRecording::ports() const {
    return state_->filter->ports();
}
void PipeWireManualRecording::connectInputs(const std::vector<PipeWirePort> &ports) {
    auto &s = *state_;
    if (s.stopped || s.attempted || s.inputRouted)
        throw ProjectError(ErrorCode::InvalidState, "Manual input route is closed");
    s.filter->connectInputs(ports);
    s.inputRouted = true;
}
void PipeWireManualRecording::connectOutputs(const std::vector<PipeWirePort> &ports) {
    auto &s = *state_;
    if (s.stopped || s.attempted || s.outputRouted)
        throw ProjectError(ErrorCode::InvalidState, "Manual output route is closed");
    s.filter->connectOutputs(ports);
    s.outputRouted = true;
}
void PipeWireManualRecording::activate() {
    auto &s = *state_;
    if (s.stopped || s.attempted || !s.inputRouted || !s.outputRouted ||
        s.run.status() != DuplexStatus::Ready)
        throw ProjectError(ErrorCode::InvalidState, "Select manual input and master output routes");
    s.attempted = true;
    try {
        s.filter
            ->activate(); // Writers are created later from actual published starts in service().
    } catch (...) {
        s.activationError = std::current_exception();
        s.run.requestFault(DuplexStatus::DeviceLost);
        s.finish(false);
        std::rethrow_exception(s.activationError);
    }
}
void PipeWireManualRecording::checkActivation() const {
    if (state_->activationError)
        std::rethrow_exception(state_->activationError);
}
std::uint64_t PipeWireManualRecording::prepareTake() {
    return state_->run.prepareTake();
}
void PipeWireManualRecording::abandonTake(std::uint64_t id) {
    state_->run.abandonTake(id);
}
ManualPunchSubmit PipeWireManualRecording::submit(ManualPunchCommand c) noexcept {
    return state_->run.submit(c);
}
bool PipeWireManualRecording::acknowledgement(ManualPunchReceipt &r) noexcept {
    return state_->run.acknowledgement(r);
}
void PipeWireManualRecording::service() {
    state_->run.service();
}
bool PipeWireManualRecording::takeGroup(ManualRecordedGroup &g) {
    return state_->run.takeGroup(g);
}
void PipeWireManualRecording::stop() {
    state_->finish(false);
}
void PipeWireManualRecording::cancel() {
    state_->finish(true);
}
void PipeWireManualRecording::checkError() const {
    state_->run.checkError();
}
void PipeWireManualRecording::checkReader() const {
    state_->run.checkReader();
}
PreparedMixGraph &PipeWireManualRecording::graph() noexcept {
    return state_->run.graph();
}
DuplexStatus PipeWireManualRecording::status() const noexcept {
    return state_->run.status();
}
Frame PipeWireManualRecording::position() const noexcept {
    return state_->run.position();
}
std::uint64_t PipeWireManualRecording::missingTrackFrames() const noexcept {
    return state_->run.missingTrackFrames();
}
std::optional<DuplexCallbackFault> PipeWireManualRecording::callbackFault() const noexcept {
    return state_->run.callbackFault();
}
std::size_t PipeWireManualRecording::occupiedSlots() const noexcept {
    return state_->run.occupiedSlots();
}
std::uint32_t PipeWireManualRecording::nodeId() const noexcept {
    return state_->filter->nodeId();
}
bool PipeWireManualRecording::memoryLocked() const noexcept {
    return state_->filter->memoryLocked();
}
} // namespace soundcurrent::daw
