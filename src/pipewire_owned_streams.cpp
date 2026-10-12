// SPDX-License-Identifier: GPL-3.0-only
#include <soundcurrent/pipewire_owned_streams.hpp>
#include <time.h>
namespace soundcurrent::daw {
namespace {
class Streams final : public PipeWireMonitorStreams {
    PipeWireMonitorCaptureStream capture_;PipeWireMonitorRenderStream render_;
    static PipeWireMonitorCaptureStreamOptions captureOptions(PreparedPipeWireMonitorRoute &r,PipeWireOwnedStreamOptions &o){o.capture.capture=r.captureOptions();return o.capture;}
    static PipeWireMonitorRenderStreamOptions renderOptions(PreparedPipeWireMonitorRoute &r,PipeWireOwnedStreamOptions &o){o.render.render=r.renderOptions();return o.render;}
  public:
    Streams(PreparedPipeWireMonitorRoute &r,PipeWireOwnedStreamOptions o):capture_(r.capture(),r.clocks(),captureOptions(r,o)),render_(r.output(),r.clocks(),renderOptions(r,o)){}
    std::vector<AudioPort> ports() const override{return capture_.ports();}
    void connect(const std::vector<AudioPort> &i,const std::vector<AudioPort> &o) override{capture_.connectInputs(i);render_.connectOutputs(o);}
    void activateCapture() override{capture_.activate();}
    void activateRender() override{render_.activate();}
    std::optional<Qpc100ns> now() noexcept override {timespec t{};
        if(clock_gettime(CLOCK_MONOTONIC,&t)!=0 || t.tv_sec<0 || t.tv_nsec<0 || t.tv_nsec>=1000000000 ||
           std::uint64_t(t.tv_sec)>(UINT64_MAX-std::uint64_t(t.tv_nsec)/100)/10000000)return {};
        return Qpc100ns{std::uint64_t(t.tv_sec)*10000000+std::uint64_t(t.tv_nsec)/100};}
    void requestCaptureStop() noexcept override{capture_.requestStop();}
    void requestRenderStop() noexcept override{render_.requestStop();}
    void joinCapture() noexcept override{capture_.stop();}
    void joinRender() noexcept override{render_.stop();}
    bool renderComplete() const noexcept override{return render_.contentComplete();}
    bool unavailable() const noexcept override{return bool(capture_.firstUnavailable() || render_.firstUnavailable());}
    std::optional<PipeWireCaptureFault> captureFault() const noexcept override{return capture_.firstFault();}
    std::optional<PipeWireRenderFault> renderFault() const noexcept override{return render_.firstFault();}
    std::optional<PipeWireCaptureEnd> captureEndAfterJoin() const noexcept override{return capture_.endAfterJoin();}
    std::optional<PipeWireRenderEnd> renderEndAfterJoin() const noexcept override{return render_.endAfterJoin();}
};
}
std::unique_ptr<PipeWireMonitorStreams> preparePipeWireOwnedStreams(PreparedPipeWireMonitorRoute &r,PipeWireOwnedStreamOptions o){return std::make_unique<Streams>(r,std::move(o));}
} // namespace soundcurrent::daw
