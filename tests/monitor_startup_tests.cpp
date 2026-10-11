// SPDX-License-Identifier: GPL-3.0-only
#include <soundcurrent/monitor_processing.hpp>
#include "rt_audit.hpp"
#include <array>
#include <bit>
#include <cmath>
#include <iostream>
#include <limits>
#include <thread>
#include <chrono>

using namespace soundcurrent::daw;
namespace {
unsigned checks = 0;
void check(bool ok,const char *why) { ++checks; if (!ok) throw std::runtime_error(why); }
template<class F> auto rt(F &&f) { rt_audit::Guard guard; return f(); }
constexpr std::uint64_t origin = std::uint64_t(1)<<63, qpc = std::uint64_t(1)<<54;
struct Config {
    CaptureFanoutConfig capture{};
    MonitorOutputBankConfig output{};
    MonitorProcessingConfig processing{};
    Config(unsigned channels=1,unsigned sourceRate=48000,unsigned fifo=4097) {
        capture.capture.identity={11,31,7}; capture.capture.channels=channels;
        capture.capture.sampleRate=sourceRate; capture.capture.maximumPacketFrames=sourceRate/100;
        capture.capture.slots=8; capture.monitorSlots=8;
        output.channels=channels; output.slots=128; output.maximumPullFrames=fifo;
        output.first.value=std::uint64_t(1)<<54;
        auto &b=processing.buffer; b.channels=channels; b.sourceRate=sourceRate;
        b.maximumInputFrames=capture.capture.maximumPacketFrames; b.inputCapacityFrames=16384;
        b.outputFrameBase=output.first.value;
        auto &d=processing.drift; d.captureIdentity=capture.capture.identity; d.sourceRate=sourceRate;
        d.inputCapacityFrames=b.inputCapacityFrames; d.outputFrameBase=b.outputFrameBase;
        d.occupancyGainPpmPerFrame=0;
        processing.startup=MonitorStartupConfig{fifo,fifo,50000000};
    }
};
MonitorStartupAdmission readyAdmission(const Config &c) {
    const auto &b=c.processing.buffer;const auto &d=c.processing.drift;const auto &s=*c.processing.startup;
    return {b.runGeneration,d.captureIdentity,d.renderIdentity,b.sourceRate,b.outputRate,b.channels,
        d.renderFrequency,s.nativeCapacityFrames,s.maximumNativeFillFrames,s.maximumWait100ns};
}
float sample(std::uint64_t frame,unsigned ch) {
    return float(2.5*std::sin(double(frame)*.019+ch*.1));
}
void observe(PreparedMonitorProcessing &p,const Config &c,unsigned tick) {
    const auto now=qpc+std::uint64_t(tick)*100000;
    rt([&]{ p.observe(CaptureClockReading{c.processing.drift.captureIdentity,
        {origin+std::uint64_t(tick)*c.capture.capture.maximumPacketFrames},{now}});
        p.observe(RenderClockReading{c.processing.drift.renderIdentity,
        {origin+std::uint64_t(tick)*480},{now},48000}); });
}
void publishRaw(PreparedCaptureFanout &f,const Config &c,unsigned tick,std::vector<float> &pcm,bool nonfinite=false) {
    const auto frames=c.capture.capture.maximumPacketFrames;
    const auto at=std::uint64_t(tick)*frames;
    for(unsigned n=0;n<frames;++n)for(unsigned ch=0;ch<c.output.channels;++ch)
        pcm[std::size_t(n)*c.output.channels+ch]=sample(at+n,ch);
    if (nonfinite) pcm.back()=std::numeric_limits<float>::quiet_NaN();
    const CaptureBankStamp stamp{c.capture.capture.identity,{origin+at},{qpc+std::uint64_t(tick)*100000},frames,0};
    const auto result=rt([&]{return f.publish({stamp,reinterpret_cast<const std::byte*>(pcm.data()),pcm.size()*sizeof(float)});});
    check(result.rawAcceptedFrames==frames && result.monitorAcceptedFrames==frames,"Actual packet not preserved on both routes");
    CaptureBankView v;check(rt([&]{return f.acquireRaw(v);})==CaptureBankRead::Ready,"Raw packet missing during preroll");
    check(v.token.original==stamp,"Raw original stamp changed during preroll");
    bool same=v.interleaved.size()==pcm.size();
    for(std::size_t n=0;n<pcm.size();++n)same=same && std::bit_cast<std::uint32_t>(v.interleaved[n])==std::bit_cast<std::uint32_t>(pcm[n]);
    check(same,"Rolling monitor startup changed raw PCM bits");
    check(rt([&]{return f.releaseRaw(v);}),"Raw release failed");
}
void success(unsigned channels,unsigned rate) {
    Config c(channels,rate);ResourceLedger ledger(256*1024*1024);c.processing.resources=ledger;
    PreparedCaptureFanout f(c.capture);PreparedMonitorOutputBank o(c.output);
    std::vector<float> pcm(std::size_t(c.capture.capture.maximumPacketFrames)*channels);
    {
        PreparedMonitorStartupHandoff handoff(readyAdmission(c),65536,ledger);
        PreparedMonitorProcessing p(f,o,c.processing,nullptr,nullptr,nullptr,&handoff);
        std::optional<MonitorStartupReady> observedReady;
        bool readerFailed=false;std::uint64_t readerPolls=0;rt_audit::Counts readerAudit;
        std::jthread reader([&](std::stop_token stop) {
            rt_audit::reset();const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(30);
            while(!stop.stop_requested() && std::chrono::steady_clock::now()<deadline) {
                const auto state=rt([&]{return handoff.phase();});
                auto original=rt([&]{return handoff.ready();});++readerPolls;
                if(original) {
                    if(original->admitted!=readyAdmission(c) || !original->original.contentReady ||
                       original->publishedFrames<c.processing.startup->nativeCapacityFrames)
                        readerFailed=true;
                    observedReady=original;break;
                }
                if(state!=MonitorStartupHandoffPhase::Pending && state!=MonitorStartupHandoffPhase::Ready) {
                    readerFailed=true;break;
                }
                std::this_thread::yield();
            }
            readerAudit=rt_audit::counts;
        }); // Joins/stops BEFORE processing teardown on every exceptional path.
        unsigned tick=0;
        for(;tick<250;++tick) {
            observe(p,c,tick);publishRaw(f,c,tick,pcm);
            for(unsigned n=0;n<2;++n) {
                const auto code=rt([&]{return p.step({qpc+std::uint64_t(tick)*100000});});
                check(code!=MonitorProcessingCode::Fault,"Unseeded startup failed");
                if(p.statistics().startup.phase==MonitorStartupPhase::Transferring)break;
            }
            const auto s=p.statistics();
            if(tick<200)check(!s.serviceCalls && !s.publishedFrames && !s.startup.contentReady &&
                !s.buffer.acceptedInputFrames,"PCM advanced before original clock span");
            if(s.startup.phase==MonitorStartupPhase::Transferring)break;
        }
        check(tick>=200 && tick<250,"Clock span did not control startup");
        const auto initial=p.statistics().startup;
        check(initial.retainedFrames==initial.capacityFrames && initial.discardedFrames>0 &&
            initial.acceptedFrames==initial.discardedFrames+initial.retainedFrames,"Rolling tail disposition inconsistent");
        check(f.rawAcceptedFrames()==std::uint64_t(tick+1)*c.capture.capture.maximumPacketFrames,
            "Raw startup prefix was shortened");
        const auto now=qpc+std::uint64_t(tick)*100000;
        for(unsigned n=0;n<20000 && !p.statistics().startup.contentReady;++n) {
            const auto code=rt([&]{return p.step({now});});
            check(code!=MonitorProcessingCode::Fault && code!=MonitorProcessingCode::OutputFull,
                "Admitted tail could not prime full native FIFO");
        }
        const auto ready=p.statistics();
        check(ready.startup.contentReady && ready.startup.phase==MonitorStartupPhase::Live &&
            ready.startup.transferredFrames==initial.capacityFrames && !ready.startup.retainedFrames &&
            o.acceptedFrames()>=c.processing.startup->nativeCapacityFrames && !o.deliveredFrames(),
            "Content admitted without the entire actual native FIFO");
        check(ready.startup.firstTransferred && ready.startup.lastAccepted,"Original startup source evidence missing");
        const auto first=*ready.startup.firstTransferred;
        const auto firstFrame=first.original.position.value-origin+first.packetOffsetFrames;
        check(firstFrame==initial.discardedFrames && first.original.qpc.value>=qpc &&
            first.original.identity==c.capture.capture.identity && first.packetOffsetFrames<first.original.frames,
            "First transferred source stamp/partial-packet offset changed");
        reader.join();
        check(!readerFailed && observedReady && readerPolls &&
            observedReady->original==ready.startup && observedReady->admitted==handoff.admission() &&
            observedReady->publishedFrames==ready.publishedFrames && observedReady->asOfQpc100ns==now,
            "Concurrent native reader missed or mutated full-FIFO original readiness");
        check(!readerAudit.cppAllocate && !readerAudit.cppFree && !readerAudit.cAllocate &&
            !readerAudit.cFree && !readerAudit.blockingLock,"Readiness reader allocated/freed/locked");
        check(ledger.usage().reservedBytes==p.chargedBytes()+handoff.chargedBytes(),"Startup credit was not charged once");
        const auto immutable=handoff.ready();
        check(immutable && !rt([&]{return handoff.publish(*immutable);}),"Readiness was published/reused twice");
        // Independent causal converter with the SAME retained actual source, no
        // wrapper or seeded clocks. This checks trimming/wrap/transfer sample order.
        PreparedMonitorBuffer reference(c.processing.buffer);
        std::vector<float> retained(std::size_t(initial.capacityFrames)*channels);
        for(unsigned n=0;n<initial.capacityFrames;++n)for(unsigned ch=0;ch<channels;++ch)
            retained[std::size_t(n)*channels+ch]=sample(firstFrame+n,ch);
        for(unsigned n=0;n<initial.capacityFrames;n+=c.processing.buffer.maximumInputFrames) {
            const auto count=std::min(c.processing.buffer.maximumInputFrames,initial.capacityFrames-n);
            check(reference.push(std::span<const float>(retained).subspan(std::size_t(n)*channels,std::size_t(count)*channels))==MonitorBufferCode::Accepted,"Independent retained source rejected");
        }
        std::vector<float> expected;expected.reserve(std::size_t(ready.publishedFrames)*channels);
        std::vector<float> block(64*channels);
        for(unsigned n=0;n<20000 && expected.size()<std::size_t(ready.publishedFrames)*channels;++n) {
            check(reference.service()!=MonitorBufferCode::Fault,"Independent converter failed");
            const auto r=reference.pull(block);
            if(r.frames)expected.insert(expected.end(),block.begin(),block.begin()+std::ptrdiff_t(std::size_t(r.frames)*channels));
        }
        std::vector<float> actual(std::size_t(c.processing.startup->nativeCapacityFrames)*channels);
        const auto pulled=rt([&]{return o.pull(actual);});
        check(pulled.frames==c.processing.startup->nativeCapacityFrames && pulled.first==c.output.first,
            "Full native FIFO content pull incomplete");
        check(expected.size()>=actual.size(),"Independent converter extent missing");
        double error=0;for(std::size_t n=0;n<actual.size();++n)error=std::max(error,std::abs(double(actual[n])-expected[n]));
        check(error<1e-5,"Startup tail sample order/wrap differs from independent converter");
        rt([&]{p.cancel();});p.settleAfterJoin();f.settleAfterJoin();o.settleAfterJoin();
        check(handoff.phase()==MonitorStartupHandoffPhase::Cancelled && handoff.ready()==immutable,
            "Cancel erased or modified the immutable ready source evidence");
        std::cout<<"generation-bound full-FIFO receipt; concurrent reader audit=0; " ;
        std::cout<<"unseeded original observations; channels="<<channels<<" sourceRate="<<rate
            <<" firstReadyTick="<<tick<<" raw="<<f.rawAcceptedFrames()<<" monitorDiscard="<<initial.discardedFrames
            <<" retained="<<initial.capacityFrames<<" fullFifo="<<pulled.frames<<" maxError="<<error<<"\n";
    }
    check(!ledger.usage().reservedBytes && !ledger.usage().owners,"Startup credit leaked on retirement");
}
void handoffFailures() {
    for(unsigned kind=0;kind<4;++kind) {
        Config c;PreparedCaptureFanout f(c.capture);PreparedMonitorOutputBank o(c.output);
        PreparedMonitorStartupHandoff h(readyAdmission(c));
        PreparedMonitorProcessing p(f,o,c.processing,nullptr,nullptr,nullptr,&h);
        if(kind==0) {
            rt([&]{p.step({qpc});p.step({qpc+c.processing.startup->maximumWait100ns});});
            check(h.phase()==MonitorStartupHandoffPhase::Fault && !h.ready(),"Timeout invented full FIFO readiness");
        } else if(kind==1) {
            rt([&]{f.finish();p.step({qpc});});
            check(h.phase()==MonitorStartupHandoffPhase::InputEnd && !h.ready(),"Early capture EOF invented readiness or lost cause");
        } else if(kind==2) {
            rt([&]{h.requestCancel();});
            check(rt([&]{return p.step({qpc});})==MonitorProcessingCode::Cancelled &&
                h.phase()==MonitorStartupHandoffPhase::Cancelled && !h.ready(),"Control cancel did not reach processing startup");
        } else {
            p.settleAfterJoin();
            check(h.phase()==MonitorStartupHandoffPhase::Fault && !h.ready(),"Abandoned processing owner fabricated readiness");
        }
        p.settleAfterJoin();f.settleAfterJoin();o.settleAfterJoin();h.settleAfterProducerJoin();
    }
    Config c;PreparedMonitorStartupHandoff abandoned(readyAdmission(c));
    rt([&]{abandoned.settleAfterProducerJoin();});
    check(abandoned.phase()==MonitorStartupHandoffPhase::OwnerLost && !abandoned.ready(),"Unstarted/lost publisher fabricated an original ready receipt");
    for(unsigned kind=0;kind<6;++kind) {
        auto a=readyAdmission(c);
        if(kind==0)++a.runGeneration;
        if(kind==1)++a.captureIdentity.endpoint;
        if(kind==2)++a.renderIdentity.stream;
        if(kind==3)++a.renderFrequency;
        if(kind==4)++a.nativeCapacityFrames;
        if(kind==5)++a.maximumWait100ns;
        PreparedCaptureFanout f(c.capture);PreparedMonitorOutputBank o(c.output);PreparedMonitorStartupHandoff h(a);
        ResourceLedger ledger(256*1024*1024);c.processing.resources=ledger;bool refused=false;
        try {PreparedMonitorProcessing p(f,o,c.processing,nullptr,nullptr,nullptr,&h);}catch(const ProjectError &){refused=true;}
        check(refused && !ledger.usage().reservedBytes,"Foreign/stale native handoff admission accepted or leaked credit");
    }
    {
        PreparedCaptureFanout f(c.capture);PreparedMonitorOutputBank o(c.output);
        PreparedMonitorStartupHandoff h(readyAdmission(c));
        PreparedMonitorProcessing first(f,o,c.processing,nullptr,nullptr,nullptr,&h);
        const auto before=c.processing.resources->usage().reservedBytes;bool refused=false;
        try {PreparedMonitorProcessing duplicate(f,o,c.processing,nullptr,nullptr,nullptr,&h);}catch(const ProjectError &){refused=true;}
        check(refused && h.claimed() && c.processing.resources->usage().reservedBytes==before,
            "Second processing publisher admitted or changed original ownership credit");
        rt([&]{first.cancel();});first.settleAfterJoin();f.settleAfterJoin();o.settleAfterJoin();
    }
    PreparedMonitorStartupHandoff malformed(readyAdmission(c));
    check(malformed.claimProcessingOwner(),"Malformed receipt fixture did not claim its single owner");
    for(unsigned kind=0;kind<8;++kind) {
        MonitorStartupReady r;r.admitted=readyAdmission(c);r.publishedFrames=c.processing.startup->nativeCapacityFrames;r.asOfQpc100ns=qpc;
        r.original.phase=MonitorStartupPhase::Live;r.original.contentReady=true;r.original.capacityFrames=100;
        r.original.acceptedFrames=200;r.original.discardedFrames=100;r.original.transferredFrames=100;
        const CaptureBankStamp stamp{c.capture.capture.identity,{origin},{qpc},480,0};
        r.original.firstTransferred=MonitorStartupSource{stamp,0};r.original.lastAccepted=stamp;
        if(kind==0)r.publishedFrames--;
        if(kind==1)r.asOfQpc100ns=0;
        if(kind==2)r.original.contentReady=false;
        if(kind==3)r.original.retainedFrames=1;
        if(kind==4)r.original.acceptedFrames++;
        if(kind==5)r.original.firstTransferred->packetOffsetFrames=480;
        if(kind==6)r.original.discardedFrames=UINT64_MAX;
        if(kind==7)r.original.lastAccepted->identity.endpoint++;
        check(!rt([&]{return malformed.publish(r);}) && !malformed.ready() &&
            malformed.phase()==MonitorStartupHandoffPhase::Pending,"Malformed receipt changed pending handoff or became ready");
    }
    ResourceLedger tiny(1);bool refused=false;
    try {PreparedMonitorStartupHandoff h(readyAdmission(c),65536,tiny);}catch(const ProjectError &){refused=true;}
    check(refused && !tiny.usage().owners && !tiny.usage().reservedBytes,"Ready-handoff resource refusal leaked");
}

void failures() {
    for(unsigned kind=0;kind<7;++kind) {
        Config c;PreparedCaptureFanout f(c.capture);PreparedMonitorOutputBank o(c.output);
        PreparedMonitorProcessing p(f,o,c.processing);
        std::vector<float> pcm(c.capture.capture.maximumPacketFrames);
        if(kind==0) {
            check(rt([&]{return p.step({qpc});})==MonitorProcessingCode::WaitingClock,"Clockless startup should wait");
            check(rt([&]{return p.step({qpc+c.processing.startup->maximumWait100ns});})==MonitorProcessingCode::Fault &&
                p.firstFault()->cause==MonitorProcessingCause::StartupTimeout,"Clockless startup did not terminate at admitted bound");
        } else if(kind==1) {
            rt([&]{f.finish();});check(rt([&]{return p.step({qpc});})==MonitorProcessingCode::Fault &&
                p.firstFault()->cause==MonitorProcessingCause::StartupInputEnd,"Early EOF invented readiness");
        } else if(kind==2) {
            observe(p,c,0);publishRaw(f,c,0,pcm,true);
            check(rt([&]{return p.step({qpc});})==MonitorProcessingCode::Fault &&
                p.firstFault()->cause==MonitorProcessingCause::StartupInput,"Nonfinite startup packet accepted");
        } else if(kind==3) {
            observe(p,c,0);publishRaw(f,c,0,pcm);rt([&]{p.step({qpc});p.cancel();});
            check(p.statistics().cancelled && p.statistics().startup.acceptedFrames==pcm.size() &&
                f.rawAcceptedFrames()==pcm.size() && !p.statistics().startup.contentReady,"Cancel lost raw/disposition or invented readiness");
        } else if(kind==4) {
            auto identity=c.processing.drift.captureIdentity;++identity.generation;
            rt([&]{p.observe(CaptureClockReading{identity,{origin},{qpc}});});
            check(p.firstFault() && p.firstFault()->cause==MonitorProcessingCause::Clock,"Foreign original clock was accepted");
        } else {
            unsigned tick=0;
            for(;tick<250 && p.statistics().startup.phase==MonitorStartupPhase::Observing;++tick) {
                observe(p,c,tick);publishRaw(f,c,tick,pcm);
                rt([&]{p.step({qpc+std::uint64_t(tick)*100000});p.step({qpc+std::uint64_t(tick)*100000});});
            }
            const auto now=qpc+std::uint64_t(tick-1)*100000;
            for(unsigned n=0;n<20000 && !o.acceptedFrames();++n)
                check(rt([&]{return p.step({now});})!=MonitorProcessingCode::Fault,"Failure fixture did not reach first block");
            check(o.acceptedFrames()==64 && !p.statistics().startup.contentReady,"Failure fixture accidentally primed full FIFO");
            if(kind==5) {
                std::array<float,64> out{};check(rt([&]{return o.pull(out);}).frames==64,"Premature pull fixture");
                check(rt([&]{return p.step({now});})==MonitorProcessingCode::Fault &&
                    p.firstFault()->cause==MonitorProcessingCause::StartupPrematurePull,"Partial prefill incorrectly authorized content");
            } else check(rt([&]{return p.step({qpc+c.processing.startup->maximumWait100ns});})==MonitorProcessingCode::Fault &&
                p.firstFault()->cause==MonitorProcessingCause::StartupTimeout,"Prefill escaped startup deadline");
        }
        p.settleAfterJoin();f.settleAfterJoin();o.settleAfterJoin();
    }
    // Strict confidence still rejects moving clocks after their minimum span.
    Config c;c.processing.drift.maximumConfidencePpm=1;
    PreparedCaptureFanout f(c.capture);PreparedMonitorOutputBank o(c.output);PreparedMonitorProcessing p(f,o,c.processing);
    std::vector<float> pcm(c.capture.capture.maximumPacketFrames);
    for(unsigned tick=0;tick<250;++tick) {
        observe(p,c,tick);publishRaw(f,c,tick,pcm);
        rt([&]{p.step({qpc+std::uint64_t(tick)*100000});p.step({qpc+std::uint64_t(tick)*100000});});
        check(!p.firstFault() && !p.statistics().startup.contentReady && !p.statistics().serviceCalls,
            "Startup bypassed confidence rejection");
    }
    check(p.statistics().startup.discardedFrames>0 && f.rawAcceptedFrames()==120000,"Confidence wait overflowed or shortened raw prefix");
    rt([&]{p.cancel();});p.settleAfterJoin();f.settleAfterJoin();o.settleAfterJoin();
}
void admission() {
    for(unsigned kind=0;kind<6;++kind) {
        Config c;
        if(kind==0)c.output.slots=32;
        if(kind==1)c.output.maximumPullFrames=64;
        if(kind==2)c.processing.startup->maximumNativeFillFrames=4098;
        if(kind==3)c.processing.startup->maximumWait100ns=c.processing.drift.minimumSpan100ns;
        if(kind==4)c.processing.buffer.inputCapacityFrames=c.processing.drift.inputCapacityFrames=4096;
        if(kind==5)c.processing.memoryBudgetBytes=100;
        ResourceLedger ledger(256*1024*1024);c.processing.resources=ledger;
        PreparedCaptureFanout f(c.capture);PreparedMonitorOutputBank o(c.output);
        bool refused=false;try {PreparedMonitorProcessing p(f,o,c.processing);}catch(const ProjectError&){refused=true;}
        check(refused && !ledger.usage().reservedBytes && !ledger.usage().owners,"Invalid native startup geometry/budget admitted or leaked credit");
        f.settleAfterJoin();o.settleAfterJoin();
    }
}
void oneFramePackets() {
    Config c(1,8000,128);
    c.capture.capture.maximumPacketFrames=c.processing.buffer.maximumInputFrames=1;
    c.output.slots=8;c.processing.buffer.inputCapacityFrames=c.processing.drift.inputCapacityFrames=2048;
    c.processing.drift.targetQueuedSourceFrames=32;
    c.processing.drift.minimumSpan100ns=1000000;c.processing.drift.timestampUncertainty100ns=1;
    PreparedCaptureFanout f(c.capture);PreparedMonitorOutputBank o(c.output);PreparedMonitorProcessing p(f,o,c.processing);
    for(unsigned n=0;n<=800;++n) {
        const auto now=qpc+std::uint64_t(n)*1250;
        rt([&]{p.observe(CaptureClockReading{c.processing.drift.captureIdentity,{origin+n},{now}});
            p.observe(RenderClockReading{c.processing.drift.renderIdentity,{origin+6*n},{now},48000});});
        const float pcm=sample(n,0);
        const CaptureBankStamp stamp{c.capture.capture.identity,{origin+n},{now},1,0};
        check(rt([&]{return f.publish({stamp,reinterpret_cast<const std::byte*>(&pcm),sizeof(pcm)});}).rawAcceptedFrames==1,"One-frame publication");
        CaptureBankView v;check(rt([&]{return f.acquireRaw(v);})==CaptureBankRead::Ready &&
            v.token.original==stamp && v.interleaved[0]==pcm,"One-frame raw prefix changed");
        check(rt([&]{return f.releaseRaw(v);}),"One-frame raw release");
        check(rt([&]{return p.step({now});})!=MonitorProcessingCode::Fault,"One-frame descriptor trim failed");
        if(p.statistics().startup.phase==MonitorStartupPhase::Transferring)break;
        check(rt([&]{return p.step({now});})!=MonitorProcessingCode::Fault,"One-frame empty poll failed");
    }
    const auto s=p.statistics().startup;
    check(s.phase==MonitorStartupPhase::Transferring && s.discardedFrames>0 &&
        s.retainedFrames==s.capacityFrames && s.acceptedFrames==800,"One-frame descriptors escaped admission bound");
    for(unsigned n=0;n<20000 && !p.statistics().startup.contentReady;++n)
        check(rt([&]{return p.step({qpc+1000000});})!=MonitorProcessingCode::Fault,"One-frame transfer failed");
    const auto ready=p.statistics().startup;
    check(ready.contentReady && ready.firstTransferred && ready.firstTransferred->packetOffsetFrames==0 &&
        ready.firstTransferred->original.position.value==origin+s.discardedFrames,"One-frame first original stamp lost");
    rt([&]{p.cancel();});p.settleAfterJoin();f.settleAfterJoin();o.settleAfterJoin();
}
}
int main() {
    try {
        rt_audit::reset();for(unsigned channels:{1u,2u,8u,256u})success(channels,48000);
        success(2,44100);success(2,96000);failures();admission();oneFramePackets();handoffFailures();
        const auto a=rt_audit::counts;
        check(!a.cppAllocate && !a.cppFree && !a.cAllocate && !a.cFree && !a.blockingLock,"Prepared startup allocated/freed/locked");
        std::cout<<checks<<" startup checks; guarded allocate/free/lock=0; no SDK/hardware claim\n";return 0;
    }catch(const std::exception &e){std::cerr<<"FAIL "<<e.what()<<"\n";return 1;}
}
