// SPDX-License-Identifier: GPL-3.0-only
#include <soundcurrent/monitor_processing.hpp>
#include "rt_audit.hpp"
#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <iostream>
#include <thread>
#include <chrono>
#include <limits>
using namespace soundcurrent::daw;
namespace {
unsigned checks = 0;
void check(bool ok, const char *why) { ++checks; if(!ok) throw std::runtime_error(why); }
template<class F> auto rt(F f) { rt_audit::Guard g; return f(); }
template<class F> void refuses(F f) { try { f(); } catch(const ProjectError &) { check(true,"refused"); return; } check(false,"Bad configuration accepted"); }
struct Config {
    CaptureFanoutConfig capture{};
    MonitorOutputBankConfig output{};
    MonitorProcessingConfig processing{};
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
void publish(PreparedCaptureFanout &f,const Config &c,std::span<const float> pcm,unsigned frames=1024,bool finish=true) {
    for(unsigned n=0;n<frames;n+=64) {
        const auto count=std::min(64u,frames-n);
        const CaptureBankStamp stamp{c.capture.capture.identity,{(std::uint64_t(1)<<63)+n},{now+n*211},count,0};
        const auto bytes=pcm.subspan(std::size_t(n)*c.capture.capture.channels,std::size_t(count)*c.capture.capture.channels);
        const auto r=rt([&]{return f.publish({stamp,reinterpret_cast<const std::byte *>(bytes.data()),bytes.size_bytes()});});
        check(r.rawAcceptedFrames==count && r.monitorAcceptedFrames==count,"Synthetic capture publication");
    }
    if(finish)rt([&]{f.finish();});
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
void parityAndPressure(unsigned channels,unsigned mapping=0) {
    Config c(channels);ResourceLedger ledger(256*1024*1024);c.processing.resources=ledger;
    if(mapping==1){c.output.channels=2;c.processing.channelMap=monoToStereoMonitor();}
    if(mapping==2){c.output.channels=1;c.processing.channelMap=MonitorChannelMap{2,1,{{0,0,.5},{1,0,.5}}};}
    if(mapping==3){c.output.channels=3;c.processing.channelMap=MonitorChannelMap{1,3,{{0,0,1},{0,2,-.5}}};}
    const auto outputChannels=c.output.channels;
    const auto pcm=signal(channels);PreparedCaptureFanout f(c.capture);PreparedMonitorOutputBank o(c.output);
    std::vector<float> actual;actual.reserve(std::size_t(512)*outputChannels);
    {
        PreparedMonitorProcessing p(f,o,c.processing);seed(p,c);publish(f,c,pcm);
        check(!rt([&]{return p.finishAt({c.output.first.value+512});}),"Finished before consuming original monitor route");
        bool end=false,pressure=false;std::vector<float> scratch(std::size_t(64)*outputChannels,999.f);
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
                if(r.frames)actual.insert(actual.end(),scratch.begin(),scratch.begin()+std::ptrdiff_t(std::size_t(r.frames)*outputChannels));
            }
        }
        // Producer can finish with its last accepted block still queued.
        while(!o.drained()) {const auto r=rt([&]{return o.pull(scratch);});check(r.frames>0,"Output cannot drain");actual.insert(actual.end(),scratch.begin(),scratch.begin()+std::ptrdiff_t(std::size_t(r.frames)*outputChannels));}
        check(end && pressure && p.statistics().done && p.statistics().publishedFrames==512 && actual.size()==std::size_t(512)*outputChannels,"Processing end/pressure/extent");
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
    for(unsigned i=0;i<20000 && expected.size()<std::size_t(512)*channels;++i) {
        check(reference.service()!=MonitorBufferCode::Fault,"Reference postroll");
        const auto r=reference.pull(scratch);expected.insert(expected.end(),scratch.begin(),scratch.begin()+std::ptrdiff_t(std::size_t(r.frames)*channels));
    }
    check(expected.size()==std::size_t(512)*channels,"Reference output extent");
    if(mapping) {
        std::vector<float> mapped; mapped.reserve(actual.size());
        for(unsigned n=0;n<512;++n) {
            if(mapping==1){mapped.push_back(expected[n]);mapped.push_back(expected[n]);}
            if(mapping==2)mapped.push_back(float((double(expected[n*2])+expected[n*2+1])*.5));
            if(mapping==3){mapped.push_back(expected[n]);mapped.push_back(0);mapped.push_back(expected[n]*-.5f);}
        }
        expected=std::move(mapped);
    }
    check(expected.size()==actual.size(),"Mapped reference output extent");
    for(std::size_t n=0;n<actual.size();++n)check(std::bit_cast<std::uint32_t>(actual[n])==std::bit_cast<std::uint32_t>(expected[n]),"Partitioned composed PCM differs");
    std::cout<<"processing inputChannels="<<channels<<" outputChannels="<<outputChannels<<" rawFrames=1024 outputFrames=512 comparedOutputBits="<<actual.size()<<" retained-pressure exact retry\n";
}
void channelMaps() {
    ResourceLedger ledger(65536);
    {
        PreparedMonitorChannelMap map(monoToStereoMonitor(),4,65536,ledger);
        check(ledger.usage().reservedBytes==map.chargedBytes(),"Standalone matrix credit");
        std::array<float,10> pcm{2.5f,-3.f,.25f,4.f,99,99,99,99,99,99};
        // Output overlaps the entire input: successful publication waits for all sums.
        check(rt([&]{return map.process({pcm.data(),4},{pcm.data()+1,8},4);})==MonitorChannelMapCode::Mapped,"Aliased matrix refused");
        check(pcm==std::array<float,10>{2.5f,2.5f,2.5f,-3,-3,.25f,.25f,4,4,99},"Mono unity output/headroom/canary");
        std::array<float,4> in{1,2,3,4};std::array<float,8> out;out.fill(99);
        check(rt([&]{return map.process(in,out,0);})==MonitorChannelMapCode::InvalidGeometry,"Zero-frame map accepted");
        check(rt([&]{return map.process(in,out,5);})==MonitorChannelMapCode::InvalidGeometry,"Oversized map accepted");
        check(rt([&]{return map.process(in,{out.data(),7},4);})==MonitorChannelMapCode::InvalidGeometry,"Partial map output accepted");
        in.back()=std::numeric_limits<float>::infinity();
        check(rt([&]{return map.process(in,out,4);})==MonitorChannelMapCode::NonfiniteInput,"Nonfinite map input accepted");
        check(std::all_of(out.begin(),out.end(),[](auto x){return x==99;}),"Refused matrix wrote a prefix");
    }
    check(!ledger.usage().owners && !ledger.usage().reservedBytes,"Standalone matrix credit leaked");
    {
        MonitorChannelMap plan{3,4,{{0,2,2},{1,0,-1},{2,0,.5}}};
        PreparedMonitorChannelMap map(plan,2);
        const std::array<float,6> in{2.5f,4,2,-3,6,8};std::array<float,8> out{};
        check(rt([&]{return map.process(in,out,2);})==MonitorChannelMapCode::Mapped,"Sparse matrix refused");
        check(out==std::array<float,8>{-3,0,5,0,-2,0,-6,0},"Independent sparse matrix oracle");
        // Overflow on the LAST sample cannot publish even the valid first frame.
        PreparedMonitorChannelMap overflow({1,1,{{0,0,64}}},2);
        const std::array<float,2> huge{1,std::numeric_limits<float>::max()};std::array<float,2> unchanged{91,92};
        check(rt([&]{return overflow.process(huge,unchanged,2);})==MonitorChannelMapCode::NumericOverflow &&
            unchanged==std::array<float,2>{91,92},"Numeric overflow clipped or partially published");
    }
    {
        MonitorChannelMap plan{256,256,{}};
        for(unsigned ch=0;ch<256;++ch)plan.routes.push_back({ch,255-ch,1});
        PreparedMonitorChannelMap map(plan,1);
        std::array<float,256> in{},out{};
        for(unsigned ch=0;ch<256;++ch)in[ch]=float(ch)/8;
        check(rt([&]{return map.process(in,out,1);})==MonitorChannelMapCode::Mapped,"256-slot permutation refused");
        for(unsigned ch=0;ch<256;++ch)check(out[ch]==float(255-ch)/8,"256-slot independent permutation oracle");
        plan.routes.clear();
        for(unsigned dest=0;dest<256;++dest)for(unsigned src=0;src<256;++src)
            plan.routes.push_back({src,dest,1.0/256});
        PreparedMonitorChannelMap dense(plan,1);
        in.fill(2.5f);
        check(rt([&]{return dense.process(in,out,1);})==MonitorChannelMapCode::Mapped,"Maximum route matrix refused");
        check(std::all_of(out.begin(),out.end(),[](auto x){return x==2.5f;}),"Maximum route sum/headroom");
        plan.routes.push_back({0,0,1});
        refuses([&]{PreparedMonitorChannelMap excessive(plan,1);});
    }
    for(unsigned kind=0;kind<10;++kind) {
        auto plan=monoToStereoMonitor();unsigned frames=64;std::size_t budget=65536;
        if(kind==0)plan.inputChannels=0;
        if(kind==1)plan.outputChannels=257;
        if(kind==2)plan.routes.clear();
        if(kind==3)plan.routes.push_back(plan.routes.front());
        if(kind==4)plan.routes[0].source=1;
        if(kind==5)plan.routes[0].destination=2;
        if(kind==6)plan.routes[0].gain=std::numeric_limits<double>::quiet_NaN();
        if(kind==7)plan.routes[0].gain=65;
        if(kind==8)frames=UINT32_MAX;
        if(kind==9)budget=1;
        refuses([&]{PreparedMonitorChannelMap map(plan,frames,budget,ledger);});
        check(!ledger.usage().owners && !ledger.usage().reservedBytes,"Refused matrix leaked credit");
    }
    // Existing native state1.1 has no matrix declaration. Refuse startup mapping
    // before claiming a handoff or accepting any raw/native frames.
    Config c(1);c.output.channels=2;c.processing.channelMap=monoToStereoMonitor();
    c.processing.startup=MonitorStartupConfig{64,64,50000000};
    PreparedCaptureFanout capture(c.capture);PreparedMonitorOutputBank output(c.output);
    refuses([&]{PreparedMonitorProcessing p(capture,output,c.processing);});
    check(!capture.rawAcceptedFrames() && !output.acceptedFrames(),"Unversioned map changed capture/output");
}
void mappedFaults() {
    for(unsigned kind=0;kind<4;++kind) {
        Config c(1);c.output.channels=2;c.processing.channelMap=monoToStereoMonitor();
        if(kind==0)c.processing.channelMap->inputChannels=2;
        if(kind==1)c.processing.channelMap->outputChannels=1;
        if(kind==2)c.processing.channelMap->routes[0].gain=65;
        ResourceLedger ledger(kind==3?1:256*1024*1024);c.processing.resources=ledger;
        PreparedCaptureFanout f(c.capture);PreparedMonitorOutputBank o(c.output);
        refuses([&]{PreparedMonitorProcessing p(f,o,c.processing);});
        check(!ledger.usage().owners && !ledger.usage().reservedBytes && !f.rawAcceptedFrames() &&
              !o.acceptedFrames(),"Failed composed matrix admission changed ownership/PCM");
    }
    Config c(1);c.output.channels=2;c.processing.channelMap=MonitorChannelMap{1,2,{{0,0,64},{0,1,64}}};
    PreparedCaptureFanout f(c.capture);PreparedMonitorOutputBank o(c.output);
    PreparedMonitorProcessing p(f,o,c.processing);seed(p,c);
    std::vector<float> pcm(1024,std::numeric_limits<float>::max()/8);publish(f,c,pcm);
    bool failed=false,ended=false;std::array<float,128> output{};
    for(unsigned i=0;i<20000;++i) {
        const auto code=rt([&]{return p.step({now});});
        if(code==MonitorProcessingCode::Fault){failed=true;break;}
        if(code==MonitorProcessingCode::NeedEnd && !ended) {
            ended=rt([&]{return p.finishAt({c.output.first.value+512});});check(ended,"Overflow fixture finite end refused");
        }
        const auto read=rt([&]{return o.pull(output);});
        for(unsigned n=0;n<read.frames;++n)
            check(std::isfinite(output[n*2]) && output[n*2]==output[n*2+1],"Overflow published nonfinite/partial channel data");
    }
    check(failed && p.firstFault()->cause==MonitorProcessingCause::ChannelMap &&
          o.acceptedFrames()==p.statistics().publishedFrames &&
          p.statistics().buffer.deliveredOutputFrames==o.acceptedFrames()+PreparedMonitorBuffer::gridFrames,
          "Composed numeric overflow failed to stop monitor-only publication");
    const auto first=p.firstFault();rt([&]{p.step({now});});check(first==p.firstFault(),"Matrix fault changed on retry");
    drainRaw(f,pcm,1);p.settleAfterJoin();f.settleAfterJoin();o.settleAfterJoin();
    check(f.rawAcceptedFrames()==1024,"Matrix failure shortened raw recording");
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
void finitePlanning() {
    // Exact partial/zero/short ends, dynamic reservation, refusal, real EOF and
    // capacity/postroll/clock/cancel failures. Raw routing is always independent.
    for(unsigned kind=0;kind<8;++kind) {
        Config c;c.capture.capture.slots=64;c.capture.monitorSlots=64;
        PreparedCaptureFanout f(c.capture);PreparedMonitorOutputBank o(c.output);PreparedMonitorProcessing p(f,o,c.processing);
        const unsigned frames=kind==4?4096:1024;
        const auto pcm=signal(3,frames);seed(p,c);
        const auto length=kind==0?512u:kind==1?7u:kind==2 || kind==4 || kind==5?0u:kind==3?4096u:512u;
        const auto end=c.output.first.value+length;
        if(kind!=6) {
            check(rt([&]{return p.planEnd({end});}),"Early finite end refused");
            const auto reserved=p.statistics();
            check(rt([&]{return p.planEnd({end});}) && reserved.buffer==p.statistics().buffer,"Same end retry mutated buffer");
            check(!rt([&]{return p.planEnd({end+1});}) && reserved.buffer==p.statistics().buffer,"Changed fixed end accepted");
        }
        publish(f,c,pcm,frames,kind!=6 && kind!=5);
        if(kind==5) {
            for(unsigned n=0;n<32 && p.statistics().buffer.acceptedInputFrames<1024;++n)
                check(rt([&]{return p.step({now});})==MonitorProcessingCode::Progress,"Planned zero horizon failed actual input collection");
            const auto paused=p.statistics();
            check(rt([&]{return p.step({now});})==MonitorProcessingCode::NeedInput && p.statistics().buffer==paused.buffer,"Open planned horizon progressed without input");
            rt([&]{p.cancel();f.finish();});
        }
        std::vector<float> actual;actual.reserve(std::size_t(length)*3);std::array<float,192> scratch{};
        bool fault=false,planned=kind!=6;std::uint64_t peakGenerated=0;
        for(unsigned i=0;i<20000;++i) {
            if(kind==6 && !planned && p.statistics().buffer.generatedOutputFrames>=64) {
                const auto before=p.statistics();
                check(!rt([&]{return p.planEnd({c.output.first.value+63});}) && p.statistics().buffer==before.buffer,"Late end refusal changed progress");
                check(rt([&]{return p.planEnd({end});}),"Dynamic future end refused");planned=true;rt([&]{f.finish();});
            }
            const auto code=rt([&]{return p.step({kind==7?now+c.processing.drift.maximumAge100ns+1:now});});
            peakGenerated=std::max(peakGenerated,p.statistics().buffer.generatedOutputFrames);
            if(planned && kind!=3)check(peakGenerated<=length,"Finite horizon overshot");
            const auto r=rt([&]{return o.pull(scratch);});
            actual.insert(actual.end(),scratch.begin(),scratch.begin()+std::ptrdiff_t(r.frames*3));
            if(code==MonitorProcessingCode::Done || code==MonitorProcessingCode::Cancelled)break;
            if(code==MonitorProcessingCode::Fault){fault=true;break;}
            check(code!=MonitorProcessingCode::NeedEnd,"Planned EOF needed a late caller end");
        }
        if(kind==3)check(fault && p.bufferFault()->code==MonitorBufferFaultCode::InsufficientInput,"Planned end fabricated future postroll");
        else if(kind==4)check(fault && p.firstFault()->cause==MonitorProcessingCause::PostrollCapacity && p.bufferFault()->code==MonitorBufferFaultCode::InputOverrun,"Planned postroll capacity did not fault");
        else if(kind==5)check(p.statistics().cancelled && !p.statistics().buffer.generatedOutputFrames,"Planned cancellation generated PCM");
        else if(kind==7)check(fault && p.firstFault()->cause==MonitorProcessingCause::Clock,"Planned end bypassed clock age");
        else {
            while(!o.drained()){const auto r=rt([&]{return o.pull(scratch);});check(r.frames>0,"Planned output drain");actual.insert(actual.end(),scratch.begin(),scratch.begin()+std::ptrdiff_t(r.frames*3));}
            check(p.statistics().done && actual.size()==std::size_t(length)*3 && p.statistics().buffer.acceptedInputFrames==1024,"Planned exact end/raw postroll extent");
            PreparedMonitorBuffer reference(c.processing.buffer);
            for(unsigned n=0;n<1024;n+=64)check(reference.push(std::span<const float>(pcm).subspan(n*3,192))==MonitorBufferCode::Accepted,"Planned reference input");
            check(reference.finishAt(end)==MonitorBufferCode::Accepted,"Planned reference end");
            std::size_t compared=0;
            for(unsigned n=0;n<20000 && compared<actual.size();++n){check(reference.service()!=MonitorBufferCode::Fault,"Planned reference service");const auto r=reference.pull(scratch);
                for(unsigned j=0;j<r.frames*3;++j){check(compared<actual.size() && std::bit_cast<std::uint32_t>(scratch[j])==std::bit_cast<std::uint32_t>(actual[compared]),"Planned output differs from direct buffer");++compared;}}
            check(compared==actual.size(),"Planned reference extent");
        }
        drainRaw(f,pcm,3);p.settleAfterJoin();f.settleAfterJoin();o.settleAfterJoin();
        check(!rt([&]{return p.planEnd({end});}),"Terminal planned end accepted");
    }
}
void originalClockIntegration() {
    for(unsigned kind=0;kind<6;++kind) {
        Config c;MonitorClockHandoffConfig qconfig;
        qconfig.captureIdentity=c.processing.drift.captureIdentity;qconfig.renderIdentity=c.processing.drift.renderIdentity;
        qconfig.runGeneration=c.processing.drift.runGeneration;qconfig.renderFrequency=c.processing.drift.renderFrequency;
        if(kind==1)++qconfig.runGeneration;
        if(kind==2)qconfig.slots=1;
        PreparedMonitorClockHandoff clocks(qconfig);PreparedCaptureFanout f(c.capture);PreparedMonitorOutputBank o(c.output);
        if(kind==1) {
            refuses([&]{PreparedMonitorProcessing p(f,o,c.processing,&clocks);});
            clocks.finishCapture();clocks.finishRender();clocks.settleAfterJoin();continue;
        }
        PreparedMonitorProcessing p(f,o,c.processing,&clocks);const auto pcm=signal(3);publish(f,c,pcm);
        for(unsigned n=0;n<6;++n) {
            const auto pos=(std::uint64_t(1)<<63)+std::uint64_t(n)*48000;
            const auto q=now-50000000+std::uint64_t(n)*10000000;
            const auto a=CaptureClockReading{qconfig.captureIdentity,{pos},{q}};
            const auto b=RenderClockReading{qconfig.renderIdentity,{pos},{q},48000};
            const auto ca=rt([&]{return clocks.publish(a);});const auto cb=rt([&]{return clocks.publish(b);});
            if(kind==2 && n==1){check(ca==MonitorClockCode::Fault,"Overflow clock source hidden");break;}
            check(ca==MonitorClockCode::Accepted && cb==MonitorClockCode::Accepted,"Clock integration publication");
        }
        if(kind==2) {
            const auto pumped=rt([&]{return p.pumpClocks();});
            check(pumped.code==MonitorProcessingCode::Fault && !pumped.captureReadings && !pumped.renderReadings &&
                  !p.statistics().buffer.generatedOutputFrames,"Diagnostic clock prefix fitted as healthy");
        } else if(kind==3) {
            rt([&]{clocks.requestCancel();});
            check(rt([&]{return p.step({now});})==MonitorProcessingCode::Cancelled,"Clock cancellation not propagated");
        } else {
            const auto a=rt([&]{return p.pumpClocks();});const auto b=rt([&]{return p.pumpClocks();});
            check(a.captureReadings==4 && a.renderReadings==4 && b.captureReadings==2 && b.renderReadings==2,"Clock pump work bound/original prefix");
            if(kind==4) {
                rt([&]{clocks.finishCapture();clocks.finishRender();clocks.consumerFailed();});
                check(rt([&]{return p.step({now});})==MonitorProcessingCode::Fault,"Late clock route loss bypassed step gate");
            } else if(kind==5) {
                const auto q=now+c.processing.drift.maximumAge100ns+1;
                rt([&]{clocks.publish(CaptureClockReading{qconfig.captureIdentity,{UINT64_MAX},{q},ClockReliability::Missing});
                        clocks.publish(RenderClockReading{qconfig.renderIdentity,{UINT64_MAX},{q},48000,ClockReliability::Delayed});});
                check(rt([&]{return p.pumpClocks();}).code==MonitorProcessingCode::Progress,"Unreliable originals not delivered");
                check(rt([&]{return p.step({q});})==MonitorProcessingCode::Fault && p.firstFault()->cause==MonitorProcessingCause::Clock,"Unreliable readings refreshed accurate clock age");
            } else {
                bool closed=false;std::array<float,192> out{};
                for(unsigned i=0;i<20000 && !p.statistics().done;++i) {
                    const auto code=rt([&]{return p.step({now});});
                    check(code!=MonitorProcessingCode::Fault,"Pumped clocks failed healthy processing");
                    if(code==MonitorProcessingCode::NeedEnd && !closed){closed=rt([&]{return p.finishAt({c.output.first.value+512});});check(closed,"Pumped clock postroll refused");}
                    rt([&]{return o.pull(out);});
                }
                check(p.statistics().done && p.statistics().publishedFrames==512,"Pumped clock processing extent");
            }
        }
        drainRaw(f,pcm,3);p.settleAfterJoin();f.settleAfterJoin();o.settleAfterJoin();
        rt([&]{clocks.finishCapture();clocks.finishRender();});clocks.settleAfterJoin();
        check(f.rawAcceptedFrames()==1024,"Clock handoff failure changed original raw prefix");
    }
}
} // namespace
int main() {
    try {
        rt_audit::reset();channelMaps();mappedFaults();parityAndPressure(1,1);parityAndPressure(2,2);parityAndPressure(1,3);parityAndPressure(3);parityAndPressure(256);faultsAndAdmission();clockStartupAndPostroll();concurrentOwners();originalClockIntegration();finitePlanning();
        check(!rt_audit::counts.cppAllocate && !rt_audit::counts.cppFree && !rt_audit::counts.cAllocate &&
              !rt_audit::counts.cFree && !rt_audit::counts.blockingLock,"Prepared processing RT audit");
        std::cout<<checks<<" bounded monitor processing checks passed; guarded allocate/free/lock=0; synthetic clock observations only\n";return 0;
    }catch(const std::exception &e){rt_audit::active=false;std::cerr<<"Check "<<checks<<": "<<e.what()<<'\n';return 1;}
}
