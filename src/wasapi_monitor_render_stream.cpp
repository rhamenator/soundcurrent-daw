// SPDX-License-Identifier: GPL-3.0-only
#include <soundcurrent/wasapi_monitor_render.hpp>
namespace soundcurrent::daw {
namespace {
WasapiRenderOptions admitted(WasapiRenderOptions o){
    if(!o.identity.endpoint || !o.identity.stream || !o.identity.generation)
        throw ProjectError(ErrorCode::InvalidParameter,"Declare an immutable native render route identity");
    return o;
}
}
struct WasapiMonitorRenderStream::State {
    PreparedWasapiRenderBinding binding;
    WasapiRenderStream stream; // Joins BEFORE binding retirement.
    bool stopped=false;
    State(WasapiRenderOptions o,WasapiMonitorRenderOptions c):binding(std::move(c)),stream(admitted(std::move(o)),binding.callbacks()){}
    void stop() noexcept {if(!stopped){binding.requestCancel();stream.stop();stopped=true;}}
};
WasapiMonitorRenderStream::WasapiMonitorRenderStream(WasapiRenderOptions o,WasapiMonitorRenderOptions c):state_(std::make_unique<State>(std::move(o),std::move(c))){}
WasapiMonitorRenderStream::~WasapiMonitorRenderStream(){stop();}
WasapiPreparedRenderFormat WasapiMonitorRenderStream::preparedFormat() const noexcept {return state_->stream.preparedFormat();}
void WasapiMonitorRenderStream::bind(PreparedWasapiMonitorRender &c){
    if(state_->stopped || !state_->binding.bind(c,preparedFormat()))throw ProjectError(ErrorCode::InvalidState,"Native render owner is already bound or ended");
}
void WasapiMonitorRenderStream::activate(){
    if(state_->stopped || state_->binding.phase()!=WasapiRenderBindingPhase::Bound || state_->binding.firstFault() || state_->binding.firstUnavailable())
        throw ProjectError(ErrorCode::InvalidState,"Bind a healthy native render context before activation");
    state_->stream.activate();
}
void WasapiMonitorRenderStream::requestStop() noexcept {
    if(!state_->stopped){state_->binding.requestCancel();state_->stream.requestStop();}
}
void WasapiMonitorRenderStream::stop() noexcept {state_->stop();}
std::optional<WasapiRenderEnd> WasapiMonitorRenderStream::endAfterJoin() const noexcept {return state_->stream.endAfterJoin();}
std::optional<WasapiRenderBindingFault> WasapiMonitorRenderStream::bindingFault() const noexcept {return state_->binding.firstFault();}
} // namespace soundcurrent::daw
