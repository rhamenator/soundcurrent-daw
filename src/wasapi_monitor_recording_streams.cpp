// SPDX-License-Identifier: GPL-3.0-only
#include <soundcurrent/native_monitor_recording.hpp>
#include <soundcurrent/wasapi_render_lifecycle.hpp>
#include <windows.h>

namespace soundcurrent::daw {
namespace {
class WasapiMonitorStreams final : public NativeMonitorStreams {
    WasapiMonitorCaptureStream capture_;
    WasapiMonitorRenderStream render_;
    std::uint64_t qpcFrequency_ = 0;
  public:
    WasapiMonitorStreams(WasapiCaptureOptions c, WasapiRenderOptions r, WasapiMonitorRenderOptions o)
        : capture_(std::move(c)), render_(std::move(r), std::move(o)) {
        LARGE_INTEGER frequency{};
        if (!QueryPerformanceFrequency(&frequency) || frequency.QuadPart <= 0)
            throw ProjectError(ErrorCode::InvalidState, "Native processing QPC frequency is unavailable");
        qpcFrequency_ = std::uint64_t(frequency.QuadPart);
    }
    WasapiPreparedCaptureFormat captureFormat() const noexcept override { return capture_.preparedFormat(); }
    WasapiPreparedRenderFormat renderFormat() const noexcept override { return render_.preparedFormat(); }
    void bind(PreparedNativeMonitorRoute &r) override { capture_.bind(r.captureContext()); render_.bind(r.renderContext()); }
    void activateCapture() override { capture_.activate(); }
    void activateRender() override { render_.activate(); }
    std::optional<Qpc100ns> now() noexcept override {
        LARGE_INTEGER ticks{};
        if (!QueryPerformanceCounter(&ticks) || ticks.QuadPart <= 0) return {};
        return convertWasapiQpc(std::uint64_t(ticks.QuadPart), qpcFrequency_);
    }
    void requestCaptureStop() noexcept override { capture_.requestStop(); }
    void requestRenderStop() noexcept override { render_.requestStop(); }
    void joinCapture() noexcept override { capture_.stop(); }
    void joinRender() noexcept override { render_.stop(); }
};
}
std::unique_ptr<NativeMonitorStreams> prepareWasapiMonitorStreams(WasapiCaptureOptions capture,
        WasapiRenderOptions render, WasapiMonitorRenderOptions options) {
    return std::make_unique<WasapiMonitorStreams>(std::move(capture), std::move(render), std::move(options));
}
} // namespace soundcurrent::daw
