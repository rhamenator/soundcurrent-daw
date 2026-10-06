// SPDX-License-Identifier: GPL-3.0-only
#include <soundcurrent/pipewire_duplex_recording.hpp>
#include <exception>

namespace soundcurrent::daw {
struct PipeWireDuplexRecording::State {
    DuplexRecordingRun run;
    RecordingCallbackInstrumentation audit;
    void (*auditClock)(void *, const DeviceBlockClock &) noexcept;
    std::unique_ptr<PipeWireFilter> filter;
    std::exception_ptr activationError;
    bool inputRouted = false, outputRouted = false, attempted = false, stopped = false;
    State(std::filesystem::path root, const Session &s, MixPlan plan,
          std::vector<DuplexRecordingLane> lanes, const PipeWireDuplexRecordingOptions &o)
        : run(std::move(root), s, std::move(plan), std::move(lanes), native(o.run)), audit(o.audit),
          auditClock(o.auditClock) {}
    static DuplexRecordingOptions native(DuplexRecordingOptions o) {
        o.backend = CaptureBackend::PipeWire;
        return o;
    }
    static void process(void *context, const DeviceBlockClock &clock,
                        std::span<const float *const> input, std::span<float *const> output,
                        std::uint32_t capacity) noexcept {
        static_cast<State *>(context)->run.process(clock, input, output, capacity);
    }
    static void observeClock(void *context, const DeviceBlockClock &clock) noexcept {
        auto &s = *static_cast<State *>(context);
        if (s.auditClock)
            s.auditClock(s.audit.context, clock);
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
    void stop(bool cancel) noexcept {
        if (stopped)
            return;
        (void)run.status(); // Retain a disk failure before a user-stop request wins.
        run.requestStop();
        if (filter)
            filter->stop(); // Native callbacks/control notifications joined before raw finish.
        if (cancel)
            run.cancel();
        else
            run.stop();
        stopped = true;
    }
    ~State() {
        stop(false);
    }
};
PipeWireDuplexRecording::PipeWireDuplexRecording(std::filesystem::path root, const Session &s,
                                                 MixPlan plan,
                                                 std::vector<DuplexRecordingLane> lanes,
                                                 PipeWireDuplexRecordingOptions options)
    : state_(
          std::make_unique<State>(std::move(root), s, std::move(plan), std::move(lanes), options)) {
    auto &st = *state_;
    st.filter = std::make_unique<PipeWireFilter>(
        PipeWireFilterOptions{"sc-daw-recording-duplex-" + Id::generate().str(),
                              options.run.nativeInputs, st.run.graph().plan().output.channels},
        PipeWireCallbacks{&st, State::process, State::unavailable, State::begin, State::end,
                          State::observeClock});
    if (!st.filter->waitReady(options.readyTimeout))
        throw ProjectError(ErrorCode::Io, "PipeWire duplex ports are not ready");
}
PipeWireDuplexRecording::~PipeWireDuplexRecording() = default;
std::vector<PipeWirePort> PipeWireDuplexRecording::ports() const {
    return state_->filter->ports();
}
void PipeWireDuplexRecording::connectInputs(const std::vector<PipeWirePort> &ports) {
    auto &s = *state_;
    if (s.stopped || s.attempted || s.inputRouted)
        throw ProjectError(ErrorCode::InvalidState, "Duplex input route is closed");
    s.filter->connectInputs(ports);
    s.inputRouted = true;
}
void PipeWireDuplexRecording::connectOutputs(const std::vector<PipeWirePort> &ports) {
    auto &s = *state_;
    if (s.stopped || s.attempted || s.outputRouted)
        throw ProjectError(ErrorCode::InvalidState, "Duplex output route is closed");
    s.filter->connectOutputs(ports);
    s.outputRouted = true;
}
void PipeWireDuplexRecording::activate() {
    auto &s = *state_;
    if (s.stopped || s.attempted || !s.inputRouted || !s.outputRouted ||
        s.run.status() != DuplexStatus::Ready)
        throw ProjectError(ErrorCode::InvalidState, "Select duplex input and master output routes");
    s.attempted = true;
    bool writersStarted = false;
    try {
        s.run.startWriters();
        writersStarted = true;
        s.filter->activate();
    } catch (...) {
        s.activationError = std::current_exception();
        s.run.requestFault(writersStarted ? DuplexStatus::DeviceLost : DuplexStatus::CaptureFailed);
        s.stop(false);
        std::rethrow_exception(s.activationError);
    }
}
void PipeWireDuplexRecording::checkActivation() const {
    if (state_->activationError)
        std::rethrow_exception(state_->activationError);
    state_->run.checkActivation();
}
void PipeWireDuplexRecording::stop() noexcept {
    state_->stop(false);
}
void PipeWireDuplexRecording::cancel() noexcept {
    state_->stop(true);
}
DuplexRecordingRun &PipeWireDuplexRecording::run() noexcept {
    return state_->run;
}
std::uint32_t PipeWireDuplexRecording::nodeId() const noexcept {
    return state_->filter->nodeId();
}
bool PipeWireDuplexRecording::memoryLocked() const noexcept {
    return state_->filter->memoryLocked();
}
} // namespace soundcurrent::daw
