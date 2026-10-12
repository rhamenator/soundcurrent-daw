// SPDX-License-Identifier: GPL-3.0-only
#include <soundcurrent/pipewire_monitor_route.hpp>
namespace soundcurrent::daw {
namespace {
struct Admission {
    PipeWireRecordingSpec spec;PipeWireMonitorRouteOptions options;
    CaptureFanoutConfig capture;MonitorOutputBankConfig output;MonitorProcessingConfig processing;
    MonitorClockHandoffConfig clocks;MonitorControlConfig control;MonitorCaptureStopConfig stop;
    MonitorStartupAdmission startup;PipeWireReceiptQueueOptions receipts;std::size_t bytes=0;
};
Admission admit(PipeWireRecordingSpec s,PipeWireMonitorRouteOptions o){Admission a;a.spec=std::move(s);a.options=std::move(o);
    const auto &spec=a.spec;const auto &opts=a.options;auto &f=a.capture;auto &out=a.output;
    if(!spec.generation || !spec.renderIdentity.endpoint || !spec.renderIdentity.stream || !spec.renderIdentity.generation ||
       spec.renderRate<8000 || spec.renderRate>384000 || !spec.renderChannels || spec.renderChannels>256 ||
       !spec.maximumRenderFrames || spec.maximumRenderFrames>65536 || !opts.maximumWait100ns || opts.maximumWait100ns>600000000 ||
       (!opts.channelMap && spec.raw.layout.channels!=spec.renderChannels) || opts.maximumWarmupFrames>std::uint64_t(spec.renderRate)*60)
        throw ProjectError(ErrorCode::InvalidParameter,"Invalid expected PipeWire route geometry");
    const auto budget=opts.memoryBudgetBytes;
    f.capture.identity=spec.raw.identity;f.capture.sampleRate=spec.raw.sampleRate;f.capture.channels=spec.raw.layout.channels;
    f.capture.maximumPacketFrames=spec.raw.maximumPacketFrames;f.capture.initialPacketSequence=spec.raw.initialPacketSequence;
    f.capture.slots=opts.rawSlots;f.monitorSlots=opts.monitorSlots;f.memoryBudgetBytes=f.capture.memoryBudgetBytes=budget;
    out.sampleRate=spec.renderRate;out.channels=spec.renderChannels;out.maximumPullFrames=spec.maximumRenderFrames;
    out.slots=opts.outputSlots;out.generation=spec.generation;out.first=opts.first;out.memoryBudgetBytes=budget;
    auto &c=a.processing;auto &b=c.buffer;auto &d=c.drift;
    b.sourceRate=d.sourceRate=spec.raw.sampleRate;b.outputRate=d.outputRate=spec.renderRate;
    b.channels=spec.raw.layout.channels;b.maximumInputFrames=spec.raw.maximumPacketFrames;
    b.inputCapacityFrames=d.inputCapacityFrames=opts.inputCapacityFrames;b.runGeneration=d.runGeneration=spec.generation;
    b.outputFrameBase=d.outputFrameBase=opts.first.value;d.captureIdentity=spec.raw.identity;d.renderIdentity=spec.renderIdentity;
    d.renderFrequency=spec.renderRate;d.targetQueuedSourceFrames=opts.targetQueuedSourceFrames;
    c.memoryBudgetBytes=b.memoryBudgetBytes=d.memoryBudgetBytes=budget;
    c.startup=MonitorStartupConfig{spec.maximumRenderFrames,spec.maximumRenderFrames,opts.maximumWait100ns};
    c.channelMap=opts.channelMap;c.equalizer=opts.equalizer;
    auto &q=a.clocks;q.captureIdentity=spec.raw.identity;q.renderIdentity=spec.renderIdentity;q.renderFrequency=spec.renderRate;
    q.runGeneration=spec.generation;q.slots=opts.clockSlots;q.memoryBudgetBytes=budget;q.timestampDomain=MonitorClockDomain::PosixMonotonic100ns;
    a.control.runGeneration=spec.generation;a.control.slots=opts.controlSlots;a.control.memoryBudgetBytes=budget;
    auto &stop=a.stop;stop.runGeneration=spec.generation;stop.sourceRate=spec.raw.sampleRate;stop.channels=spec.raw.layout.channels;
    stop.maximumPacketFrames=spec.raw.maximumPacketFrames;stop.monitorSlots=opts.monitorSlots;stop.inputCapacityFrames=opts.inputCapacityFrames;
    stop.targetQueuedSourceFrames=opts.targetQueuedSourceFrames;stop.warmupInputFrames=monitorBufferWarmupInputFrames(b);stop.memoryBudgetBytes=budget;
    a.startup={spec.generation,spec.raw.identity,spec.renderIdentity,spec.raw.sampleRate,spec.renderRate,spec.renderChannels,spec.renderRate,
        spec.maximumRenderFrames,spec.maximumRenderFrames,opts.maximumWait100ns,
        c.channelMap?std::optional(monitorChannelMapReceipt(*c.channelMap)):std::nullopt,
        c.equalizer?std::optional(monitorEqReceipt(*c.equalizer,spec.renderRate,spec.renderChannels)):std::nullopt};
    a.receipts.generation=spec.generation;a.receipts.slots=opts.receiptSlots;a.receipts.memoryBudgetBytes=budget;
    PreparedMonitorDrift validate(d);PreparedMonitorStartupHandoff validateStartup(a.startup,budget);
    PayloadCharge charge("Owned PipeWire monitor route",budget);charge.add(sizeof(Admission)+65536);
    charge.add(captureFanoutPayloadBytes(f));charge.add(monitorOutputBankPayloadBytes(out));charge.add(monitorClockHandoffPayloadBytes(q));
    charge.add(monitorControlPayloadBytes(a.control));charge.add(monitorCaptureStopPayloadBytes(stop));charge.add(sizeof(PreparedPipeWireCaptureStop));
    charge.add(sizeof(PreparedMonitorStartupHandoff));charge.add(pipeWireReceiptPayloadBytes(a.receipts));charge.add(monitorProcessingPayloadBytes(f,out,c));
    charge.add(sizeof(PreparedPipeWireMonitorCapture));charge.add(spec.raw.maximumPacketFrames,std::size_t(spec.raw.layout.channels)*sizeof(float));
    charge.add(sizeof(PreparedPipeWireMonitorRender));charge.add(spec.maximumRenderFrames,std::size_t(spec.renderChannels)*sizeof(float));
    if(c.channelMap)charge.add(c.channelMap->routes.capacity(),3*sizeof(ChannelMix));
    if(c.equalizer)charge.add(c.equalizer->settings.bands.capacity(),3*(sizeof(EqBand)+128));
    a.bytes=charge.bytes();return a;
}
}
struct PreparedPipeWireMonitorRoute::State {
    ResourceLease lease;const Admission admitted;
    PreparedCaptureFanout capture;PreparedMonitorOutputBank output;PreparedMonitorClockHandoff clocks;PreparedMonitorControl control;
    PreparedPipeWireCaptureStop stop;PreparedMonitorStartupHandoff startup;PreparedPipeWireReceiptQueues receipts;
    PreparedMonitorProcessing processing;bool settled=false;
    explicit State(Admission a):lease(a.options.resources?a.options.resources->reserve(a.bytes):ResourceLease{}),admitted(std::move(a)),
        capture(admitted.capture),output(admitted.output),clocks(admitted.clocks),control(admitted.control),stop(admitted.stop),
        startup(admitted.startup,admitted.options.memoryBudgetBytes),receipts(admitted.receipts),
        processing(capture,output,admitted.processing,&clocks,&control,&stop,&startup) {}
};
PreparedPipeWireMonitorRoute::PreparedPipeWireMonitorRoute(PipeWireRecordingSpec s,PipeWireMonitorRouteOptions o)
    :state_(std::make_unique<State>(admit(std::move(s),std::move(o)))){}
PreparedPipeWireMonitorRoute::~PreparedPipeWireMonitorRoute()=default;
PreparedCaptureFanout &PreparedPipeWireMonitorRoute::capture() noexcept{return state_->capture;}
PreparedMonitorOutputBank &PreparedPipeWireMonitorRoute::output() noexcept{return state_->output;}
PreparedMonitorClockHandoff &PreparedPipeWireMonitorRoute::clocks() noexcept{return state_->clocks;}
PreparedMonitorControl &PreparedPipeWireMonitorRoute::control() noexcept{return state_->control;}
PreparedPipeWireCaptureStop &PreparedPipeWireMonitorRoute::stop() noexcept{return state_->stop;}
PreparedMonitorStartupHandoff &PreparedPipeWireMonitorRoute::startup() noexcept{return state_->startup;}
PreparedPipeWireReceiptQueues &PreparedPipeWireMonitorRoute::receipts() noexcept{return state_->receipts;}
PreparedMonitorProcessing &PreparedPipeWireMonitorRoute::processing() noexcept{return state_->processing;}
PipeWireMonitorCaptureOptions PreparedPipeWireMonitorRoute::captureOptions() noexcept {
    PipeWireMonitorCaptureOptions o;o.memoryBudgetBytes=state_->admitted.options.memoryBudgetBytes;o.requireNativeReturns=true;
    o.receiptContext=&state_->receipts;o.receipt=PreparedPipeWireReceiptQueues::captureSink;o.stop=&state_->stop;return o;
}
PipeWireMonitorRenderOptions PreparedPipeWireMonitorRoute::renderOptions() noexcept {
    PipeWireMonitorRenderOptions o;o.memoryBudgetBytes=state_->admitted.options.memoryBudgetBytes;
    o.maximumWarmupFrames=state_->admitted.options.maximumWarmupFrames;o.maximumWarmupNs=state_->admitted.options.maximumWait100ns*100;
    o.receiptContext=&state_->receipts;o.receipt=PreparedPipeWireReceiptQueues::renderSink;o.startup=&state_->startup;return o;
}
const PipeWireRecordingSpec &PreparedPipeWireMonitorRoute::declared() const noexcept{return state_->admitted.spec;}
std::size_t PreparedPipeWireMonitorRoute::chargedBytes() const noexcept{return state_->admitted.bytes;}
void PreparedPipeWireMonitorRoute::settleAfterJoin() noexcept {auto &s=*state_;if(s.settled)return;
    s.processing.settleAfterJoin();s.startup.settleAfterProducerJoin();s.stop.settleAfterJoin();s.control.settleAfterJoin();
    s.clocks.settleAfterJoin();s.output.settleAfterJoin();s.capture.settleAfterJoin();s.settled=true;}
} // namespace soundcurrent::daw
