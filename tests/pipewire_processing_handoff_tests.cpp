// SPDX-License-Identifier: GPL-3.0-only
#include <soundcurrent/pipewire_capture_stop.hpp>
#include <soundcurrent/pipewire_recording_journal.hpp>
#include <soundcurrent/monitor_processing.hpp>
#include "rt_audit.hpp"
#include <array>
#include <bit>
#include <cmath>
#include <iostream>
#include <thread>
#include <atomic>
#include <barrier>
using namespace soundcurrent::daw;
namespace {
unsigned checks=0;
void check(bool b,const char *why){++checks;if(!b)throw std::runtime_error(why);}
template<class F>auto rt(F f){rt_audit::Guard guard;return f();}
template<class F>void refuses(F f){try{f();}catch(const ProjectError &){++checks;return;}throw std::runtime_error("Invalid handoff binding accepted");}
#ifndef SC_HANDOFFS_ONLY
DeviceBlockClock clock(unsigned n){return {100000+std::uint64_t(n)*480,480,4000000000ULL+std::uint64_t(n)*10000000,19,n+4,1,48000,-37,false,false};}
#endif
void stopFacts(){
    MonitorCaptureStopConfig cfg;cfg.maximumPacketFrames=480;cfg.channels=1;cfg.monitorSlots=8;cfg.inputCapacityFrames=16384;
    PreparedPipeWireCaptureStop missing(cfg);rt([&]{missing.settleAfterJoin();});
    check(missing.endStatus()==MonitorCaptureEndStatus::OwnerLost && !missing.endReceipt()->original,"Lost native owner invented receipt");
    for(unsigned mode=0;mode<3;++mode){PreparedPipeWireCaptureStop stop(cfg);PipeWireCaptureEnd end;end.callbacksJoined=mode!=1;
        if(mode==2)end.unavailable=AudioBridgeStatus::DeviceLost;
        check(rt([&]{return stop.complete(end);}) && !rt([&]{return stop.complete(end);}),"Native end overwritten");
        check(stop.endStatus()==(mode==0?MonitorCaptureEndStatus::Healthy:mode==1?MonitorCaptureEndStatus::InvalidReceipt:MonitorCaptureEndStatus::NativeFailure),"Native end status wrong");
        check(stop.endReceipt()->original->unavailable==end.unavailable,"Original PipeWire end altered");}
    PreparedPipeWireCaptureStop stop(cfg);auto foreign=MonitorCaptureStopRequest{2,MonitorCaptureStopKind::Cancel,{},0,0};
    check(!rt([&]{return stop.request(foreign);}),"Foreign processing stop accepted");
    check(rt([&]{return stop.request({1,MonitorCaptureStopKind::Horizon,2400,2400,960});}) && stop.originalRequest()->acceptedMonitorInputFrames==960,"Stop request altered");
    check(!rt([&]{return stop.request({1,MonitorCaptureStopKind::Cancel,{},0,0});}),"Stop request overwritten");
}
void concurrentStop(){
    MonitorCaptureStopConfig cfg;PreparedPipeWireCaptureStop stop(cfg);std::atomic<bool> okay{true};std::barrier start(2);std::array<rt_audit::Counts,2> audits{};
    std::thread processing([&]{
        rt_audit::reset();
        start.arrive_and_wait();
        if(!rt([&]{return stop.request({1,MonitorCaptureStopKind::Cancel,{},17,31});}))okay=false;
        for(unsigned n=0;!stop.endStatus() && n<1000000;++n)std::this_thread::yield();
        if(rt([&]{return stop.endStatus();})!=MonitorCaptureEndStatus::Healthy)okay=false;
        const auto e=rt([&]{return stop.endReceipt();});if(!e || !e->original || !e->original->callbacksJoined)okay=false;
        audits[0]=rt_audit::counts;
    });
    std::thread native([&]{
        rt_audit::reset();
        start.arrive_and_wait();
        for(unsigned n=0;!stop.requested() && n<1000000;++n)std::this_thread::yield();
        const auto request=rt([&]{return stop.originalRequest();});if(!request || request->acceptedMonitorInputFrames!=31)okay=false;
        PipeWireCaptureEnd end;end.callbacksJoined=true;if(!rt([&]{return stop.complete(end);}))okay=false;
        audits[1]=rt_audit::counts;
    });
    processing.join();native.join();check(okay,"Concurrent stop/end immutable publication failed");
    for(const auto &a:audits)check(!a.cppAllocate && !a.cppFree && !a.cAllocate && !a.cFree && !a.blockingLock,"Concurrent handoff allocated/freed/locked under guard");
}
#ifndef SC_HANDOFFS_ONLY
struct Geometry {
    CaptureFanoutConfig capture;
    MonitorOutputBankConfig output;
    MonitorProcessingConfig processing;
    MonitorClockHandoffConfig clocks;
    MonitorCaptureStopConfig stop;
    MonitorStartupAdmission startup;
    Geometry(){
        capture.capture.identity={11,31,7};capture.capture.channels=1;capture.capture.maximumPacketFrames=480;capture.capture.slots=8;capture.monitorSlots=8;
        output.channels=2;output.maximumPullFrames=480;output.slots=64;
        auto &b=processing.buffer;b.channels=1;b.maximumInputFrames=480;b.inputCapacityFrames=16384;
        auto &d=processing.drift;d.captureIdentity=capture.capture.identity;d.inputCapacityFrames=b.inputCapacityFrames;d.occupancyGainPpmPerFrame=0;
        processing.startup=MonitorStartupConfig{960,480,50000000};
        processing.channelMap=MonitorChannelMap{1,2,{{0,0,1},{0,1,-.5}}};
        MonitorEqSpec eq;eq.settings.bands.push_back({Id::generate(),1000,-6,1});processing.equalizer=eq;
        clocks.captureIdentity=capture.capture.identity;clocks.timestampDomain=MonitorClockDomain::PosixMonotonic100ns;
        stop.sourceRate=48000;stop.channels=1;stop.maximumPacketFrames=480;stop.monitorSlots=8;stop.inputCapacityFrames=16384;
        stop.warmupInputFrames=monitorBufferWarmupInputFrames(b);
        startup={1,clocks.captureIdentity,clocks.renderIdentity,48000,48000,2,48000,960,480,50000000,
            monitorChannelMapReceipt(*processing.channelMap),monitorEqReceipt(*processing.equalizer,48000,2)};
    }
};
void readinessGates(){
    for(unsigned mode=0;mode<7;++mode){Geometry g;PreparedMonitorOutputBank bank(g.output);PreparedMonitorClockHandoff clocks(g.clocks);
        PreparedMonitorStartupHandoff startup(g.startup);PipeWireMonitorRenderOptions o;o.startup=&startup;
        PreparedPipeWireMonitorRender render(bank,clocks,o);std::array<float,480> left{},right{};const std::array<float *,2> out{left.data(),right.data()};
        std::array<float,128> block;block.fill(2.F);
        for(unsigned n=0;n<16;++n)check(rt([&]{return bank.publish({1,{std::uint64_t(n)*64},64},block);})==MonitorOutputCode::Accepted,"Gate fixture publication failed");
        check(rt([&]{return render.process(clock(0),out,480,Qpc100ns{clock(0).monotonicNs/100});})==PipeWireRenderCode::Warmup && !bank.deliveredFrames(),"Filled bank bypassed immutable startup admission");
        const std::array<PipeWirePortLeaseResult,2> returned{{{PipeWireBufferStatus::Ready,true,0},{PipeWireBufferStatus::Ready,true,0}}};
        rt([&]{render.completeCycle({clock(0),true,{},returned});});
        if(mode==0 || mode==4 || mode==5 || mode==6){check(rt([&]{return startup.claimProcessingOwner();}),"Readiness publisher refused");
            MonitorStartupStatistics s;s.phase=MonitorStartupPhase::Live;s.contentReady=true;s.capacityFrames=256;s.transferredFrames=s.acceptedFrames=256;
            const CaptureBankStamp stamp{g.capture.capture.identity,{100000},{clock(0).monotonicNs/100},480,0};s.firstTransferred=MonitorStartupSource{stamp,0};s.lastAccepted=stamp;
            check(rt([&]{return startup.publish({g.startup,s,1024,(mode==4 || mode==5)?clock(2).monotonicNs/100:clock(0).monotonicNs/100});}),"Validated readiness fixture refused");
        }else if(mode==1)rt([&]{startup.requestCancel();});
        else if(mode==2)rt([&]{startup.fail();});
        else rt([&]{startup.settleAfterProducerJoin();});
        const auto code=rt([&]{return render.process(clock(1),out,480,mode==6?std::nullopt:std::optional(Qpc100ns{(mode==5?clock(2):clock(1)).monotonicNs/100}));});
        if(mode==0 || mode==5){check(code==PipeWireRenderCode::Content && bank.deliveredFrames()==480,"Ready content not released");
            rt([&]{render.completeCycle({clock(1),true,{},returned});startup.requestCancel();});
            check(rt([&]{return render.process(clock(2),out,480,Qpc100ns{clock(2).monotonicNs/100});})==PipeWireRenderCode::Stopped && bank.deliveredFrames()==480,"Cancellation after Ready consumed more PCM");
        }else check(code==(mode==1?PipeWireRenderCode::Stopped:PipeWireRenderCode::Fault) && !bank.deliveredFrames(),"Terminal/future readiness permitted content");
        render.finishAfterJoin();
    }
    Geometry g;PreparedMonitorOutputBank bank(g.output);PreparedMonitorClockHandoff clocks(g.clocks);auto bad=g.startup;bad.renderIdentity={9,8,7};
    PreparedMonitorStartupHandoff startup(bad);PipeWireMonitorRenderOptions o;o.startup=&startup;
    refuses([&]{PreparedPipeWireMonitorRender render(bank,clocks,o);});
    ResourceLedger tiny(16);auto cfg=g.stop;cfg.resources=tiny;refuses([&]{PreparedPipeWireCaptureStop stop(cfg);});
    check(!tiny.usage().reservedBytes,"Refused handoff leaked payload credit");
}
void combined(){
    Geometry g;const auto root=std::filesystem::temp_directory_path()/("sc-pw-processing-"+Id::generate().str());std::filesystem::create_directory(root);
    const auto project=makeOneTrackSession("Native processing","Raw");ProjectStore(root).save(project);
    try{
        PreparedCaptureFanout f(g.capture);PreparedMonitorOutputBank b(g.output);PreparedMonitorClockHandoff h(g.clocks);
        PreparedPipeWireCaptureStop stop(g.stop);PreparedMonitorStartupHandoff startup(g.startup);
        PreparedMonitorProcessing p(f,b,g.processing,&h,nullptr,&stop,&startup);
        PreparedPipeWireReceiptQueues q;
        PipeWireMonitorCaptureOptions co;co.requireNativeReturns=true;co.receiptContext=&q;co.receipt=PreparedPipeWireReceiptQueues::captureSink;co.stop=&stop;
        PreparedPipeWireMonitorCapture c(f,h,co);
        PipeWireMonitorRenderOptions ro;ro.startup=&startup;ro.receiptContext=&q;ro.receipt=PreparedPipeWireReceiptQueues::renderSink;
        PreparedPipeWireMonitorRender r(b,h,ro);
        PipeWireRecordingSpec spec;spec.raw.project=project.id;spec.raw.track=project.tracks.front().id;spec.raw.identity=g.capture.capture.identity;
        spec.raw.layout={LayoutKind::Mono,1};spec.raw.maximumPacketFrames=480;spec.renderChannels=2;spec.maximumRenderFrames=480;
        PipeWireRecordingJournal writer(root,spec);const auto job=writer.jobDirectory();
        check(rt([&]{return p.planEnd({2400});}),"Finite monitor horizon refused");
        std::array<float,480> input,left{},right{};for(unsigned k=0;k<input.size();++k)input[k]=float(3*std::sin(double(k)*2*3.141592653589793/48));
        const std::array<const float *,1> source{input.data()};const std::array<float *,2> sink{left.data(),right.data()};
        const std::array<PipeWirePortLeaseResult,1> inReturns{{{PipeWireBufferStatus::Ready,true,0}}};
        const std::array<PipeWirePortLeaseResult,2> outReturns{{{PipeWireBufferStatus::Ready,true,0},{PipeWireBufferStatus::Ready,true,0}}};
        bool ended=false;std::uint64_t seen=0,content=0;unsigned warmup=0;
        for(unsigned n=0;n<500 && !r.contentComplete();++n){const auto clk=clock(n);
            if(!ended){check(rt([&]{return c.process(clk,source,480);})==PipeWireCaptureCode::Accepted,"Combined capture fault");
                rt([&]{c.completeCycle({clk,true,inReturns,{}});});
                PipeWireReceiptRecord<PipeWireCaptureCycle> receipt;check(rt([&]{return q.read(receipt);})==PipeWireReceiptCode::Ready,"Capture receipt missing");
                CaptureBankView v;check(rt([&]{return f.acquireRaw(v);})==CaptureBankRead::Ready,"Original raw missing");
                for(unsigned k=0;k<v.interleaved.size();++k)check(std::bit_cast<unsigned>(v.interleaved[k])==std::bit_cast<unsigned>(input[k]),"Monitor processing changed raw headroom");
                writer.appendCapture(receipt,v);seen+=v.token.original.frames;check(rt([&]{return f.releaseRaw(v);}),"Raw release failed");}
            const auto rendered=rt([&]{return r.process(clk,sink,480,Qpc100ns{clk.monotonicNs/100});});
            check(rendered!=PipeWireRenderCode::Fault && rendered!=PipeWireRenderCode::Stopped,"Combined native render fault");
            rt([&]{r.completeCycle({clk,true,{},outReturns});});
            PipeWireReceiptRecord<PipeWireRenderCycle> receipt;check(rt([&]{return q.read(receipt);})==PipeWireReceiptCode::Ready,"Render receipt missing");writer.appendRender(receipt);
            if(rendered==PipeWireRenderCode::Warmup){++warmup;check(!b.deliveredFrames(),"Warmup consumed before startup-ready receipt");}
            if(receipt.original.monitorFrames){content+=receipt.original.monitorFrames;
                double energy=0;
                for(unsigned k=0;k<receipt.original.monitorFrames;++k){energy+=double(left[k])*left[k];check(std::abs(right[k]+.5*left[k])<.001,"Monitor channel map mismatch");}
                if(content>=960){const auto expected=3*std::pow(10.,-6./20.)/std::sqrt(2.);
                    check(std::abs(std::sqrt(energy/receipt.original.monitorFrames)-expected)<.002,"Monitor EQ amplitude/headroom mismatch");}}

            rt([&]{p.pumpClocks();});
            for(unsigned k=0;k<128;++k){const auto code=rt([&]{return p.step({clk.monotonicNs/100});});
                check(code!=MonitorProcessingCode::Fault && code!=MonitorProcessingCode::Cancelled,"Shared monitor processing failed");
                if(code==MonitorProcessingCode::NeedInput || code==MonitorProcessingCode::WaitingClock || code==MonitorProcessingCode::Done || code==MonitorProcessingCode::OutputFull)break;}
            if(stop.requested() && !ended){c.finishAfterJoin();q.finishCapture();ended=true;}
        }
        check(ended && warmup && content==2400 && r.contentComplete() && stop.endStatus()==MonitorCaptureEndStatus::Healthy,"Native horizon/startup/finite completion wrong");
        r.finishAfterJoin();q.finishRender();p.settleAfterJoin();f.settleAfterJoin();writer.finalize(f,q);
        const auto verified=inspectPipeWireRecording(job,spec);check(verified.raw.committedFrames==seen && verified.monitorQueuedFrames==2400,"Paired journal and processed render disagree");
        check(r.startupReadyAfterJoin() && r.startupReadyAfterJoin()->admitted==g.startup,"Native readiness EQ/map provenance lost");
    }catch(...){std::cerr<<"Retained combined fixture "<<root.generic_string()<<'\n';throw;}
    std::filesystem::remove_all(root);
}
#endif
}
int main(){try{rt_audit::reset();stopFacts();
#ifdef SC_HANDOFFS_ONLY
    for(unsigned n=0;n<16;++n)concurrentStop();
#else
    concurrentStop();readinessGates();combined();
#endif
    const auto a=rt_audit::counts;
    check(!a.cppAllocate && !a.cppFree,"Bounded handoff C++ allocation/free");
#ifdef SC_WRAP_LIBC
    check(!a.cAllocate && !a.cFree && !a.blockingLock,"Bounded handoff C allocation/free/lock");
#endif
#ifdef SC_HANDOFFS_ONLY
    std::cout<<checks<<" PipeWire stop publication checks passed;16 actual processing/native thread pairs;SDK=false, physical=false\n";
#else
    std::cout<<checks<<" PipeWire processing handoff checks passed;actual planar boundary/shared EQ+map/startup/stop/raw journal composition;SDK=false, physical=false, native thread owner incomplete\n";
#endif
    return 0;
}catch(const std::exception &e){std::cerr<<"Check "<<checks<<": "<<e.what()<<'\n';return 1;}}
