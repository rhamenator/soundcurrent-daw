// SPDX-License-Identifier: GPL-3.0-only
#include <soundcurrent/wasapi_monitor_render.hpp>
#include <soundcurrent/wasapi_render_lifecycle.hpp>
#include "rt_audit.hpp"
#include <barrier>
#include <bit>
#include <iostream>
#include <thread>
using namespace soundcurrent::daw;
namespace {
unsigned checks=0;
void check(bool ok,const char *why){++checks;if(!ok)throw std::runtime_error(why);}
template<class F> auto rt(F f){rt_audit::Guard g;return f();}
template<class F> void refuses(F f){try{f();}catch(const ProjectError &){check(true,"Refused");return;}check(false,"Bad admission accepted");}
void audit(rt_audit::Counts a){check(!a.cppAllocate && !a.cppFree && !a.cAllocate && !a.cFree && !a.blockingLock,"Prepared renderer allocated/freed/blocked");}
constexpr std::uint64_t base=(std::uint64_t(1)<<54)+50000,pos=(std::uint64_t(1)<<63)+80000,qpc=(std::uint64_t(1)<<54)+90000;
struct Model {
    MonitorOutputBankConfig bc;MonitorClockHandoffConfig cc;WasapiPreparedRenderFormat fmt;
    PreparedMonitorOutputBank bank;PreparedMonitorClockHandoff clocks;PreparedWasapiMonitorRender context;PreparedWasapiRenderBinding binding;
    static MonitorOutputBankConfig output(){MonitorOutputBankConfig b;b.first={base};b.channels=2;b.maximumBlockFrames=8;b.maximumPullFrames=8;b.slots=4;return b;}
    static MonitorClockHandoffConfig clock(unsigned slots){MonitorClockHandoffConfig c;c.renderIdentity={22,31,7};c.renderFrequency=96000;c.slots=slots;return c;}
    explicit Model(unsigned clockSlots=64):bc(output()),cc(clock(clockSlots)),fmt{48000,2,32,8,3,96000,{10000,30000,0,0},cc.renderIdentity},bank(bc),clocks(cc),context(fmt,bank,clocks){}
};
WasapiRenderClock reading(std::uint64_t submitted=0){return {submitted,pos+submitted*2,96000,qpc+submitted*209,0,submitted,0};}
std::array<float,16> samples(unsigned n){std::array<float,16> p;for(unsigned j=0;j<16;++j)p[j]=float(int((n*16+j)%91)-45)/7.f;return p;}
void push(Model &m,unsigned frames,unsigned n=0){auto p=samples(n);check(rt([&]{return m.bank.publish({1,{base+m.bank.acceptedFrames()},frames},std::span<const float>(p).first(frames*2));})==MonitorOutputCode::Accepted,"Output publication");}
struct Source {
    WasapiRenderCallbacks cb;PreparedWasapiRenderLifecycle life;
    explicit Source(Model &m):cb(m.binding.callbacks()),life({cb.context,cb.finished,cb.shouldStop}){life.prepared(m.fmt);}
    WasapiRenderAction fill(float *p,unsigned frames,const WasapiRenderClock &r,std::int32_t release=0){
        const auto a=rt([&]{return cb.fill(cb.context,p,frames,r);});
        rt([&]{life.contentReleased({r,frames,a==WasapiRenderAction::Abort?0u:frames,a,release,true});});return a;
    }
    void finish(std::int32_t stop=0,std::optional<WasapiStreamFailure> failure={}){
        rt([&]{life.stopped(stop);life.finish(failure);});
    }
};
void healthy(){for(unsigned length:{0u,1u,7u,8u,15u,24u}){Model m;check(m.binding.bind(m.context,m.fmt),"Finite bind");
    for(unsigned n=0,at=0;at<length;++n){const auto count=std::min(8u,length-at);push(m,count,n);at+=count;}m.bank.finish();m.clocks.finishCapture();
    Source source(m);rt([&]{source.life.started();});std::array<float,18> out;unsigned consumed=0,native=0;bool final=false;
    for(unsigned n=0;n<8 && !final;++n){out.fill(-999.f);const auto r=reading(native);const auto a=source.fill(out.data()+1,8,r);const auto count=std::min(8u,length-consumed);auto expected=samples(n);
        check(out.front()==-999.f && out.back()==-999.f,"Native fill crossed lease canaries");
        for(unsigned j=0;j<count*2;++j)check(std::bit_cast<std::uint32_t>(out[1+j])==std::bit_cast<std::uint32_t>(expected[j]),"Headroom/float bits changed");
        for(unsigned j=count*2;j<16;++j)check(out[1+j]==0,"Certified final slack uninitialized");
        consumed+=count;native+=8;final=a==WasapiRenderAction::Finish;check(final==(consumed==length),"Finite Finish extent");
        MonitorClockRecord<RenderClockReading> clock;check(rt([&]{return m.clocks.read(clock);}).code==MonitorClockCode::Ready && clock.original.position.value==r.clockPosition && clock.original.qpc.value==r.qpc100ns && clock.original.frequency==r.clockFrequency,"Original render clock changed");
    }
    check(final && !m.context.result() && !m.clocks.renderDone() && !source.cb.shouldStop(source.cb.context),"Content Finish hid native drain/Stop");
    rt([&]{source.life.padding(0);source.life.queueDrained();});source.finish(1);
    const auto result=m.context.result();check(result && result->status==WasapiMonitorRenderStatus::Complete && result->consumedMonitorFrames==length && result->requestedNativeFrames==native && result->finalSlackFrames==native-length && result->monitorEnd->value==base+length && result->original->stopHresult==1 && m.clocks.renderDone(),"Original complete/end/slack domains lost");
    rt([&]{source.cb.finished(source.cb.context,{});source.cb.fill(source.cb.context,out.data()+1,8,reading(native));});
    check(m.binding.firstFault()->afterEnd && m.context.result()->status==WasapiMonitorRenderStatus::Complete && m.context.result()->original->stopHresult==1 && m.bank.deliveredFrames()==length,"Late callback rewrote original native outcome");
}}
void failures(){
    for(unsigned kind=0;kind<8;++kind){Model m(kind==2?1:64);check(m.binding.bind(m.context,m.fmt),"Failure bind");Source source(m);std::array<float,18> out;out.fill(-999.f);
        if(kind==0 || kind==1)push(m,3);else if(kind!=3)push(m,8);
        if(kind==1)m.bank.processingFailed();
        if(kind==3)m.context.requestCancel();
        if(kind>=4)m.bank.finish();
        rt([&]{source.life.started();});
        auto a=source.fill(out.data()+1,8,reading(),kind==5?-23:0);
        if(kind==2){MonitorOutputStamp p{1,{base+8},8};auto pcm=samples(1);check(m.bank.publish(p,pcm)==MonitorOutputCode::Accepted,"Second clock-pressure PCM");a=source.fill(out.data()+1,8,reading(8));}
        if(kind==4){rt([&]{source.cb.unavailable(source.cb.context,-17);source.cb.unavailable(source.cb.context,-19);});}
        if(kind==5){rt([&]{source.cb.unavailable(source.cb.context,-23);});}
        if(kind==6){rt([&]{source.life.padding(0);source.life.queueDrained();});}
        check(!m.context.result() && !m.clocks.renderDone(),"Availability/fault closed EOF before actual SDK Stop");
        if(kind==0)check(a==WasapiRenderAction::Abort && m.context.firstFault()->cause==WasapiMonitorRenderCause::Underflow && m.bank.consumerFault()->cause==MonitorOutputConsumerCause::Underflow && m.bank.deliveredFrames()==3 && out[7]==-999.f,"Underflow disguised as silence/committed prefix");
        if(kind==1)check(a==WasapiRenderAction::Abort && m.bank.deliveredFrames()==0 && out[1]==-999.f && m.context.firstFault()->cause==WasapiMonitorRenderCause::Output,"Faulted prefix committed or backing touched");
        if(kind==2)check(a==WasapiRenderAction::Abort && m.bank.deliveredFrames()==8 && m.bank.acceptedFrames()==16 && m.clocks.renderFault()->cause==MonitorClockCause::QueueFull,"Clock pressure erased unconsumed PCM");
        if(kind==3)check(a==WasapiRenderAction::Abort && !m.bank.deliveredFrames() && out[1]==-999.f,"Cancel changed output backing");
        source.finish(kind==6?-25:kind==4?-17:0,kind==4?std::optional(WasapiStreamFailure{-17,4}):kind==5?std::optional(WasapiStreamFailure{-23,3}):kind==6?std::optional(WasapiStreamFailure{-25,4}):std::nullopt);
        const auto r=m.context.result();check(r && m.clocks.renderDone() && r->original,"Actual SDK end absent");
        if(kind<=2)check(r->status==WasapiMonitorRenderStatus::Fault,"Earlier render cause hidden");
        if(kind==3)check(r->status==WasapiMonitorRenderStatus::Cancelled && r->cancelRequested,"Cancellation became healthy drain");
        if(kind==4)check(r->status==WasapiMonitorRenderStatus::NativeFailure && r->firstUnavailable==-17 && r->original->failure->hresult==-17,"Original first SDK failure changed");
        if(kind==5)check(r->status==WasapiMonitorRenderStatus::NativeFailure && r->consumedMonitorFrames==8 && !r->original->contentCommittedFrames && r->original->lastContentLease->releaseHresult==-23 && !r->original->queueDrainObserved,"Consumption confused with successful drain");
        if(kind==6)check(r->status==WasapiMonitorRenderStatus::NativeFailure && r->original->queueDrainObserved && r->original->stopHresult==-25,"Failed Stop hidden by empty native queue");
        if(kind==7)check(r->status==WasapiMonitorRenderStatus::Interrupted && r->original->contentCommittedFrames==8,"Undrained native end became Complete");
    }
    Model lost;check(lost.binding.bind(lost.context,lost.fmt),"Lost-owner bind");lost.context.settleAfterJoin();check(lost.context.result()->status==WasapiMonitorRenderStatus::OwnerLost && !lost.context.result()->original && lost.clocks.renderDone(),"Missing owner fabricated SDK success");
}
void admission(){Model m;auto f=m.fmt;
    auto posix=m.clocks.config();posix.timestampDomain=MonitorClockDomain::PosixMonotonic100ns;PreparedMonitorClockHandoff foreign(posix);
    refuses([&]{PreparedWasapiMonitorRender bad(f,m.bank,foreign);});
    for(unsigned k=0;k<7;++k){f=m.fmt;if(k==0)f.sampleRate++;if(k==1)f.channels++;if(k==2)f.maximumFillFrames=9;if(k==3)f.clockFrequency++;if(k==4)f.identity.generation++;if(k==5)f.capacityFrames=0;if(k==6)f.timing.endGuardFrames=33;refuses([&]{PreparedWasapiMonitorRender bad(f,m.bank,m.clocks);});}
    f=m.fmt;f.channelMask++;refuses([&]{m.binding.bind(m.context,f);});
    ResourceLedger ledger(65536);WasapiMonitorRenderOptions o;o.resources=ledger;o.memoryBudgetBytes=1;auto before=ledger.usage();refuses([&]{PreparedWasapiRenderBinding bad(o);});check(ledger.usage()==before,"Rejected binding charged resources");
    o.memoryBudgetBytes=65536;{PreparedWasapiRenderBinding b(o);PreparedWasapiMonitorRender c(m.fmt,m.bank,m.clocks,o);check(ledger.usage().reservedBytes==b.chargedBytes()+c.chargedBytes(),"Whole render context/binding charge missing");}check(!ledger.usage().reservedBytes,"Joined render credit leaked");
    Model early;auto cb=early.binding.callbacks();WasapiRenderEnd e;e.prepared=early.fmt;rt([&]{cb.finished(cb.context,e);});check(!early.binding.bind(early.context,early.fmt) && early.binding.originalEnd() && !early.context.result(),"End-before-bind lost original receipt or invented transfer");early.context.settleAfterJoin();check(early.context.result()->status==WasapiMonitorRenderStatus::OwnerLost && !early.context.result()->original,"Unbound SDK end copied as bound success");
    Model unbound;auto c=unbound.binding.callbacks();std::array<float,16> pcm{};check(rt([&]{return c.fill(c.context,pcm.data(),8,reading());})==WasapiRenderAction::Abort && !unbound.binding.bind(unbound.context,unbound.fmt) && !unbound.bank.deliveredFrames(),"Unbound fill consumed PCM");
    Model cancelled;cancelled.binding.requestCancel();check(!cancelled.binding.bind(cancelled.context,cancelled.fmt),"Cancelled binding activated");
    Model invalid;check(invalid.binding.bind(invalid.context,invalid.fmt),"Invalid lease bind");auto cbad=invalid.binding.callbacks();check(rt([&]{return cbad.fill(cbad.context,nullptr,8,reading());})==WasapiRenderAction::Abort && invalid.context.firstFault()->cause==WasapiMonitorRenderCause::InvalidLease,"Null lease read");
}
void stoppedRoutes(){
    for(unsigned kind=0;kind<3;++kind){Model m;check(m.binding.bind(m.context,m.fmt),"Stopped route bind");push(m,8);
        if(kind==0)m.bank.requestStop();else if(kind==1)m.clocks.requestStop();else m.clocks.requestCancel();
        Source source(m);rt([&]{source.life.started();});std::array<float,16> pcm;pcm.fill(-999.f);
        check(source.fill(pcm.data(),8,reading())==WasapiRenderAction::Abort && !m.context.firstFault() && !m.bank.consumerFault() && !m.clocks.renderFault() && !m.bank.deliveredFrames() && pcm.front()==-999.f,"Control Stop/cancel invented render failure or consumed PCM");
        source.finish();check(m.context.result()->status==(kind==2?WasapiMonitorRenderStatus::Cancelled:WasapiMonitorRenderStatus::Interrupted) && m.context.result()->original && !m.context.result()->original->queueDrainObserved,"Stopped route fabricated completion");
    }
}
void originalMetadata(){
    Model m;check(m.binding.bind(m.context,m.fmt),"Missing timestamp bind");push(m,8);m.bank.finish();auto cb=m.binding.callbacks();std::array<float,16> out{};auto r=reading();r.qpc100ns=0;
    check(rt([&]{return cb.fill(cb.context,out.data(),8,r);})==WasapiRenderAction::Finish,"Missing QPC changed finite PCM");
    MonitorClockRecord<RenderClockReading> clock;check(rt([&]{return m.clocks.read(clock);}).code==MonitorClockCode::Ready && clock.original.qpc.value==0 && clock.original.reliability==ClockReliability::Missing,"Missing original QPC fabricated");
    Model mismatch;check(mismatch.binding.bind(mismatch.context,mismatch.fmt),"SDK metadata mismatch bind");push(mismatch,8);mismatch.bank.finish();auto c=mismatch.binding.callbacks();auto original=reading();auto action=rt([&]{return c.fill(c.context,out.data(),8,original);});
    PreparedWasapiRenderLifecycle life;rt([&]{life.prepared(mismatch.fmt);life.started();auto modified=original;modified.qpc100ns++;life.contentReleased({modified,8,8,action,0,true});life.padding(0);life.queueDrained();life.stopped(0);life.finish({});c.finished(c.context,*life.afterJoin());});
    check(mismatch.context.result()->status==WasapiMonitorRenderStatus::InvalidReceipt && mismatch.context.result()->original->lastContentLease->original.qpc100ns==original.qpc100ns+1,"SDK last-clock mismatch admitted or normalized");
}
void bindingRace(){constexpr unsigned rounds=1000;std::barrier phase(3);Model *current=nullptr;bool bound=false;std::array<rt_audit::Counts,2> a{};
    std::thread control([&]{rt_audit::reset();for(unsigned n=0;n<rounds;++n){phase.arrive_and_wait();bound=current->binding.bind(current->context,current->fmt);phase.arrive_and_wait();}a[0]=rt_audit::counts;});
    std::thread source([&]{rt_audit::reset();for(unsigned n=0;n<rounds;++n){phase.arrive_and_wait();auto c=current->binding.callbacks();WasapiRenderEnd e;e.prepared=current->fmt;rt([&]{c.finished(c.context,e);});phase.arrive_and_wait();}a[1]=rt_audit::counts;});
    for(unsigned n=0;n<rounds;++n){Model m;current=&m;phase.arrive_and_wait();phase.arrive_and_wait();check(m.binding.originalEnd() && m.binding.phase()==WasapiRenderBindingPhase::Ended && bool(m.context.result())==bound && m.clocks.renderDone()==bound,"Render bind/end race lost original ownership");if(!bound){m.context.settleAfterJoin();check(m.context.result()->status==WasapiMonitorRenderStatus::OwnerLost,"Unbound race fabricated SDK end");}}
    control.join();source.join();for(auto x:a)audit(x);std::cout<<"1000 actual render control/SDK bind/end races\n";
}
void concurrent(){constexpr unsigned rounds=1000;Model m;check(m.binding.bind(m.context,m.fmt),"Concurrent render bind");m.clocks.finishCapture();std::barrier phase(3);std::array<float,18> backing;std::array<rt_audit::Counts,2> audits{};std::atomic<bool> failed=false;
    std::thread producer([&]{rt_audit::reset();for(unsigned n=0;n<rounds;++n){const unsigned frames=n+1==rounds?7:8;auto p=samples(n);if(rt([&]{return m.bank.publish({1,{base+std::uint64_t(n)*8},frames},std::span<const float>(p).first(frames*2));})!=MonitorOutputCode::Accepted)failed.store(true);if(n+1==rounds)rt([&]{m.bank.finish();});phase.arrive_and_wait();phase.arrive_and_wait();MonitorClockRecord<RenderClockReading> clock;if(rt([&]{return m.clocks.read(clock);}).code!=MonitorClockCode::Ready || clock.original.position.value!=pos+std::uint64_t(n)*16)failed.store(true);phase.arrive_and_wait();}audits[0]=rt_audit::counts;});
    std::thread render([&]{rt_audit::reset();Source source(m);rt([&]{source.life.started();});for(unsigned n=0;n<rounds;++n){phase.arrive_and_wait();backing.fill(-999.f);const auto a=source.fill(backing.data()+1,8,reading(std::uint64_t(n)*8));if(a!=(n+1==rounds?WasapiRenderAction::Finish:WasapiRenderAction::Continue))failed.store(true);phase.arrive_and_wait();phase.arrive_and_wait();}rt([&]{source.life.padding(0);source.life.queueDrained();});source.finish();audits[1]=rt_audit::counts;});
    for(unsigned n=0;n<rounds;++n){phase.arrive_and_wait();phase.arrive_and_wait();auto p=samples(n);const unsigned count=n+1==rounds?14:16;check(backing.front()==-999.f && backing.back()==-999.f,"Concurrent native canaries");for(unsigned j=0;j<count;++j)check(std::bit_cast<std::uint32_t>(backing[j+1])==std::bit_cast<std::uint32_t>(p[j]),"Concurrent original output bits");if(n+1==rounds)check(backing[15]==0 && backing[16]==0 && !m.context.result() && !m.clocks.renderDone(),"Final slack or native end ordering");phase.arrive_and_wait();}
    producer.join();render.join();for(auto x:audits)audit(x);const auto r=m.context.result();check(!failed.load() && r && r->status==WasapiMonitorRenderStatus::Complete && r->consumedMonitorFrames==7999 && r->requestedNativeFrames==8000 && r->finalSlackFrames==1 && m.clocks.renderCounts().accepted==1000 && m.clocks.renderCounts().delivered==1000,"Concurrent output/clock/native drain extent");
}
}
int main(){try{rt_audit::reset();healthy();failures();admission();stoppedRoutes();originalMetadata();bindingRace();concurrent();audit(rt_audit::counts);std::cout<<checks<<" native monitor render checks passed; guarded allocate/free/lock=0; fake SDK only\n";return 0;}catch(const std::exception &e){rt_audit::active=false;std::cerr<<"Check "<<checks<<": "<<e.what()<<'\n';return 1;}}
