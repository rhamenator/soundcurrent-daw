// SPDX-License-Identifier: GPL-3.0-only
#include <soundcurrent/wasapi_monitor_capture.hpp>
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
template<class F> void refuses(F f){try{f();}catch(const ProjectError &){check(true,"refused");return;}check(false,"Admission unexpectedly accepted");}
void audit(rt_audit::Counts a){check(!a.cppAllocate && !a.cppFree && !a.cAllocate && !a.cFree && !a.blockingLock,"Prepared callback allocated/freed/blocked");}
constexpr std::uint64_t pos=(std::uint64_t(1)<<63)+9000,qpc=(std::uint64_t(1)<<54)+7000;
struct Model {
    CaptureFanoutConfig fc;
    MonitorClockHandoffConfig cc;
    MonitorCaptureStopConfig sc;
    WasapiPreparedCaptureFormat format;
    PreparedCaptureFanout f;
    PreparedMonitorClockHandoff clocks;
    PreparedMonitorCaptureStop stop;
    PreparedWasapiMonitorCapture context;
    PreparedWasapiCaptureBinding binding;
    static CaptureFanoutConfig capture(unsigned monitor){CaptureFanoutConfig c;c.capture.identity={11,31,7};c.capture.maximumPacketFrames=8;c.capture.slots=4;c.monitorSlots=monitor;return c;}
    static MonitorClockHandoffConfig clock(unsigned slots){MonitorClockHandoffConfig c;c.captureIdentity={11,31,7};c.slots=slots;return c;}
    static MonitorCaptureStopConfig stopConfig(unsigned monitor){MonitorCaptureStopConfig c;c.maximumPacketFrames=8;c.monitorSlots=monitor;return c;}
    Model(unsigned monitor=4,unsigned clockSlots=8):fc(capture(monitor)),cc(clock(clockSlots)),sc(stopConfig(monitor)),
        format{48000,1,8,4,false,fc.capture.identity},f(fc),clocks(cc),stop(sc),context(format,f,clocks,stop) {}
};
WasapiCaptureEnd ending(unsigned packets=0,bool started=false,std::int32_t hr=0){WasapiCaptureEnd e;e.started=started;
    if(started)e.stopHresult=hr;
    if(packets){e.deliveredPackets=packets;e.deliveredFrames=packets*8;e.lastDelivered=WasapiDeliveredPacket{8,0,pos+(packets-1)*8,qpc+(packets-1)*1700};}return e;}
void drain(PreparedCaptureFanout &f,CaptureFanoutSide side,unsigned frames){unsigned got=0;CaptureBankView v;
    while((side==CaptureFanoutSide::Raw?rt([&]{return f.acquireRaw(v);}):rt([&]{return f.acquireMonitor(v);}))==CaptureBankRead::Ready){got+=v.token.original.frames;check(side==CaptureFanoutSide::Raw?rt([&]{return f.releaseRaw(v);}):rt([&]{return f.releaseMonitor(v);}),"Release failed");}
    check(got==frames,"Unexpected original bank extent");}
void metadata(){Model m;auto cb=m.binding.callbacks();check(!m.binding.originalEnd() && !cb.shouldStop(cb.context),"Prepared owner invented end/Stop");
    check(m.binding.bind(m.context,m.format) && !m.binding.bind(m.context,m.format),"Binding is not one-shot");
    std::array<float,8> pcm{2.5f,-3.f,std::bit_cast<float>(std::uint32_t{0x7fc01234}),0,1,2,3,4};const auto expected=pcm;
    rt([&]{cb.packet(cb.context,{reinterpret_cast<const std::byte *>(pcm.data()),sizeof(pcm),8,wasapiDiscontinuity,pos,qpc});});pcm.fill(999.f);
    CaptureBankView raw,monitor;check(rt([&]{return m.f.acquireRaw(raw);})==CaptureBankRead::Ready && rt([&]{return m.f.acquireMonitor(monitor);})==CaptureBankRead::Ready,"Bank delivery missing");
    for(unsigned n=0;n<8;++n)check(std::bit_cast<std::uint32_t>(raw.interleaved[n])==std::bit_cast<std::uint32_t>(expected[n]) && std::bit_cast<std::uint32_t>(monitor.interleaved[n])==std::bit_cast<std::uint32_t>(expected[n]),"Original float bits changed");
    check(raw.token.original.position.value==pos && raw.token.original.qpc.value==qpc && raw.token.original.flags==wasapiDiscontinuity,"Wide original metadata narrowed");
    check(rt([&]{return m.f.releaseRaw(raw);}) && rt([&]{return m.f.releaseMonitor(monitor);}),"Metadata lease release");
    MonitorClockRecord<CaptureClockReading> r;check(rt([&]{return m.clocks.read(r);}).code==MonitorClockCode::Ready && r.original.position.value==pos && r.original.qpc.value==qpc && r.original.discontinuity && r.original.reliability==ClockReliability::Accurate,"Original clock reading altered");
    auto e=ending(1,true,1);e.lastDelivered->flags=wasapiDiscontinuity;
    rt([&]{cb.finished(cb.context,e);cb.finished(cb.context,ending(0,true,-17));});
    check(m.f.producerDone() && m.clocks.captureDone() && m.stop.endReceipt()->original.stopHresult==1 && m.binding.originalEnd()->stopHresult==1,"Actual end/EOF/first result lost");
    check(m.binding.phase()==WasapiCaptureBindingPhase::Ended && cb.shouldStop(cb.context),"Ended owner can acquire");
    rt([&]{cb.packet(cb.context,{nullptr,0,8,wasapiSilent,pos+8,qpc+1700});});
    check(m.binding.firstFault()->cause==WasapiCaptureBindingCause::PacketAfterEnd && m.f.rawAcceptedFrames()==8 && m.binding.originalEnd()->deliveredPackets==1,"Late callback mutated original prefix/end");
    m.context.settleAfterJoin();check(m.stop.endReceipt()->status==MonitorCaptureEndStatus::Healthy,"Joined healthy end rewritten");
}
void pressureAndFailures(){
    for(unsigned kind=0;kind<5;++kind){Model m(kind==0?1:4,kind==1?1:8);auto cb=m.binding.callbacks();check(m.binding.bind(m.context,m.format),"Pressure bind");std::array<float,8> pcm{};
        rt([&]{cb.packet(cb.context,{reinterpret_cast<const std::byte *>(pcm.data()),sizeof(pcm),8,0,pos,qpc});
            cb.packet(cb.context,{kind==2?reinterpret_cast<const std::byte *>(1):reinterpret_cast<const std::byte *>(pcm.data()),kind==2?0:sizeof(pcm),8,kind==2?wasapiSilent:kind==3?wasapiTimestampError:0u,pos+8,qpc+1700});});
        if(kind==0)check(cb.shouldStop(cb.context) && m.f.firstFault()->side==CaptureFanoutSide::Monitor && m.f.firstFault()->rawPacketAccepted && m.f.rawAcceptedFrames()==16 && m.f.monitorAcceptedFrames()==8,"Monitor pressure erased original raw packet");
        if(kind==1)check(cb.shouldStop(cb.context) && m.clocks.captureFault()->cause==MonitorClockCause::QueueFull && m.f.rawAcceptedFrames()==16,"Clock pressure erased raw packet");
        if(kind==2){CaptureBankView v;check(m.f.acquireMonitor(v)==CaptureBankRead::Ready && m.f.releaseMonitor(v),"Silent first lease");check(m.f.acquireMonitor(v)==CaptureBankRead::Ready,"Silent second lease");for(float x:v.interleaved)check(x==0,"Silent stale backing was read");check(v.token.original.flags==wasapiSilent && m.f.releaseMonitor(v),"Silent original stamp/release");}
        if(kind==3)check(cb.shouldStop(cb.context) && m.f.firstFault()->cause==CaptureBankStatus::TimestampUnavailable && m.clocks.captureCounts().accepted==2,"Timestamp failure not preserved");
        if(kind==4){rt([&]{cb.unavailable(cb.context,-25);cb.unavailable(cb.context,-29);});check(!m.f.producerDone() && !m.stop.endReceipt() && cb.shouldStop(cb.context) && m.context.firstUnavailable()==-25 && m.binding.firstUnavailable()==-25,"Availability failure hid Stop outcome or closed EOF early");}
        auto e=ending(2,true,kind==4?-17:0);if(kind==4)e.failure=WasapiStreamFailure{-25,2};
        rt([&]{cb.finished(cb.context,e);});check(m.f.producerDone() && m.stop.endReceipt()->original.deliveredPackets==2,"Actual end before EOF missing");
        if(kind==4)check(m.stop.endReceipt()->status==MonitorCaptureEndStatus::NativeFailure && m.stop.endReceipt()->original.failure->hresult==-25 && m.stop.endReceipt()->original.stopHresult==-17,"First SDK failure/Stop result collapsed");
        drain(m.f,CaptureFanoutSide::Raw,kind==3?8:16);if(kind!=2)drain(m.f,CaptureFanoutSide::Monitor,kind==0 || kind==3?8:16);
    }
    Model m;auto cb=m.binding.callbacks();rt([&]{cb.packet(cb.context,{nullptr,0,8,wasapiSilent,pos,qpc});});check(m.binding.firstFault()->cause==WasapiCaptureBindingCause::UnboundPacket && !m.binding.bind(m.context,m.format) && !m.f.rawAcceptedFrames(),"Unbound callback accepted");
    Model missing;check(missing.binding.bind(missing.context,missing.format),"Missing-owner bind");missing.context.settleAfterJoin();check(missing.stop.endReceipt()->status==MonitorCaptureEndStatus::OwnerLost && !missing.stop.endReceipt()->original.stopHresult && missing.f.producerDone() && missing.clocks.captureFault()->cause==MonitorClockCause::ProducerFailed,"Missing owner represented as successful Stop");
}
void admission(){Model m;auto fmt=m.format;fmt.identity.stream++;refuses([&]{PreparedWasapiMonitorCapture bad(fmt,m.f,m.clocks,m.stop);});refuses([&]{m.binding.bind(m.context,fmt);});
    for(unsigned kind=0;kind<3;++kind){auto f=m.format;if(kind==0)f.sampleRate++;if(kind==1)f.channels++;if(kind==2)f.maximumPacketFrames++;refuses([&]{PreparedWasapiMonitorCapture bad(f,m.f,m.clocks,m.stop);});}
    ResourceLedger ledger(65536,"Capture context test");WasapiMonitorCaptureOptions o;o.resources=ledger;o.memoryBudgetBytes=1;auto before=ledger.usage();refuses([&]{PreparedWasapiCaptureBinding bad(o);});check(ledger.usage()==before,"Failed admission changed ledger");
    o.memoryBudgetBytes=65536;{PreparedWasapiCaptureBinding b(o);PreparedWasapiMonitorCapture c(m.format,m.f,m.clocks,m.stop,o);check(ledger.usage().reservedBytes==b.chargedBytes()+c.chargedBytes(),"Whole context/binding charge missing");}check(!ledger.usage().reservedBytes,"Joined context/binding credit leaked");
    Model stale;stale.context.settleAfterJoin();check(!stale.binding.bind(stale.context,stale.format),"Retired context bound to new source");
    Model early;auto cb=early.binding.callbacks();rt([&]{cb.finished(cb.context,{});});check(!early.binding.bind(early.context,early.format) && early.binding.originalEnd() && !early.binding.originalEnd()->started && !early.binding.originalEnd()->stopHresult,"End-before-bind invented Start/Stop");
}
void bindingRace(){constexpr unsigned rounds=1000;std::barrier phase(3);Model *current=nullptr;bool accepted=false;std::array<rt_audit::Counts,2> audits{};
    std::thread control([&]{rt_audit::reset();for(unsigned n=0;n<rounds;++n){phase.arrive_and_wait();accepted=current->binding.bind(current->context,current->format);phase.arrive_and_wait();}audits[0]=rt_audit::counts;});
    std::thread source([&]{rt_audit::reset();for(unsigned n=0;n<rounds;++n){phase.arrive_and_wait();auto cb=current->binding.callbacks();rt([&]{cb.finished(cb.context,{});});phase.arrive_and_wait();}audits[1]=rt_audit::counts;});
    for(unsigned n=0;n<rounds;++n){Model m;current=&m;phase.arrive_and_wait();phase.arrive_and_wait();check(m.binding.phase()==WasapiCaptureBindingPhase::Ended && m.binding.originalEnd() && !m.binding.originalEnd()->started,"Race lost original native end");check(m.f.producerDone()==accepted && bool(m.stop.endReceipt())==accepted,"Bind/end race lost or duplicated EOF handoff");if(!accepted){m.context.settleAfterJoin();check(m.stop.endReceipt()->status==MonitorCaptureEndStatus::OwnerLost,"Unbound context fabricated an SDK result");}}
    control.join();source.join();for(auto a:audits)audit(a);std::cout<<"1000 actual control/SDK-owner bind/end races; original end always retained\n";
}
}
int main(){try{rt_audit::reset();metadata();pressureAndFailures();admission();bindingRace();audit(rt_audit::counts);std::cout<<checks<<" native capture context checks passed; guarded allocate/free/lock=0; fake SDK only\n";return 0;}catch(const std::exception &e){rt_audit::active=false;std::cerr<<"Check "<<checks<<": "<<e.what()<<'\n';return 1;}}
