// SPDX-License-Identifier: GPL-3.0-only
#include <soundcurrent/monitor_processing.hpp>
#include <soundcurrent/original_recording.hpp>
#include <soundcurrent/wasapi_packet_copy.hpp>
#include <soundcurrent/wasapi_monitor_capture.hpp>
#include <soundcurrent/wasapi_monitor_render.hpp>
#include <soundcurrent/wasapi_render_lifecycle.hpp>
#include "rt_audit.hpp"
#include "../src/media_io.hpp"
#include <barrier>
#include <bit>
#include <iostream>
#include <thread>
using namespace soundcurrent::daw;
namespace {
unsigned checks=0;
void check(bool ok,const char *why) { ++checks;if(!ok)throw std::runtime_error(why); }
template<class F> auto rt(F f) { rt_audit::Guard guard;return f(); }
template<class F> void refuses(F f) { try{f();}catch(const ProjectError &){check(true,"Refused");return;}check(false,"Bad admission accepted"); }
void audit(const rt_audit::Counts &a) { check(!a.cppAllocate && !a.cppFree && !a.cAllocate && !a.cFree && !a.blockingLock,"Guarded allocate/free/lock"); }
constexpr std::uint64_t base=std::uint64_t(1)<<54, now=base+40000000, origin=(std::uint64_t(1)<<63)+192000;
struct Config {
    CaptureFanoutConfig capture;
    MonitorOutputBankConfig output;
    MonitorProcessingConfig processing;
    MonitorCaptureStopConfig stop;
    Config(bool context=false) {
        capture.capture.identity={11,31,7};capture.capture.maximumPacketFrames=64;
        capture.capture.slots=8;capture.monitorSlots=8;
        output.channels=1;output.first.value=base;output.maximumPullFrames=64;output.slots=2;
        auto &b=processing.buffer;b.outputFrameBase=base;b.maximumInputFrames=64;
        b.inputCapacityFrames=1664;b.outputCapacityFrames=64;
        auto &d=processing.drift;d.captureIdentity=capture.capture.identity;d.outputFrameBase=base;
        d.inputCapacityFrames=b.inputCapacityFrames;d.targetQueuedSourceFrames=1024;d.occupancyGainPpmPerFrame=0;
        stop.monitorSlots=8;stop.inputCapacityFrames=b.inputCapacityFrames;
        stop.warmupInputFrames=monitorBufferWarmupInputFrames(b);
        // 64k is a synthetic fixture rate: 64 frames = exactly 10,000 QPC
        // units, avoiding timestamp-rounding drift in the independent PCM oracle.
        if(context) {
            capture.capture.sampleRate=output.sampleRate=b.sourceRate=b.outputRate=
                d.sourceRate=d.outputRate=stop.sourceRate=64000;
            d.renderFrequency=64000;
        }
    }
};
void seed(PreparedMonitorProcessing &p,const Config &c) {
    for(unsigned n=0;n<3;++n) {
        const auto q=now-40000000+std::uint64_t(n)*20000000;
        const auto pos=origin-4*c.processing.drift.sourceRate+std::uint64_t(n)*2*c.processing.drift.sourceRate;
        p.observe(CaptureClockReading{c.processing.drift.captureIdentity,{pos},{q}});
        p.observe(RenderClockReading{c.processing.drift.renderIdentity,{pos},{q},c.processing.drift.renderFrequency});
    }
}
void lifecycle() {
    struct Context {
        bool stop=false;unsigned ended=0;WasapiCaptureEnd receipt;
        static bool poll(void *p) noexcept { return static_cast<Context *>(p)->stop; }
        static void finish(void *p,const WasapiCaptureEnd &r) noexcept { auto &s=*static_cast<Context *>(p);++s.ended;s.receipt=r; }
    } c;
    PreparedWasapiCaptureLifecycle l({&c,Context::poll,Context::finish});
    check(!l.afterJoin(),"Missing lifecycle receipt fabricated before finish");
    check(!rt([&]{return l.shouldStop();}),"Unrequested native Stop");
    rt([&]{l.started();l.delivered({nullptr,0,31,wasapiDiscontinuity|wasapiSilent,origin,now});
        l.delivered({nullptr,0,17,0,origin+31,now+100});l.stopped(1);l.stopped(-9);
        l.finish({});l.finish(WasapiStreamFailure{-7,4},true);});
    check(c.ended==1 && c.receipt.started && c.receipt.stopHresult==1 && !c.receipt.failure &&
        c.receipt.deliveredFrames==48 && c.receipt.deliveredPackets==2 && c.receipt.lastDelivered->devicePosition==origin+31 &&
        c.receipt.lastDelivered->qpc100ns==now+100,"Actual lifecycle metadata/first result lost");
    check(rt([&]{return l.shouldStop();}),"Ended owner still acquiring");
    rt([&]{l.delivered({nullptr,0,64,0,0,0});});check(l.afterJoin()->deliveredFrames==48,"Ended owner mutated receipt");
    for(unsigned kind=0;kind<4;++kind) {
        Context s;PreparedWasapiCaptureLifecycle q({&s,Context::poll,Context::finish});
        if(kind==0) { s.stop=true;check(rt([&]{return q.shouldStop();}),"Before-start Stop ignored"); }
        if(kind>=2)rt([&]{q.started();q.stopped(kind==2?-17:0);});
        rt([&]{q.finish(kind==1?std::optional(WasapiStreamFailure{-19,1}):kind==2?std::optional(WasapiStreamFailure{-17,4}):std::nullopt,kind==3);});
        check(s.ended==1 && s.receipt.started==(kind>=2) && bool(s.receipt.stopHresult)==(kind>=2),"Missing SDK operation fabricated");
        check(s.receipt.ownerException==(kind==3) && bool(s.receipt.failure)==(kind==1 || kind==2),"Owner failure altered");
    }
}
void admissionAndReceipts() {
    Config c;check(monitorCaptureStopRequiredInputFrames(c.stop)==1664,"Stop postroll bound");
    ResourceLedger ledger(65536);c.stop.resources=ledger;const auto bytes=monitorCaptureStopPayloadBytes(c.stop);
    for(unsigned n=0;n<5;++n) {
        auto bad=c.stop;
        if(n==0)bad.inputCapacityFrames=1663;else if(n==1)bad.runGeneration=0;else if(n==2)bad.monitorSlots=257;
        else if(n==3)bad.maximumPacketFrames=65537;else bad.memoryBudgetBytes=bytes-1;
        const auto before=ledger.usage();refuses([&]{PreparedMonitorCaptureStop q(bad);});check(ledger.usage()==before,"Rejected native stop admission charged credit");
    }
    {
        PreparedMonitorCaptureStop q(c.stop);check(q.chargedBytes()==bytes && ledger.usage().reservedBytes==bytes,"Stop ledger charge");
        PreparedCaptureFanout f(c.capture);PreparedMonitorOutputBank o(c.output);
        auto bad=c.processing;bad.buffer.inputCapacityFrames+=64;
        refuses([&]{PreparedMonitorProcessing p(f,o,bad,nullptr,nullptr,&q);});
        auto r=MonitorCaptureStopRequest{2,MonitorCaptureStopKind::Horizon,base+512,base+512,1024};
        check(!rt([&]{return q.request(r);}) && !q.requested(),"Stale native command mutated cell");r.runGeneration=1;
        --r.generatedMonitorEnd;check(!rt([&]{return q.request(r);}),"Horizon command before generation");++r.generatedMonitorEnd;
        check(rt([&]{return q.request(r);}),"Native command publication");r.kind=MonitorCaptureStopKind::Cancel;
        check(!rt([&]{return q.request(r);}) && q.originalRequest()->kind==MonitorCaptureStopKind::Horizon,"Immutable native command overwritten");
        WasapiCaptureEnd end;end.started=true;end.stopHresult=-17;end.failure=WasapiStreamFailure{-17,4};
        check(rt([&]{return q.complete(end);}) && !rt([&]{return q.complete({});}),"Native end overwrite");
        check(q.endReceipt()->status==MonitorCaptureEndStatus::NativeFailure && q.endReceipt()->original.stopHresult==-17,"Native failed Stop normalized");
        q.settleAfterJoin();check(q.endReceipt()->original.stopHresult==-17,"Joined settlement replaced SDK failure");
    }
    check(!ledger.usage().reservedBytes,"Joined Stop credit leaked");
    for(unsigned n=0;n<4;++n) {
        PreparedMonitorCaptureStop q(c.stop);WasapiCaptureEnd e;
        if(n==0)e.started=true;else if(n==1)e.stopHresult=0;else if(n==2){e.started=true;e.stopHresult=0;e.deliveredFrames=1;}
        if(n<3){q.complete(e);check(q.endReceipt()->status==MonitorCaptureEndStatus::InvalidReceipt,"Inconsistent owner receipt healthy");}
        else {q.settleAfterJoin();check(q.endReceipt()->status==MonitorCaptureEndStatus::OwnerLost && !q.endReceipt()->original.stopHresult,"Missing owner fabricated Stop");}
    }
}
float sample(std::uint64_t n) { return float(int(n%71)-35)/11.f; }
struct Temp {
    std::filesystem::path path=std::filesystem::temp_directory_path()/Id::generate().str();
    Temp(){std::filesystem::create_directory(path);}
    ~Temp(){if(!std::uncaught_exceptions()){std::error_code e;std::filesystem::remove_all(path,e);}else std::cerr<<"Retained stop fixture: "<<path<<'\n';}
};
void pipeline(unsigned length,unsigned kind,bool nativeContext=false,bool nativeRender=false) {
    Config c(nativeContext);PreparedCaptureFanout f(c.capture);PreparedMonitorOutputBank o(c.output);
    PreparedMonitorCaptureStop stop(c.stop);MonitorControlConfig qc;PreparedMonitorControl control(qc);
    MonitorClockHandoffConfig hc;hc.captureIdentity=c.capture.capture.identity;
    hc.renderIdentity=c.processing.drift.renderIdentity;hc.renderFrequency=c.processing.drift.renderFrequency;
    PreparedMonitorClockHandoff clocks(hc);
    WasapiPreparedCaptureFormat format{c.capture.capture.sampleRate,1,64,4,false,c.capture.capture.identity};
    PreparedWasapiMonitorCapture context(format,f,clocks,stop);PreparedWasapiCaptureBinding binding;
    check(!nativeContext || binding.bind(context,format),"Production context binding");
    WasapiPreparedRenderFormat renderFormat{c.output.sampleRate,1,128,64,4,hc.renderFrequency,{10000,30000,0,0},hc.renderIdentity};
    PreparedWasapiMonitorRender renderContext(renderFormat,o,clocks);PreparedWasapiRenderBinding renderBinding;
    check(!nativeRender || (nativeContext && renderBinding.bind(renderContext,renderFormat)),"Composed render context binding");
    const auto renderCallbacks=renderBinding.callbacks();
    PreparedWasapiRenderLifecycle renderLife({renderCallbacks.context,renderCallbacks.finished,renderCallbacks.shouldStop});renderLife.prepared(renderFormat);
    PreparedMonitorProcessing p(f,o,c.processing,nativeContext?&clocks:nullptr,&control,&stop);seed(p,c);
    const MonitorControlRequest intent{1,1,MonitorControlKind::Stop,{base+length}};
    check(control.submit(intent)==MonitorControlCode::Accepted,"Pipeline intent");
    Temp temp;OriginalRecordingSpec spec;spec.identity=c.capture.capture.identity;spec.maximumPacketFrames=64;spec.sampleRate=c.capture.capture.sampleRate;
    OriginalCaptureWriter writer(temp.path,spec);std::exception_ptr diskError;
    struct Native {
        PreparedCaptureFanout &f;PreparedMonitorCaptureStop &stop;WasapiCaptureCallbacks bound;bool useContext;
        std::array<float,64> sdk{};unsigned releases=0,callbacks=0;bool bad=false;
        static bool poll(void *ctx) noexcept { auto &s=*static_cast<Native *>(ctx);return s.useContext?s.bound.shouldStop(s.bound.context):s.stop.requested() || s.f.status()!=CaptureFanoutStatus::Running; }
        static std::int32_t release(void *ctx,std::uint32_t frames) noexcept { auto &s=*static_cast<Native *>(ctx);++s.releases;if(frames!=64)s.bad=true;s.sdk.fill(-999.f);return 0; }
        static void packet(void *ctx,const WasapiPacket &r) noexcept {
            auto &s=*static_cast<Native *>(ctx);++s.callbacks;if(s.callbacks!=s.releases)s.bad=true;
            if(s.useContext){s.bound.packet(s.bound.context,r);return;}
            const CaptureBankStamp stamp{s.f.captureConfig().identity,{r.devicePosition},{r.qpc100ns},r.frames,r.flags};
            const auto result=s.f.publish({stamp,r.data,r.bytes});if(result.rawAcceptedFrames!=r.frames || result.monitorAcceptedFrames!=r.frames)s.bad=true;
        }
        static void finish(void *ctx,const WasapiCaptureEnd &r) noexcept {
            auto &s=*static_cast<Native *>(ctx);
            // Publish the ACTUAL SDK result before EOF, so processing cannot
            // complete a healthy run using an EOF that hides failed native Stop.
            if(s.useContext){s.bound.finished(s.bound.context,r);return;}
            if(!s.stop.complete(r))s.bad=true;
            s.f.finish();
        }
    } native{f,stop,binding.callbacks(),nativeContext};
    PreparedWasapiPacketCopy copy(1,64);PreparedWasapiCaptureLifecycle life({&native,Native::poll,Native::finish});
    constexpr unsigned rounds=400;std::barrier phase(4);std::atomic<bool> failed{false};
    std::array<rt_audit::Counts,4> audits{};std::vector<float> rendered;rendered.reserve(length);
    std::uint64_t rawFrames=0;bool ended=false;
    std::thread capture([&]{
        rt_audit::reset();rt([&]{life.started();});
        std::uint64_t offset=0;
        for(unsigned r=0;r<rounds;++r) {
            if(!ended) {
                if(rt([&]{return life.shouldStop();})) {
                    const bool sdkFailure=kind==2 || kind==3 || kind==5;
                    rt([&]{if(sdkFailure && nativeContext)native.bound.unavailable(native.bound.context,-17);life.stopped(sdkFailure?-17:0);life.finish(sdkFailure?std::optional(WasapiStreamFailure{-17,4}):std::nullopt);});ended=true;
                }else {
                    for(unsigned n=0;n<64;++n)native.sdk[n]=(kind==1 || kind==3) && offset==512 && n==3?std::bit_cast<float>(std::uint32_t{0x7fc01234}):sample(offset+n);
                    const WasapiPacket packet{reinterpret_cast<const std::byte *>(native.sdk.data()),sizeof(native.sdk),64,(offset || nativeContext)?0u:wasapiDiscontinuity,origin+offset,now+(nativeContext?offset*10000000/64000:offset*211)};
                    const auto d=rt([&]{return copy.deliver(packet,{&native,Native::release,Native::packet});});
                    if(!d.callbackInvoked || d.releaseHresult<0)failed.store(true);else rt([&]{life.delivered(packet);});
                    offset+=64;
                }
            }
            phase.arrive_and_wait();phase.arrive_and_wait();phase.arrive_and_wait();
        }
        if(!ended) {failed.store(true);rt([&]{life.stopped(0);life.finish({});});}
        audits[0]=rt_audit::counts;
    });
    std::thread disk([&]{
        rt_audit::reset();
        for(unsigned r=0;r<rounds;++r) {
            phase.arrive_and_wait();
            if(!diskError)try {CaptureBankView v;const auto code=rt([&]{return f.acquireRaw(v);});
                if(code==CaptureBankRead::Ready){writer.append(v);rawFrames+=v.token.original.frames;if(!rt([&]{return f.releaseRaw(v);}))failed.store(true);}
                else if(code!=CaptureBankRead::Empty && code!=CaptureBankRead::Drained)failed.store(true);
            }catch(...){diskError=std::current_exception();failed.store(true);}
            phase.arrive_and_wait();phase.arrive_and_wait();
        }
        audits[1]=rt_audit::counts;
    });
    std::atomic<bool> cancelPoint{false};bool cancelSubmitted=false;
    std::thread processing([&]{
        rt_audit::reset();
        for(unsigned r=0;r<rounds;++r) {
            phase.arrive_and_wait();
            for(unsigned n=0;n<16 && !p.statistics().done && !p.firstFault();++n) {
                rt([&]{return p.step({now+(nativeContext?std::uint64_t(r)*10000:std::uint64_t(r)*64*211)});});
                // Validate the original SDK end before pumping the separate
                // clock failure, so CaptureEnd remains the first actual cause.
                if(nativeContext && !p.firstFault())rt([&]{return p.pumpClocks();});
            }
            if(p.statistics().buffer.generatedOutputFrames>=64)cancelPoint.store(true,std::memory_order_release);
            phase.arrive_and_wait();phase.arrive_and_wait();
        }
        audits[2]=rt_audit::counts;
    });
    rt_audit::reset();std::array<float,17> scratch{};
    std::array<float,66> nativeOut{};std::uint64_t nativeFrames=0;bool renderEnded=false,renderStarted=false;
    for(unsigned r=0;r<rounds;++r) {
        phase.arrive_and_wait();phase.arrive_and_wait();
        if(kind>=4 && !cancelSubmitted && cancelPoint.load(std::memory_order_acquire)) {
            const MonitorControlRequest cancel{1,2,MonitorControlKind::Cancel,{base+length}};
            if(rt([&]{return control.submit(cancel);})!=MonitorControlCode::Accepted)failed.store(true);
            cancelSubmitted=true;
        }
        // Fake SDK pacing deliberately waits for a complete bank block. This
        // exercises composed owners; it is NOT a native startup/deadline policy.
        if(nativeRender && !renderEnded && (o.acceptedFrames()-o.deliveredFrames()>=64 || o.producerDone())) {
            if(!renderStarted){rt([&]{renderLife.started();});renderStarted=true;}
            nativeOut.fill(-999.f);const auto before=o.deliveredFrames();
            const WasapiRenderClock clock{nativeFrames,origin+std::uint64_t(r)*64,hc.renderFrequency,now+std::uint64_t(r)*10000,0,nativeFrames,0};
            const auto action=rt([&]{return renderCallbacks.fill(renderCallbacks.context,nativeOut.data()+1,64,clock);});
            const auto consumed=o.deliveredFrames()-before;
            check(nativeOut.front()==-999.f && nativeOut.back()==-999.f,"Composed native fill crossed SDK lease");
            if(consumed)rendered.insert(rendered.end(),nativeOut.begin()+1,nativeOut.begin()+1+consumed);
            rt([&]{renderLife.contentReleased({clock,64,action==WasapiRenderAction::Abort?0u:64u,action,0,true});});
            if(action!=WasapiRenderAction::Abort)nativeFrames+=64;
            if(action==WasapiRenderAction::Finish) {
                for(unsigned n=unsigned(consumed);n<64;++n)check(nativeOut[n+1]==0,"Composed finite slack not zero");
                check(!renderContext.result() && !clocks.renderDone(),"Composed content Finish hid SDK drain/Stop");
                rt([&]{renderLife.padding(0);renderLife.queueDrained();renderLife.stopped(0);renderLife.finish({});});renderEnded=true;
            }else if(action==WasapiRenderAction::Abort){failed.store(true);rt([&]{renderLife.stopped(0);renderLife.finish({});});renderEnded=true;}
        }
        for(unsigned n=0;!nativeRender && n<4;++n)if(o.acceptedFrames()-o.deliveredFrames()>=17 || o.producerDone()) {
            const auto result=rt([&]{return o.pull(scratch);});
            if(result.frames)rendered.insert(rendered.end(),scratch.begin(),scratch.begin()+result.frames);
        }
        phase.arrive_and_wait();
    }
    audits[3]=rt_audit::counts;capture.join();disk.join();processing.join();
    if(nativeContext) {
        context.settleAfterJoin();
        if(nativeRender){
            renderContext.settleAfterJoin();const auto result=renderContext.result();
            check(renderEnded && result && result->status==WasapiMonitorRenderStatus::Complete && result->original && result->original->queueDrainObserved && result->original->stopHresult==0 && result->consumedMonitorFrames==length && result->requestedNativeFrames==nativeFrames && result->finalSlackFrames==nativeFrames-length && result->monitorEnd->value==base+length && renderBinding.originalEnd(),"Composed original native render receipt/extent lost");
        }else clocks.finishRender();
        clocks.settleAfterJoin();
        check(binding.originalEnd() && binding.originalEnd()->stopHresult==life.afterJoin()->stopHresult &&
            clocks.captureCounts().accepted==native.callbacks,"Original bound SDK end/clock count lost");
    }
    control.finish();p.settleAfterJoin();f.settleAfterJoin();o.settleAfterJoin();stop.settleAfterJoin();control.settleAfterJoin();
    if(diskError)std::rethrow_exception(diskError);
    check(!failed.load() && !native.bad && ended && f.rawDrained() && rawFrames==f.rawAcceptedFrames(),"Source Stop composed owners failed");
    const auto end=stop.endReceipt();check(end && end->original.deliveredFrames==rawFrames && end->original.deliveredPackets==native.callbacks && native.callbacks==native.releases,"SDK delivery/raw/release extent");
    check(stop.originalRequest()->runGeneration==1 && end->original.lastDelivered->devicePosition+64==origin+rawFrames,"Original last SDK metadata");
    const auto saved=writer.finalize(f);const auto verified=inspectOriginalRecording(writer.jobDirectory(),spec);
    std::vector<float> expected(rawFrames);for(std::uint64_t n=0;n<rawFrames;++n)expected[n]=(kind==1 || kind==3) && n==515?std::bit_cast<float>(std::uint32_t{0x7fc01234}):sample(n);
    media_io::SampleHash hash;hash.update(expected);check(saved.finalized && verified.sampleSha256==hash.digest(),"Stopped original disk bytes");
    for(const auto &a:audits)audit(a);
    if(kind==0) {
        check(p.statistics().done && !p.firstFault() && o.drained() && rendered.size()==length && end->status==MonitorCaptureEndStatus::Healthy,"Normal finite capture Stop/drain");
        check(stop.originalRequest()->kind==MonitorCaptureStopKind::Horizon && stop.originalRequest()->generatedMonitorEnd==base+length &&
            p.statistics().buffer.queuedInputFrames<=c.processing.buffer.inputCapacityFrames,"Horizon/postroll admission");
        auto referenceConfig=c.processing.buffer;referenceConfig.inputCapacityFrames=std::max(referenceConfig.inputCapacityFrames,std::uint32_t(expected.size()));
        PreparedMonitorBuffer reference(referenceConfig);
        for(std::size_t at=0;at<expected.size();at+=64)check(reference.push(std::span<const float>(expected).subspan(at,64))==MonitorBufferCode::Accepted,"Reference actual postroll");
        check(reference.finishAt(base+length)==MonitorBufferCode::Accepted,"Reference stopped end");
        std::size_t at=0;
        for(unsigned n=0;n<20000 && at<rendered.size();++n){check(reference.service()!=MonitorBufferCode::Fault,"Reference stopped kernel");std::array<float,64> ref{};const auto v=reference.pull(ref);
            for(unsigned j=0;j<v.frames;++j)check(at<rendered.size() && std::bit_cast<std::uint32_t>(ref[j])==std::bit_cast<std::uint32_t>(rendered[at++]),"Stopped output PCM differs");}
        check(at==length,"Reference stopped output extent");
    }else if(kind==1 || kind==3) {
        check(p.firstFault()->cause==MonitorProcessingCause::Buffer && stop.originalRequest()->kind==MonitorCaptureStopKind::Fault && verified.finitePrefixFrames==515,"Nonfinite source/Stop raw prefix lost");
        if(kind==3)check(end->status==MonitorCaptureEndStatus::NativeFailure && end->original.stopHresult==-17,"Later SDK Stop failure lost original root");
    }else if(kind==2)check(p.firstFault()->cause==MonitorProcessingCause::CaptureEnd && end->status==MonitorCaptureEndStatus::NativeFailure && end->original.stopHresult==-17 && verified.endFault,"Failed actual Stop hidden by EOF");
    else {
        check(cancelSubmitted && p.statistics().cancelled && stop.originalRequest()->kind==MonitorCaptureStopKind::Cancel && rendered.size()<length,"Cancel did not reach capture owner");
        check(end->status==(kind==5?MonitorCaptureEndStatus::NativeFailure:MonitorCaptureEndStatus::Healthy),"Cancellation lost actual native end result");
    }
    std::cout<<(nativeRender?"production capture+render contexts; fake 64k SDK/pacing, synthetic prior clock fit; ":nativeContext?"production capture context; fake 64k source, synthetic prior clock fit; ":"source-stop fake SDK; ")<<"owners=4 phased; kind="<<kind<<" requested="<<length<<" raw="<<rawFrames<<" monitor="<<rendered.size()<<" actual postroll="<<(rawFrames>=length?rawFrames-length:0)<<"\n";
}
} // namespace
int main() {
    try { rt_audit::reset();lifecycle();admissionAndReceipts();pipeline(0,0);pipeline(7,0);pipeline(512,0);pipeline(3072,0);
        pipeline(512,1);pipeline(512,2);pipeline(512,3);pipeline(3072,4);pipeline(3072,5);
        for(unsigned n:{0u,7u,512u,3072u})pipeline(n,0,true);
        for(unsigned kind=1;kind<=5;++kind)pipeline(kind>=4?3072:512,kind,true);
        for(unsigned n:{0u,7u,512u,3072u})pipeline(n,0,true,true);
        audit(rt_audit::counts);
        std::cout<<checks<<" capture Stop lifecycle checks passed; guarded allocate/free/lock=0; fake SDK only\n";return 0;
    }catch(const std::exception &e){rt_audit::active=false;std::cerr<<"Check "<<checks<<": "<<e.what()<<'\n';return 1;}
}
