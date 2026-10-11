// SPDX-License-Identifier: GPL-3.0-only
#include <soundcurrent/monitor_processing.hpp>
#include "rt_audit.hpp"
#include <array>
#include <bit>
#include <cmath>
#include <iostream>
#include <thread>
#include <chrono>
using namespace soundcurrent::daw;
namespace {
unsigned checks = 0;
void check(bool ok, const char *why) { ++checks; if(!ok) throw std::runtime_error(why); }
template<class F> auto rt(F f) { rt_audit::Guard g; return f(); }
template<class F> void refuses(F f) { try { f(); } catch(const ProjectError &) { check(true,"refused"); return; } check(false,"Bad configuration accepted"); }
struct Config {
    CaptureFanoutConfig capture;
    MonitorOutputBankConfig output;
    MonitorProcessingConfig processing;
    Config(unsigned channels = 3) {
        capture.capture.identity = {11,31,7}; capture.capture.channels = channels;
        capture.capture.maximumPacketFrames = 64; capture.capture.slots = 32; capture.monitorSlots = 32;
        output.channels = channels; output.slots = 1; output.maximumPullFrames = 64;
        output.first.value = std::uint64_t(1) << 54;
        auto &b = processing.buffer; b.channels = channels; b.maximumInputFrames = 64;
        b.inputCapacityFrames = 2048; b.outputCapacityFrames = 64; b.outputFrameBase = output.first.value;
        auto &d = processing.drift; d.captureIdentity = capture.capture.identity;
        d.inputCapacityFrames = b.inputCapacityFrames; d.targetQueuedSourceFrames = 1024;
        d.outputFrameBase = b.outputFrameBase; d.occupancyGainPpmPerFrame = 0;
    }
};
constexpr std::uint64_t now = (std::uint64_t(1) << 54) + 40000000;
void seed(PreparedMonitorProcessing &p, const Config &c) {
    const auto origin = std::uint64_t(1) << 63;
    for(unsigned n = 0; n != 3; ++n) {
        const auto q = now - 40000000 + std::uint64_t(n)*20000000;
        rt([&]{ p.observe(CaptureClockReading{c.processing.drift.captureIdentity,{origin+n*96000},{q}});
                p.observe(RenderClockReading{c.processing.drift.renderIdentity,{origin+n*96000},{q},48000}); });
    }
}
std::vector<float> signal(unsigned channels, unsigned frames = 1024) {
    std::vector<float> pcm(std::size_t(channels)*frames);
    for(unsigned n=0;n<frames;++n)for(unsigned ch=0;ch<channels;++ch)
        pcm[std::size_t(n)*channels+ch] = float(2.5*std::sin(double(n)*.027+ch*.1));
    return pcm;
}
void publish(PreparedCaptureFanout &f,const Config &c,std::span<const float> pcm,unsigned frames=1024) {
    for(unsigned n=0;n<frames;n+=64) {
        const auto count=std::min(64u,frames-n);
        const CaptureBankStamp stamp{c.capture.capture.identity,{(std::uint64_t(1)<<63)+n},{now+n*211},count,0};
        const auto bytes=pcm.subspan(std::size_t(n)*c.output.channels,std::size_t(count)*c.output.channels);
        const auto r=rt([&]{return f.publish({stamp,reinterpret_cast<const std::byte *>(bytes.data()),bytes.size_bytes()});});
        check(r.rawAcceptedFrames==count && r.monitorAcceptedFrames==count,"Synthetic capture publication");
    }
    rt([&]{f.finish();});
}
void drainRaw(PreparedCaptureFanout &f,std::span<const float> expected,unsigned channels) {
    std::size_t at=0;
    for(;;) {
        CaptureBankView v;const auto code=rt([&]{return f.acquireRaw(v);});
        if(code==CaptureBankRead::Drained)break;
        check(code==CaptureBankRead::Ready,"Raw packet missing");
        for(float x:v.interleaved)check(std::bit_cast<std::uint32_t>(x)==std::bit_cast<std::uint32_t>(expected[at++]),"Raw bits changed by monitor");
        check(rt([&]{return f.releaseRaw(v);}),"Raw release");
    }
    check(at==expected.size() && at/channels==f.rawAcceptedFrames(),"Raw extent differs");
}
void parityAndPressure(unsigned channels) {
    Config c(channels);ResourceLedger ledger(256*1024*1024);c.processing.resources=ledger;
    const auto pcm=signal(channels);PreparedCaptureFanout f(c.capture);PreparedMonitorOutputBank o(c.output);
    std::vector<float> actual;actual.reserve(std::size_t(512)*channels);
    {
        PreparedMonitorProcessing p(f,o,c.processing);seed(p,c);publish(f,c,pcm);
        check(!rt([&]{return p.finishAt({c.output.first.value+512});}),"Finished before consuming original monitor route");
        bool end=false,pressure=false;std::vector<float> scratch(std::size_t(64)*channels,999.f);
        for(unsigned i=0;i<20000 && !p.statistics().done;++i) {
            const auto code=rt([&]{return p.step({now});});
            check(code!=MonitorProcessingCode::Fault && code!=MonitorProcessingCode::Cancelled,"Processing failed");
            if(code==MonitorProcessingCode::NeedEnd && !end) {
                check(rt([&]{return p.finishAt({c.output.first.value+512});}),"Explicit postroll end refused");end=true;
            }
            if(code==MonitorProcessingCode::OutputFull) {
                pressure=true;const auto before=p.statistics();
                check(rt([&]{return p.step({now});})==MonitorProcessingCode::OutputFull,"Pressure retry changed result");
                const auto after=p.statistics();
                check(before.buffer==after.buffer && before.pendingOutputFrames==after.pendingOutputFrames &&
                      before.serviceCalls==after.serviceCalls && before.publishedFrames==after.publishedFrames,
                      "Pending output pressure advanced input/controller/DSP");
            }
            // Deliberately leave the single output slab occupied until pressure.
            if(code==MonitorProcessingCode::OutputFull || code==MonitorProcessingCode::Done) {
                const auto r=rt([&]{return o.pull(scratch);});
                if(r.frames)actual.insert(actual.end(),scratch.begin(),scratch.begin()+std::ptrdiff_t(std::size_t(r.frames)*channels));
            }
        }
        // Producer can finish with its last accepted block still queued.
        while(!o.drained()) {const auto r=rt([&]{return o.pull(scratch);});check(r.frames>0,"Output cannot drain");actual.insert(actual.end(),scratch.begin(),scratch.begin()+std::ptrdiff_t(std::size_t(r.frames)*channels));}
        check(end && pressure && p.statistics().done && p.statistics().publishedFrames==512 && actual.size()==std::size_t(512)*channels,"Processing end/pressure/extent");
        drainRaw(f,pcm,channels);p.settleAfterJoin();f.settleAfterJoin();o.settleAfterJoin();
        check(ledger.usage().reservedBytes==p.chargedBytes(),"Processing group not charged exactly");
    }
    check(!ledger.usage().reservedBytes && !ledger.usage().owners,"Processing group credit leaked");
    // Independent direct buffer reference, ratio 1, same real postroll, a different
    // partition (all input before rendering) and no transport/processing wrapper.
    PreparedMonitorBuffer reference(c.processing.buffer);
    for(unsigned n=0;n<1024;n+=64)check(reference.push(std::span<const float>(pcm).subspan(std::size_t(n)*channels,std::size_t(64)*channels))==MonitorBufferCode::Accepted,"Reference input");
    check(reference.finishAt(c.output.first.value+512)==MonitorBufferCode::Accepted,"Reference end");
    std::vector<float> expected,scratch(std::size_t(64)*channels);expected.reserve(actual.size());
    for(unsigned i=0;i<20000 && expected.size()<actual.size();++i) {
        check(reference.service()!=MonitorBufferCode::Fault,"Reference postroll");
        const auto r=reference.pull(scratch);expected.insert(expected.end(),scratch.begin(),scratch.begin()+std::ptrdiff_t(std::size_t(r.frames)*channels));
    }
    check(expected.size()==actual.size(),"Reference output extent");
    for(std::size_t n=0;n<actual.size();++n)check(std::bit_cast<std::uint32_t>(actual[n])==std::bit_cast<std::uint32_t>(expected[n]),"Partitioned composed PCM differs");
    std::cout<<"processing channels="<<channels<<" rawFrames=1024 outputFrames=512 comparedOutputBits="<<actual.size()<<" retained-pressure exact retry\n";
}
void faultsAndAdmission() {
    {
        Config c;PreparedCaptureFanout f(c.capture);PreparedMonitorOutputBank o(c.output);
        ResourceLedger tiny(1);c.processing.resources=tiny;
        refuses([&]{PreparedMonitorProcessing p(f,o,c.processing);});
        check(!tiny.usage().reservedBytes && !tiny.usage().owners,"Refused processing credit leaked");
        ResourceLedger admitted(256*1024*1024);c.processing.resources=admitted;
        c.processing.drift.maximumConfidencePpm=0;
        refuses([&]{PreparedMonitorProcessing p(f,o,c.processing);});
        check(!admitted.usage().reservedBytes && !admitted.usage().owners,"Partial preparation credit leaked");
    }
    for(unsigned kind=0;kind<8;++kind) {
        Config c;PreparedCaptureFanout f(c.capture);PreparedMonitorOutputBank o(c.output);
        auto bad=c.processing;
        if(kind==0)++bad.buffer.channels;
        if(kind==1)++bad.buffer.sourceRate;
        if(kind==2)++bad.buffer.runGeneration;
        if(kind==3)++bad.drift.captureIdentity.generation;
        if(kind==4)++bad.drift.inputCapacityFrames;
        if(kind==5)bad.drift.maximumConfidencePpm=0;
        if(kind==6)bad.memoryBudgetBytes=1;
        if(kind==7)bad.buffer.resources=ResourceLedger(10000000);
        refuses([&]{PreparedMonitorProcessing p(f,o,bad);});
    }
    for(unsigned kind=0;kind<6;++kind) {
        Config c;PreparedCaptureFanout f(c.capture);PreparedMonitorOutputBank o(c.output);PreparedMonitorProcessing p(f,o,c.processing);
        auto pcm=signal(3);seed(p,c);
        if(kind==0)pcm[17]=std::bit_cast<float>(std::uint32_t{0x7fc01234});
        publish(f,c,pcm);
        if(kind==1)rt([&]{o.signalUnderflow(64);});
        if(kind==2)rt([&]{p.cancel();});
        if(kind==3)rt([&]{p.observe(RenderClockReading{c.processing.drift.renderIdentity,{0},{now+1},48000});});
        if(kind==4)rt([&]{p.settleAfterJoin();});
        const auto code=rt([&]{return p.step({kind==5?now+c.processing.drift.maximumAge100ns+1:now});});
        check(code==(kind==2?MonitorProcessingCode::Cancelled:MonitorProcessingCode::Fault),"Monitor loss not terminal");
        const auto first=p.firstFault();rt([&]{p.step({now});});check(first==p.firstFault(),"Fault erased");
        drainRaw(f,pcm,3);p.settleAfterJoin();f.settleAfterJoin();o.settleAfterJoin();
        check(f.rawAcceptedFrames()==1024,"Monitor failure shortened raw route");
    }
}
void clockStartupAndPostroll() {
    for(unsigned kind=0;kind<3;++kind) {
        Config c;PreparedCaptureFanout f(c.capture);PreparedMonitorOutputBank o(c.output);PreparedMonitorProcessing p(f,o,c.processing);
        const auto pcm=signal(3);publish(f,c,pcm);
        bool waiting=false;
        for(unsigned i=0;i<100;++i) {
            const auto code=rt([&]{return p.step({now});});
            if(code==MonitorProcessingCode::WaitingClock){waiting=true;break;}
            check(code!=MonitorProcessingCode::Fault,"Startup fault before fit");
        }
        check(waiting && !p.statistics().buffer.generatedOutputFrames && !o.acceptedFrames(),"Started monitor without clock confidence");
        seed(p,c);
        for(unsigned i=0;i<100;++i) {
            const auto code=rt([&]{return p.step({now});});
            if(code==MonitorProcessingCode::NeedEnd)break;
        }
        const auto end=c.output.first.value+(kind==0?4096u:512u);
        check(rt([&]{return p.finishAt({end});}),"Explicit end after clock startup");
        if(kind==1) {
            check(rt([&]{return p.step({now+1});})==MonitorProcessingCode::Progress,"Fresh control time rejected");
            check(rt([&]{return p.step({now});})==MonitorProcessingCode::Fault && p.firstFault()->cause==MonitorProcessingCause::ControlTime,"Backward control time accepted");
        } else if(kind==2) {
            // Force a retained pending block, then cancel. Discard monitor only.
            for(unsigned i=0;i<100;++i)if(rt([&]{return p.step({now});})==MonitorProcessingCode::OutputFull)break;
            const auto before=p.statistics().pendingOutputFrames;check(before>0,"No pending cancellation fixture");
            rt([&]{p.cancel();});check(p.statistics().discardedPendingFrames==before && !p.statistics().pendingOutputFrames,"Pending cancellation count lost");
        } else {
            bool fault=false;std::array<float,192> scratch{};
            for(unsigned i=0;i<1000;++i) {
                const auto code=rt([&]{return p.step({now});});
                rt([&]{return o.pull(scratch);});
                if(code==MonitorProcessingCode::Fault){fault=true;break;}
            }
            check(fault && p.bufferFault()->code==MonitorBufferFaultCode::InsufficientInput,"Fabricated postroll instead of failing");
        }
        drainRaw(f,pcm,3);p.settleAfterJoin();f.settleAfterJoin();o.settleAfterJoin();
    }
}
void concurrentOwners() {
    Config c;unsigned inputFrames=0;for(unsigned n=0;n<256;++n)inputFrames+=1+n%31;
    const auto pcm=signal(3,inputFrames);PreparedCaptureFanout f(c.capture);PreparedMonitorOutputBank o(c.output);PreparedMonitorProcessing p(f,o,c.processing);seed(p,c);
    std::atomic<std::uint64_t> rawSeen{0},processingSeen{0};std::atomic<bool> failed{false};
    std::array<rt_audit::Counts,3> audits{};std::vector<float> rendered;rendered.reserve(3072*3);
    const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(10);
    const auto expired=[&]{return failed.load() || std::chrono::steady_clock::now()>=deadline;};
    std::thread raw([&]{
        rt_audit::reset();std::size_t offset=0;
        while(!expired()) {
            CaptureBankView v;const auto code=rt([&]{return f.acquireRaw(v);});
            if(code==CaptureBankRead::Drained)break;
            if(code==CaptureBankRead::Empty){std::this_thread::yield();continue;}
            if(code!=CaptureBankRead::Ready){failed.store(true);break;}
            for(float x:v.interleaved)if(std::bit_cast<std::uint32_t>(x)!=std::bit_cast<std::uint32_t>(pcm[offset++]))failed.store(true);
            if(!rt([&]{return f.releaseRaw(v);}))failed.store(true);
            rawSeen.store(offset/3,std::memory_order_release);
        }
        if(rawSeen.load()!=inputFrames)failed.store(true);
        audits[0]=rt_audit::counts;
    });
    std::thread processing([&]{
        rt_audit::reset();bool closing=false;
        while(!expired()) {
            const auto code=rt([&]{return p.step({now});});
            processingSeen.store(p.statistics().buffer.acceptedInputFrames,std::memory_order_release);
            if(code==MonitorProcessingCode::NeedEnd && !closing){closing=rt([&]{return p.finishAt({c.output.first.value+3072});});if(!closing)failed.store(true);}
            if(code==MonitorProcessingCode::Done)break;
            if(code==MonitorProcessingCode::Fault || code==MonitorProcessingCode::Cancelled){failed.store(true);break;}
            std::this_thread::yield();
        }
        if(!p.statistics().done)failed.store(true);
        audits[1]=rt_audit::counts;
    });
    std::thread render([&]{
        rt_audit::reset();std::array<float,51> scratch{};
        while(!expired()) {
            const auto r=rt([&]{return o.pull(scratch);});
            if(r.phase==MonitorOutputPhase::Fault || r.phase==MonitorOutputPhase::Cancelled){failed.store(true);break;}
            if(r.code==MonitorOutputCode::Drained)break;
            if(r.frames) {
                if(r.first.value!=c.output.first.value+rendered.size()/3)failed.store(true);
                rendered.insert(rendered.end(),scratch.begin(),scratch.begin()+std::ptrdiff_t(r.frames*3));
            }
            std::this_thread::yield();
        }
        if(rendered.size()!=3072*3)failed.store(true);
        audits[2]=rt_audit::counts;
    });
    std::array<float,93> scratch{};std::uint64_t offset=0,previous=0;
    for(unsigned n=0;n<256 && !expired();++n) {
        while(std::min(rawSeen.load(std::memory_order_acquire),processingSeen.load(std::memory_order_acquire))<previous && !expired())std::this_thread::yield();
        if(expired())break;
        const auto count=1+n%31;std::copy_n(pcm.data()+std::size_t(offset)*3,std::size_t(count)*3,scratch.data());
        const CaptureBankStamp stamp{c.capture.capture.identity,{(std::uint64_t(1)<<63)+offset},{now+offset*211},count,0};
        const auto r=rt([&]{return f.publish({stamp,reinterpret_cast<const std::byte *>(scratch.data()),std::size_t(count)*3*sizeof(float)});});
        if(r.rawAcceptedFrames!=count || r.monitorAcceptedFrames!=count)failed.store(true);
        previous=offset;offset+=r.rawAcceptedFrames;scratch.fill(999.f);
    }
    rt([&]{f.finish();});raw.join();processing.join();render.join();p.settleAfterJoin();f.settleAfterJoin();o.settleAfterJoin();
    check(!failed.load() && rawSeen==inputFrames && p.statistics().done && o.deliveredFrames()==3072,"Concurrent processing owners failed");
    for(const auto &a:audits)check(!a.cppAllocate && !a.cppFree && !a.cAllocate && !a.cFree && !a.blockingLock,"Concurrent processing handoff audit");
    auto referenceConfig=c.processing.buffer;referenceConfig.inputCapacityFrames=8192;
    PreparedMonitorBuffer reference(referenceConfig);
    for(unsigned n=0,at=0;n<256;++n){const auto count=1+n%31;check(reference.push(std::span<const float>(pcm).subspan(std::size_t(at)*3,std::size_t(count)*3))==MonitorBufferCode::Accepted,"Concurrent reference capacity");at+=count;
    }
    check(reference.finishAt(c.output.first.value+3072)==MonitorBufferCode::Accepted,"Concurrent reference end");
    std::size_t compared=0;std::array<float,192> referenceScratch{};
    for(unsigned i=0;i<20000 && compared<rendered.size();++i) {
        check(reference.service()!=MonitorBufferCode::Fault,"Concurrent reference postroll");
        const auto r=reference.pull(referenceScratch);
        for(unsigned n=0;n<r.frames*3;++n) {
            check(compared<rendered.size() && std::bit_cast<std::uint32_t>(referenceScratch[n])==std::bit_cast<std::uint32_t>(rendered[compared]),"Concurrent partition PCM differs");++compared;
        }
    }
    check(compared==rendered.size(),"Concurrent reference output extent");
    std::cout<<"concurrent owners=4 packets=256 rawFrames="<<inputFrames<<" outputFrames=3072; source credit waits and synthetic time outside prepared methods\n";
}
} // namespace
int main() {
    try {
        rt_audit::reset();parityAndPressure(3);parityAndPressure(256);faultsAndAdmission();clockStartupAndPostroll();concurrentOwners();
        check(!rt_audit::counts.cppAllocate && !rt_audit::counts.cppFree && !rt_audit::counts.cAllocate &&
              !rt_audit::counts.cFree && !rt_audit::counts.blockingLock,"Prepared processing RT audit");
        std::cout<<checks<<" bounded monitor processing checks passed; guarded allocate/free/lock=0; synthetic clock observations only\n";return 0;
    }catch(const std::exception &e){rt_audit::active=false;std::cerr<<"Check "<<checks<<": "<<e.what()<<'\n';return 1;}
}
