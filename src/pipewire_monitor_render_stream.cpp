// SPDX-License-Identifier: GPL-3.0-only
#include <soundcurrent/pipewire_monitor_render_stream.hpp>
#include <time.h>
namespace soundcurrent::daw {
struct PipeWireMonitorRenderStream::State {
    PreparedPipeWireMonitorRender render;
    const PipeWireMonitorRenderStreamOptions options;
    std::unique_ptr<PipeWireFilter> filter;
    bool routed=false,attempted=false,stopped=false;
    State(PreparedMonitorOutputBank &b,PreparedMonitorClockHandoff &h,PipeWireMonitorRenderStreamOptions o)
        :render(b,h,o.render),options(std::move(o)) {}
    static void process(void *ctx,const DeviceBlockClock &clock,std::span<const float *const>,
                        std::span<float *const> out,std::uint32_t capacity) noexcept {
        auto &s=*static_cast<State *>(ctx);std::optional<Qpc100ns> now;
        if(s.options.render.startup){timespec t{};
            if(clock_gettime(CLOCK_MONOTONIC,&t)==0 && t.tv_sec>=0 && t.tv_nsec>=0 && t.tv_nsec<1000000000 &&
               std::uint64_t(t.tv_sec)<=(UINT64_MAX-std::uint64_t(t.tv_nsec)/100)/10000000)
                now=Qpc100ns{std::uint64_t(t.tv_sec)*10000000+std::uint64_t(t.tv_nsec)/100};}
        s.render.process(clock,out,capacity,now);
    }
    static void completed(void *ctx,const PipeWireCycleReceipt &r) noexcept {
        static_cast<State *>(ctx)->render.completeCycle(r);
    }
    static void unavailable(void *ctx,AudioBridgeStatus reason) noexcept {
        static_cast<State *>(ctx)->render.unavailable(reason);
    }
    static void begin(void *ctx) noexcept {
        const auto &o=static_cast<State *>(ctx)->options;if(o.beginCallback)o.beginCallback(o.auditContext);
    }
    static void end(void *ctx) noexcept {
        const auto &o=static_cast<State *>(ctx)->options;if(o.endCallback)o.endCallback(o.auditContext);
    }
    void stop() noexcept {
        if(stopped)return;
        render.requestStop();if(filter)filter->stop();render.finishAfterJoin();stopped=true;
    }
    ~State(){stop();}
};
PipeWireMonitorRenderStream::PipeWireMonitorRenderStream(PreparedMonitorOutputBank &b,
        PreparedMonitorClockHandoff &h,PipeWireMonitorRenderStreamOptions o)
    :state_(std::make_unique<State>(b,h,std::move(o))) {
    auto &s=*state_;
    s.filter=std::make_unique<PipeWireFilter>(PipeWireFilterOptions{s.options.nodeName,0,b.config().channels,
        b.config().maximumPullFrames},PipeWireCallbacks{&s,State::process,State::unavailable,State::begin,State::end,
        nullptr,State::completed});
    if(!s.filter->waitReady(s.options.readyTimeout))
        throw ProjectError(ErrorCode::Io,"PipeWire monitor render ports are not ready");
}
PipeWireMonitorRenderStream::~PipeWireMonitorRenderStream()=default;
std::vector<PipeWirePort> PipeWireMonitorRenderStream::ports() const {return state_->filter->ports();}
void PipeWireMonitorRenderStream::connectOutputs(const std::vector<PipeWirePort> &p) {
    auto &s=*state_;if(s.stopped || s.attempted || s.routed)
        throw ProjectError(ErrorCode::InvalidState,"PipeWire render route is closed");
    s.filter->connectOutputs(p);s.routed=true;
}
void PipeWireMonitorRenderStream::activate() {
    auto &s=*state_;if(s.stopped || s.attempted || !s.routed)
        throw ProjectError(ErrorCode::InvalidState,"Select an inactive PipeWire render route");
    s.attempted=true;try{s.filter->activate();}catch(...){s.stop();throw;}
}
void PipeWireMonitorRenderStream::requestStop() noexcept {state_->render.requestStop();}
void PipeWireMonitorRenderStream::stop() noexcept {state_->stop();}
bool PipeWireMonitorRenderStream::contentComplete() const noexcept {return state_->render.contentComplete();}
std::optional<PipeWireRenderFault> PipeWireMonitorRenderStream::firstFault() const noexcept {return state_->render.firstFault();}
std::optional<PipeWireRenderEnd> PipeWireMonitorRenderStream::endAfterJoin() const noexcept {return state_->render.endAfterJoin();}
std::uint32_t PipeWireMonitorRenderStream::nodeId() const noexcept {return state_->filter->nodeId();}
} // namespace soundcurrent::daw
