// SPDX-License-Identifier: GPL-3.0-only
#include <soundcurrent/wasapi_monitor_capture.hpp>

namespace soundcurrent::daw {
namespace {
WasapiCaptureOptions admitted(WasapiCaptureOptions o) {
    if (!o.identity.endpoint || !o.identity.stream || !o.identity.generation)
        throw ProjectError(ErrorCode::InvalidParameter,"Declare an immutable native capture route identity");
    return o;
}
}
struct WasapiMonitorCaptureStream::State {
    PreparedWasapiCaptureBinding binding;
    WasapiCaptureStream stream; // Retired first; callback binding outlives SDK join.
    State(WasapiCaptureOptions native, WasapiMonitorCaptureOptions context)
        : binding(std::move(context)),stream(admitted(std::move(native)),binding.callbacks()) {}
};
WasapiMonitorCaptureStream::WasapiMonitorCaptureStream(WasapiCaptureOptions native, WasapiMonitorCaptureOptions context)
    : state_(std::make_unique<State>(std::move(native),std::move(context))) {}
WasapiMonitorCaptureStream::~WasapiMonitorCaptureStream() = default;
WasapiPreparedCaptureFormat WasapiMonitorCaptureStream::preparedFormat() const noexcept { return state_->stream.preparedFormat(); }
void WasapiMonitorCaptureStream::bind(PreparedWasapiMonitorCapture &context) {
    if (!state_->binding.bind(context,preparedFormat()))
        throw ProjectError(ErrorCode::InvalidState,"Native capture owner is already bound or ended");
}
void WasapiMonitorCaptureStream::activate() {
    if (state_->binding.phase()!=WasapiCaptureBindingPhase::Bound ||
        state_->binding.firstUnavailable() || state_->binding.firstFault())
        throw ProjectError(ErrorCode::InvalidState,"Bind a healthy prepared capture context before activation");
    state_->stream.activate();
}
void WasapiMonitorCaptureStream::requestStop() noexcept { state_->stream.requestStop(); }
void WasapiMonitorCaptureStream::stop() noexcept { state_->stream.stop(); }
std::optional<WasapiCaptureEnd> WasapiMonitorCaptureStream::endAfterJoin() const noexcept { return state_->stream.endAfterJoin(); }
std::optional<WasapiCaptureBindingFault> WasapiMonitorCaptureStream::bindingFault() const noexcept { return state_->binding.firstFault(); }
} // namespace soundcurrent::daw
