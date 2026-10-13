// SPDX-License-Identifier: GPL-3.0-only
#include <soundcurrent/pipewire_monitor_render.hpp>
#include "rt_audit.hpp"
#include <bit>
#include <functional>
#include <iostream>
using namespace soundcurrent::daw;
namespace {
unsigned checks=0;
void check(bool ok,const char *why){++checks;if(!ok)throw std::runtime_error(why);}
template<class F> auto rt(F f){rt_audit::Guard guard;return f();}
template<class F> void refuses(F f){try{f();}catch(const ProjectError &){++checks;return;}throw std::runtime_error("Invalid render admission accepted");}
constexpr std::uint64_t firstOrdinal=(std::uint64_t(1)<<54)+37;
MonitorOutputBankConfig bankConfig(unsigned channels=3,unsigned rate=48000,unsigned quantum=17){
    MonitorOutputBankConfig c;c.channels=channels;c.sampleRate=rate;c.maximumBlockFrames=17;
    c.maximumPullFrames=quantum;c.slots=8;c.first={firstOrdinal};return c;
}
MonitorClockHandoffConfig clocksConfig(unsigned rate=48000){
    MonitorClockHandoffConfig c;c.timestampDomain=MonitorClockDomain::PosixMonotonic100ns;c.renderFrequency=rate;return c;
}
DeviceBlockClock clock(unsigned packet=0,unsigned frames=17,unsigned rate=48000){
    return {(std::uint64_t(1)<<54)+73+std::uint64_t(packet)*frames,frames,
        (std::uint64_t(1)<<63)+987+std::uint64_t(packet)*frames*1000000000/rate,19,packet+4,1,rate,-37,false,false};
}
struct Planes {
    std::vector<float> data;std::vector<float *> views;unsigned capacity;
    Planes(unsigned channels=3,unsigned frames=17):data(std::size_t(channels)*(frames+2),-999),views(channels),capacity(frames){
        for(unsigned c=0;c<channels;++c)views[c]=data.data()+std::size_t(c)*(frames+2)+1;
    }
    void guards(){for(auto *p:views)check(p[-1]==-999 && p[capacity]==-999,"Native plane guard overwritten");}
};
std::vector<float> samples(unsigned frames,unsigned channels,unsigned offset=0){
    std::vector<float> v(std::size_t(frames)*channels);
    for(unsigned f=0;f<frames;++f)for(unsigned c=0;c<channels;++c)v[std::size_t(f)*channels+c]=float(offset+f)/8+float(c)*4-7;
    if(!offset && frames)v[0]=std::bit_cast<float>(0x80000000U);
    return v;
}
void push(PreparedMonitorOutputBank &b,unsigned frames,unsigned offset=0){
    auto v=samples(frames,b.config().channels,offset);check(b.publish({1,{firstOrdinal+offset},frames},v)==MonitorOutputCode::Accepted,"Output publication failed");
}
void complete(PreparedPipeWireMonitorRender &r,const DeviceBlockClock &c,unsigned channels,std::int32_t result=0){
    std::vector<PipeWirePortLeaseResult> returns(channels,{PipeWireBufferStatus::Ready,true,result});
    rt([&]{r.completeCycle({c,true,{},returns});});
}
void drainClock(PreparedMonitorClockHandoff &h,const DeviceBlockClock &c){
    MonitorClockRecord<RenderClockReading> v;check(h.read(v).code==MonitorClockCode::Ready && v.original.position.value==c.position &&
        v.original.qpc.value==c.monotonicNs/100 && v.original.frequency==c.rateDenominator,"Graph clock handoff changed");
}
void exactAndFinal(){
    PreparedMonitorOutputBank b(bankConfig());PreparedMonitorClockHandoff h(clocksConfig());ResourceLedger ledger(512*1024,"Render test");
    {
        PreparedPipeWireMonitorRender r(b,h,{0,5000000000ULL,512*1024,ledger});check(ledger.usage().reservedBytes>0,"Render payload not charged");
        push(b,17);push(b,5,17);b.finish();Planes p;
        for(unsigned n=0;n<2;++n){
            const auto c=clock(n);check(rt([&]{return r.process(c,p.views,17);})==(n?PipeWireRenderCode::Complete:PipeWireRenderCode::Content),"Finite native render wrong state");
            const auto expected=samples(n?5:17,3,n*17);
            for(unsigned f=0;f<17;++f)for(unsigned ch=0;ch<3;++ch)
                check(std::bit_cast<unsigned>(p.views[ch][f])==std::bit_cast<unsigned>(f<(n?5U:17U)?expected[std::size_t(f)*3+ch]:0.F),"Float bits/headroom/final silence changed");
            check(!r.contentComplete(),"Completion reported before actual buffer return");
            complete(r,c,3,n?7:0);drainClock(h,c);p.guards();
        }
        check(r.contentComplete() && !r.endAfterJoin(),"Native complete/join domains confused");
        const auto later=clock(2);check(rt([&]{return r.process(later,p.views,17);})==PipeWireRenderCode::Complete,"Completed content replayed");
        for(auto *plane:p.views)for(unsigned f=0;f<17;++f)check(plane[f]==0,"Stale data after finite end");
        complete(r,later,3);
        r.finishAfterJoin();r.finishAfterJoin();const auto e=r.endAfterJoin();
        check(e && e->contentComplete && e->callbacksJoined && !e->fault && e->monitorConsumedFrames==22 && e->monitorQueuedFrames==22 &&
            e->nativeQueuedFrames==51 && e->silentQueuedFrames==29 && e->lastContent->returns[1].queueResult==7 && e->lastContent->finalContent &&
            e->completion && e->completion->original==clock(1) && e->completion->returns[1].queueResult==7 && !e->last->finalContent,"Native finite receipt counters/results incorrect");
    }
    check(!ledger.usage().reservedBytes,"Render payload did not retire");
}
void warmupAndUnderflow(){
    PreparedMonitorOutputBank b(bankConfig());PreparedMonitorClockHandoff h(clocksConfig());PreparedPipeWireMonitorRender r(b,h);Planes p;
    const auto first=clock();check(rt([&]{return r.process(first,p.views,17);})==PipeWireRenderCode::Warmup,"Empty startup invented content");
    complete(r,first,3);drainClock(h,first);check(!b.deliveredFrames(),"Warmup advanced monitor ordinal");
    push(b,17);check(rt([&]{return r.process(clock(1),p.views,17);})==PipeWireRenderCode::Content,"Ready startup refused content");
    complete(r,clock(1),3);drainClock(h,clock(1));push(b,5,17);
    check(rt([&]{return r.process(clock(2),p.views,17);})==PipeWireRenderCode::Fault && r.firstFault()->cause==PipeWireRenderCause::Underflow,"Live underflow hidden as silence/padding");
    for(auto *plane:p.views)for(unsigned f=0;f<17;++f)check(plane[f]==0,"Underflow exposed partial/stale output");
    complete(r,clock(2),3);r.finishAfterJoin();const auto e=r.endAfterJoin();
    check(e->warmupQueuedFrames==17 && e->monitorConsumedFrames==22 && e->monitorQueuedFrames==17 && !e->contentComplete,"Underflow claimed pulled-but-silent frames queued");
    for(unsigned mode=0;mode<2;++mode){
        PreparedMonitorOutputBank bank(bankConfig());PreparedMonitorClockHandoff handoff(clocksConfig());
        PipeWireMonitorRenderOptions o;o.maximumWarmupFrames=mode?1000:17;o.maximumWarmupNs=mode?100:5000000000ULL;
        PreparedPipeWireMonitorRender render(bank,handoff,o);check(rt([&]{return render.process(clock(),p.views,17);})==PipeWireRenderCode::Warmup,"Warmup first cycle refused");
        complete(render,clock(),3);check(rt([&]{return render.process(clock(1),p.views,17);})==PipeWireRenderCode::Fault && render.firstFault()->cause==PipeWireRenderCause::WarmupLimit,"Warmup budget unbounded");
        complete(render,clock(1),3);render.finishAfterJoin();
    }
}
void finishAfterLastContent(){
    PreparedMonitorOutputBank b(bankConfig());PreparedMonitorClockHandoff h(clocksConfig());
    PreparedPipeWireMonitorRender r(b,h);Planes p;push(b,17);
    const auto audio=clock();check(rt([&]{return r.process(audio,p.views,17);})==PipeWireRenderCode::Content,"Unfinished producer changed content state");
    complete(r,audio,3);drainClock(h,audio);check(!r.contentComplete(),"Content return guessed producer completion");
    b.finish();const auto final=clock(1);
    check(rt([&]{return r.process(final,p.views,17);})==PipeWireRenderCode::Complete,"Silent final receipt refused");
    for(auto *plane:p.views)for(unsigned n=0;n<17;++n)check(plane[n]==0,"Final silent receipt repeated audio");
    complete(r,final,3);drainClock(h,final);
    const auto later=clock(2);check(rt([&]{return r.process(later,p.views,17);})==PipeWireRenderCode::Complete,"Later completed callback refused");
    complete(r,later,3);r.finishAfterJoin();const auto e=r.endAfterJoin();
    check(e && e->contentComplete && !e->fault && e->lastContent && !e->lastContent->finalContent &&
          e->last && !e->last->finalContent && e->completion && e->completion->original==final && e->completion->monitorFrames==0 &&
          e->completion->finalContent && e->completion->allQueued && e->monitorQueuedFrames==17 && e->nativeQueuedFrames==51,
          "Last content and actual completion receipt were conflated");
}
void returnFacts(){
    for(unsigned mode=0;mode<5;++mode){
        PreparedMonitorOutputBank b(bankConfig());PreparedMonitorClockHandoff h(clocksConfig());PreparedPipeWireMonitorRender r(b,h);Planes p;push(b,17);b.finish();
        const auto c=clock();check(rt([&]{return r.process(c,p.views,17);})==PipeWireRenderCode::Complete,"Receipt fixture source failed");
        std::array<PipeWirePortLeaseResult,3> native;native.fill({PipeWireBufferStatus::Ready,true,0});
        if(mode==0)native[1].queueResult=-5;
        if(mode==1)native[1].queueResult.reset();
        if(mode==2)native[1].acquired=false;
        if(mode==3)native[1].status=PipeWireBufferStatus::Invalid;
        if(mode!=4)rt([&]{r.completeCycle({c,true,{},native});});
        r.finishAfterJoin();const auto e=r.endAfterJoin();
        check(e->fault && !e->contentComplete && e->monitorConsumedFrames==17 && e->monitorQueuedFrames==0,"Bad native return invented success");
        if(mode<4)check(e->fault->channel==1 && e->faultCycle && e->faultCycle->returns[1]==native[1] && e->faultCycle->returns[2].queueResult==0,"Original per-channel native results lost");
        else check(e->fault->cause==PipeWireRenderCause::MissingReturn && !e->last,"Absent native return invented receipt");
    }
    PreparedMonitorOutputBank b(bankConfig());PreparedMonitorClockHandoff h(clocksConfig());PreparedPipeWireMonitorRender r(b,h);Planes p;push(b,17);b.finish();
    const auto processed=clock();check(rt([&]{return r.process(processed,p.views,17);})==PipeWireRenderCode::Complete,"Foreign return fixture failed");
    auto actual=processed;++actual.cycle;std::array<PipeWirePortLeaseResult,3> native;native.fill({PipeWireBufferStatus::Ready,true,0});
    rt([&]{r.completeCycle({actual,true,{},native});});r.finishAfterJoin();const auto e=r.endAfterJoin();
    check(e->fault->cause==PipeWireRenderCause::InvalidReturn && e->last->original==processed && e->last->returnClock==actual &&
        !e->last->allQueued && e->monitorQueuedFrames==0,"Mismatched return clock silently rebound to process clock");
}
void clockFault(PipeWireRenderCause cause,const std::function<void(DeviceBlockClock &)> &change){
    PreparedMonitorOutputBank b(bankConfig());PreparedMonitorClockHandoff h(clocksConfig());PreparedPipeWireMonitorRender r(b,h);Planes p;push(b,17);push(b,17,17);
    check(rt([&]{return r.process(clock(),p.views,17);})==PipeWireRenderCode::Content,"Clock fault first packet failed");complete(r,clock(),3);drainClock(h,clock());
    auto bad=clock(1);change(bad);check(rt([&]{return r.process(bad,p.views,17);})==PipeWireRenderCode::Fault,"Invalid native render clock accepted");
    check(r.firstFault()->cause==cause && r.firstFault()->original==bad && b.deliveredFrames()==17,"Clock fault lost original/advanced bank");
    r.finishAfterJoin();
}
void refusals(){
    clockFault(PipeWireRenderCause::RateChanged,[](auto &c){c.rateDenominator=44100;});
    clockFault(PipeWireRenderCause::InvalidQuantum,[](auto &c){c.duration=18;});
    clockFault(PipeWireRenderCause::MissingTimestamp,[](auto &c){c.monotonicNs=0;});
    clockFault(PipeWireRenderCause::ClockChanged,[](auto &c){++c.id;});
    clockFault(PipeWireRenderCause::CycleDiscontinuity,[](auto &c){++c.cycle;});
    clockFault(PipeWireRenderCause::PositionJump,[](auto &c){++c.position;});
    clockFault(PipeWireRenderCause::TimestampReset,[](auto &c){c.monotonicNs=clock().monotonicNs;});
    clockFault(PipeWireRenderCause::PositionOverflow,[](auto &c){c.position=UINT64_MAX-3;});
    PreparedMonitorOutputBank b(bankConfig());PreparedMonitorClockHandoff h(clocksConfig());
    refuses([&]{PreparedPipeWireMonitorRender c(b,h,{0,1,16,{}});});
    auto foreign=clocksConfig();foreign.timestampDomain=MonitorClockDomain::WindowsQpc100ns;PreparedMonitorClockHandoff qpc(foreign);
    refuses([&]{PreparedPipeWireMonitorRender c(b,qpc);});
    auto mismatch=clocksConfig(44100);PreparedMonitorClockHandoff wrong(mismatch);refuses([&]{PreparedPipeWireMonitorRender c(b,wrong);});
    PreparedPipeWireMonitorRender r(b,h);r.unavailable(AudioBridgeStatus::DeviceLost);r.finishAfterJoin();
    check(r.firstFault()->cause==PipeWireRenderCause::NativeUnavailable && !r.firstFault()->original,"Device loss invented callback clock");
}
void formats(){
    for(unsigned channels:{1U,2U,3U,16U,256U})for(unsigned rate:{8000U,44100U,48000U,96000U,192000U,384000U}){
        PreparedMonitorOutputBank b(bankConfig(channels,rate));PreparedMonitorClockHandoff h(clocksConfig(rate));PreparedPipeWireMonitorRender r(b,h);Planes p(channels);
        push(b,17);b.finish();const auto c=clock(0,17,rate);check(rt([&]{return r.process(c,p.views,17);})==PipeWireRenderCode::Complete,"Rate/layout finite render failed");
        const auto expected=samples(17,channels);for(unsigned f=0;f<17;++f)for(unsigned channel=0;channel<channels;++channel)
            check(std::bit_cast<unsigned>(p.views[channel][f])==std::bit_cast<unsigned>(expected[std::size_t(f)*channels+channel]),"Wide/rate native PCM changed");
        complete(r,c,channels);p.guards();r.finishAfterJoin();check(r.endAfterJoin()->monitorQueuedFrames==17,"Wide/rate queue extent changed");
    }
}
void stopAndPressure(){
    for(unsigned cancel=0;cancel<2;++cancel){
        PreparedMonitorOutputBank b(bankConfig());PreparedMonitorClockHandoff h(clocksConfig());PreparedPipeWireMonitorRender r(b,h);Planes p;push(b,17);
        if(cancel)b.requestCancel();else rt([&]{r.requestStop();});
        check(rt([&]{return r.process(clock(),p.views,17);})==PipeWireRenderCode::Stopped && !b.deliveredFrames(),"Stop/cancel consumed monitor data");
        complete(r,clock(),3);r.finishAfterJoin();check(!r.endAfterJoin()->fault && !r.endAfterJoin()->contentComplete && !r.endAfterJoin()->monitorQueuedFrames,"Stop/cancel became completed content");p.guards();
    }
    PreparedMonitorOutputBank b(bankConfig());auto cfg=clocksConfig();cfg.slots=2;PreparedMonitorClockHandoff h(cfg);PreparedPipeWireMonitorRender r(b,h);Planes p;
    for(unsigned packet=0;packet<2;++packet){check(rt([&]{return r.process(clock(packet),p.views,17);})==PipeWireRenderCode::Warmup,"Clock pressure setup failed");complete(r,clock(packet),3);}
    check(rt([&]{return r.process(clock(2),p.views,17);})==PipeWireRenderCode::Fault && r.firstFault()->cause==PipeWireRenderCause::ClockRejected && !b.deliveredFrames(),"Clock queue pressure hidden/consumed content");complete(r,clock(2),3);r.finishAfterJoin();
    PreparedMonitorOutputBank missing(bankConfig());PreparedMonitorClockHandoff mh(clocksConfig());PreparedPipeWireMonitorRender mr(missing,mh);
    check(rt([&]{return mr.process(clock(),p.views,17);})==PipeWireRenderCode::Warmup,"Missing-return setup failed");
    check(rt([&]{return mr.process(clock(1),p.views,17);})==PipeWireRenderCode::Fault && mr.firstFault()->cause==PipeWireRenderCause::MissingReturn && mr.firstFault()->original==clock(),"A new process silently replaced the unreturned cycle");complete(mr,clock(1),3);mr.finishAfterJoin();
}
void minimumQuantum(){
    PreparedMonitorOutputBank b(bankConfig());PreparedMonitorClockHandoff h(clocksConfig());Planes p;push(b,17);
    PipeWireMonitorRenderOptions o;o.minimumQuantumFrames=17;PreparedPipeWireMonitorRender r(b,h,o);const auto small=clock(0,16);
    check(rt([&]{return r.process(small,p.views,17);})==PipeWireRenderCode::Fault && r.firstFault()->cause==PipeWireRenderCause::InvalidQuantum &&
        r.firstFault()->original==small && !b.deliveredFrames(),"Storage minimum violated without original fault/no-consumption");
    for(auto *v:p.views)for(unsigned n=0;n<16;++n)check(v[n]==0,"Minimum-quantum rejection did not silence native output");
    r.finishAfterJoin();
    PreparedMonitorOutputBank b2(bankConfig());PreparedMonitorClockHandoff h2(clocksConfig());o.minimumQuantumFrames=18;
    refuses([&]{PreparedPipeWireMonitorRender bad(b2,h2,o);});
}
}
int main(){try{rt_audit::reset();minimumQuantum();exactAndFinal();warmupAndUnderflow();finishAfterLastContent();returnFacts();refusals();formats();stopAndPressure();const auto a=rt_audit::counts;
    check(!a.cppAllocate && !a.cppFree && !a.cAllocate && !a.cFree && !a.blockingLock,"Prepared render allocated/freed/locked");
    std::cout<<checks<<" PipeWire planar render checks passed; float bits/headroom, exact native queue results, final padding, bounded startup, underflow/refusal; audited allocate/free/lock=0; actual daemon=false\n";return 0;
}catch(const std::exception &e){std::cerr<<"Check "<<checks<<": "<<e.what()<<'\n';return 1;}}
