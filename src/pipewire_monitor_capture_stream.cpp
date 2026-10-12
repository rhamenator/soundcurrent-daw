// SPDX-License-Identifier: GPL-3.0-only
#include <soundcurrent/pipewire_monitor_capture_stream.hpp>
namespace soundcurrent::daw {
struct PipeWireMonitorCaptureStream::State {
    PreparedPipeWireMonitorCapture capture;
    const PipeWireMonitorCaptureStreamOptions options;
    std::unique_ptr<PipeWireFilter> filter;
    bool routed = false, attempted = false, stopped = false;
    static PipeWireMonitorCaptureOptions required(PipeWireMonitorCaptureOptions o){o.requireNativeReturns=true;return o;}
    State(PreparedCaptureFanout &f, PreparedMonitorClockHandoff &h,
          PipeWireMonitorCaptureStreamOptions o) : capture(f, h, required(o.capture)), options(std::move(o)) {}
    static void process(void *ctx, const DeviceBlockClock &clock, std::span<const float *const> input,
                        std::span<float *const>, std::uint32_t frames) noexcept {
        static_cast<State *>(ctx)->capture.process(clock, input, frames);
    }
    static void unavailable(void *ctx, AudioBridgeStatus reason) noexcept {
        static_cast<State *>(ctx)->capture.unavailable(reason);
    }
    static void completed(void *ctx,const PipeWireCycleReceipt &r) noexcept {
        static_cast<State *>(ctx)->capture.completeCycle(r);
    }
    static void begin(void *ctx) noexcept {
        const auto &o = static_cast<State *>(ctx)->options;
        if (o.beginCallback) o.beginCallback(o.auditContext);
    }
    static void end(void *ctx) noexcept {
        const auto &o = static_cast<State *>(ctx)->options;
        if (o.endCallback) o.endCallback(o.auditContext);
    }
    void stop() noexcept {
        if (stopped) return;
        capture.requestStop();
        if (filter) filter->stop();
        capture.finishAfterJoin(); stopped = true;
    }
    ~State() { stop(); }
};
PipeWireMonitorCaptureStream::PipeWireMonitorCaptureStream(PreparedCaptureFanout &f,
        PreparedMonitorClockHandoff &h, PipeWireMonitorCaptureStreamOptions o)
    : state_(std::make_unique<State>(f, h, std::move(o))) {
    auto &s = *state_;
    s.filter = std::make_unique<PipeWireFilter>(PipeWireFilterOptions{s.options.nodeName,
        f.captureConfig().channels, 0, f.captureConfig().maximumPacketFrames},
        PipeWireCallbacks{&s, State::process, State::unavailable, State::begin, State::end,nullptr,State::completed});
    if (!s.filter->waitReady(s.options.readyTimeout))
        throw ProjectError(ErrorCode::Io, "PipeWire monitor capture ports are not ready");
}
PipeWireMonitorCaptureStream::~PipeWireMonitorCaptureStream() = default;
std::vector<PipeWirePort> PipeWireMonitorCaptureStream::ports() const { return state_->filter->ports(); }
void PipeWireMonitorCaptureStream::connectInputs(const std::vector<PipeWirePort> &p) {
    auto &s = *state_;
    if (s.stopped || s.attempted || s.routed)
        throw ProjectError(ErrorCode::InvalidState, "PipeWire capture route is closed");
    s.filter->connectInputs(p); s.routed = true;
}
void PipeWireMonitorCaptureStream::activate() {
    auto &s = *state_;
    if (s.stopped || s.attempted || !s.routed)
        throw ProjectError(ErrorCode::InvalidState, "Select an inactive PipeWire capture route");
    s.attempted = true;
    try { s.filter->activate(); } catch (...) { s.stop(); throw; }
}
void PipeWireMonitorCaptureStream::requestStop() noexcept { state_->capture.requestStop(); }
void PipeWireMonitorCaptureStream::stop() noexcept { state_->stop(); }
std::optional<PipeWireCaptureFault> PipeWireMonitorCaptureStream::firstFault() const noexcept{return state_->capture.firstFault();}
std::optional<AudioBridgeStatus> PipeWireMonitorCaptureStream::firstUnavailable() const noexcept{return state_->capture.firstUnavailable();}
std::optional<PipeWireCaptureEnd> PipeWireMonitorCaptureStream::endAfterJoin() const noexcept {
    return state_->capture.endAfterJoin();
}
std::uint32_t PipeWireMonitorCaptureStream::nodeId() const noexcept { return state_->filter->nodeId(); }
} // namespace soundcurrent::daw
