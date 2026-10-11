// SPDX-License-Identifier: GPL-3.0-only
#include <soundcurrent/monitor_clock_handoff.hpp>
#include "rt_audit.hpp"
#include <array>
#include <chrono>
#include <iostream>
#include <thread>
using namespace soundcurrent::daw;
namespace {
unsigned checks=0;
void check(bool ok,const char *why){++checks;if(!ok)throw std::runtime_error(why);}
template<class F> auto rt(F f){rt_audit::Guard g;return f();}
template<class F> void refuses(F f){try{f();}catch(const ProjectError &){check(true,"refused");return;}check(false,"Bad handoff admission accepted");}
MonitorClockHandoffConfig config() {
    MonitorClockHandoffConfig c;c.captureIdentity={17,23,3};c.renderIdentity={37,43,5};c.renderFrequency=10000000;
    c.runGeneration=19;c.initialRecordSequence=(std::uint64_t(1)<<63)+17;c.initialQueueSequence=UINT32_MAX-2;return c;
}
CaptureClockReading capture(const MonitorClockHandoffConfig &c,unsigned n) {
    return {c.captureIdentity,{(std::uint64_t(1)<<63)+n},{(std::uint64_t(1)<<54)+std::uint64_t(n)*211},ClockReliability(n%3),n%7==0};
}
RenderClockReading render(const MonitorClockHandoffConfig &c,unsigned n) {
    return {c.renderIdentity,{(std::uint64_t(1)<<63)+std::uint64_t(n)*10000},{(std::uint64_t(1)<<54)+std::uint64_t(n)*211},c.renderFrequency,ClockReliability(n%3),n%11==0};
}
bool same(const CaptureClockReading &a,const CaptureClockReading &b) {
    return a.identity==b.identity && a.position.value==b.position.value && a.qpc.value==b.qpc.value && a.reliability==b.reliability && a.discontinuity==b.discontinuity;
}
bool same(const RenderClockReading &a,const RenderClockReading &b) {
    return a.identity==b.identity && a.position.value==b.position.value && a.qpc.value==b.qpc.value && a.frequency==b.frequency && a.reliability==b.reliability && a.discontinuity==b.discontinuity;
}
void recordsAndAdmission() {
    auto c=config();ResourceLedger ledger(65536);c.resources=ledger;
    {
        PreparedMonitorClockHandoff q(c);check(ledger.usage().reservedBytes==q.chargedBytes(),"Handoff payload not charged");
        for(unsigned n=0;n<200;++n) {
            auto a=capture(c,n);auto b=render(c,n);const auto originalA=a;const auto originalB=b;
            if(n==1){a.qpc={0};a.position={0};b.qpc={0};b.position={0};}
            const auto expectedA=a;const auto expectedB=b;
            check(rt([&]{return q.publish(a);})==MonitorClockCode::Accepted && rt([&]{return q.publish(b);})==MonitorClockCode::Accepted,"Original handoff rejected");
            a=originalA;b=originalB;a.position={7};b.frequency=7;
            MonitorClockRecord<CaptureClockReading> ar;MonitorClockRecord<RenderClockReading> br;
            check(rt([&]{return q.read(ar);}).code==MonitorClockCode::Ready && rt([&]{return q.read(br);}).code==MonitorClockCode::Ready,"Original handoff missing");
            check(same(ar.original,expectedA) && same(br.original,expectedB) && ar.sequence==c.initialRecordSequence+n && br.sequence==ar.sequence && ar.runGeneration==19 && br.runGeneration==19,"Original fields/sequence/units changed");
        }
        rt([&]{q.finishCapture();q.finishRender();});MonitorClockRecord<CaptureClockReading> r{91,92,capture(c,0)};
        check(rt([&]{return q.read(r);}).code==MonitorClockCode::Drained && r.sequence==92,"Empty handoff touched destination");
        q.settleAfterJoin();check(q.captureCounts().accepted==200 && q.captureCounts().delivered==200 && !q.captureCounts().discarded,"Joined count differs");
    }
    check(!ledger.usage().reservedBytes && !ledger.usage().owners,"Handoff credit leaked");
    for(unsigned n=0;n<7;++n) {
        auto bad=config();
        if(n==0)bad.captureIdentity.generation=0;
        if(n==1)bad.renderFrequency=0;
        if(n==2)bad.runGeneration=0;
        if(n==3)bad.slots=0;
        if(n==4)bad.slots=65;
        if(n==5)bad.initialRecordSequence=UINT64_MAX;
        if(n==6)bad.memoryBudgetBytes=1;
        refuses([&]{PreparedMonitorClockHandoff q(bad);});
    }
    auto small=config();ResourceLedger tiny(1);small.resources=tiny;refuses([&]{PreparedMonitorClockHandoff q(small);});check(!tiny.usage().reservedBytes,"Refused handoff credit leaked");
}
void faultsAndTerminal() {
    for(unsigned kind=0;kind<5;++kind) {
        auto c=config();c.slots=1;if(kind==4)c.initialRecordSequence=UINT64_MAX-1;
        PreparedMonitorClockHandoff q(c);auto a=capture(c,0);auto b=render(c,0);
        if(kind==0)++a.identity.endpoint;
        if(kind==1)a.reliability=ClockReliability(99);
        if(kind==2)++b.frequency;
        if(kind>=3){check(q.publish(a)==MonitorClockCode::Accepted,"Fault prefix publication");a=capture(c,1);}
        const auto code=kind==2?rt([&]{return q.publish(b);}):rt([&]{return q.publish(a);});
        check(code==MonitorClockCode::Fault && q.phase()==MonitorClockPhase::Fault,"Source fault hidden");
        if(kind==2)check(q.renderFault()->cause==MonitorClockCause::Frequency && same(*q.renderFault()->attempted,b),"Frequency fault lost original");
        else {
            const auto fault=q.captureFault();check(fault && fault->attempted && same(*fault->attempted,a),"Capture fault lost original");
            a.position={91};rt([&]{q.publish(a);});check(same(*q.captureFault()->attempted,*fault->attempted),"First source fault replaced");
        }
        MonitorClockRecord<CaptureClockReading> r;
        const auto read=rt([&]{return q.read(r);});
        check(read.phase==MonitorClockPhase::Fault && read.code==(kind>=3?MonitorClockCode::Ready:MonitorClockCode::Fault),"Fault prefix reported healthy");
        rt([&]{q.finishCapture();q.finishRender();});q.settleAfterJoin();
    }
    for(unsigned kind=0;kind<3;++kind) {
        const auto c=config();PreparedMonitorClockHandoff q(c);
        for(unsigned n=0;n<5;++n)q.publish(capture(c,n));
        MonitorClockRecord<CaptureClockReading> r;check(q.read(r).code==MonitorClockCode::Ready,"Terminal prefix");
        if(kind==0)rt([&]{q.requestStop();});
        if(kind==1)rt([&]{q.requestCancel();});
        rt([&]{q.finishCapture();q.finishRender();});
        if(kind==2)rt([&]{q.consumerFailed();});
        if(kind==0)for(unsigned n=1;n<5;++n)check(rt([&]{return q.read(r);}).code==MonitorClockCode::Ready,"Stop discarded original prefix");
        else {
            const auto before=r;const auto read=rt([&]{return q.read(r);});
            check(read.code==(kind==1?MonitorClockCode::Cancelled:MonitorClockCode::Fault) && same(r.original,before.original) && r.sequence==before.sequence,"Cancelled/lost read touched destination");
        }
        q.settleAfterJoin();q.settleAfterJoin();const auto counts=q.captureCounts();
        check(counts.accepted==5 && counts.delivered==(kind==0?5u:1u) && counts.discarded==(kind==0?0u:4u),"Joined discard/count/idempotence");
        if(kind==2)check(q.captureFault()->cause==MonitorClockCause::ConsumerLost && !q.captureFault()->attempted && !q.renderFault()->attempted,"Late loss invented reading");
    }
    PreparedMonitorClockHandoff abandoned(config());abandoned.settleAfterJoin();
    check(abandoned.captureFault()->cause==MonitorClockCause::ProducerFailed && !abandoned.captureFault()->attempted && abandoned.captureDone() && abandoned.renderDone(),"Abandoned clock source not faulted");
}
void concurrent() {
    const auto c=config();PreparedMonitorClockHandoff q(c);std::atomic<bool> failed{false};std::array<rt_audit::Counts,3> audits{};
    const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(10);
    const auto expired=[&]{return failed.load() || std::chrono::steady_clock::now()>=deadline;};
    std::thread producerA([&]{
        rt_audit::reset();for(unsigned n=0;n<20000 && !expired();++n) {
            while(n-q.captureCounts().delivered>=4 && !expired())std::this_thread::yield();
            if(expired())break;
            if(rt([&]{return q.publish(capture(c,n));})!=MonitorClockCode::Accepted)failed.store(true);
        }
        rt([&]{q.finishCapture();});audits[0]=rt_audit::counts;
    });
    std::thread producerB([&]{
        rt_audit::reset();for(unsigned n=0;n<20000 && !expired();++n) {
            while(n-q.renderCounts().delivered>=4 && !expired())std::this_thread::yield();
            if(expired())break;
            if(rt([&]{return q.publish(render(c,n));})!=MonitorClockCode::Accepted)failed.store(true);
        }
        rt([&]{q.finishRender();});audits[1]=rt_audit::counts;
    });
    std::thread consumer([&]{
        rt_audit::reset();unsigned a=0,b=0;
        while((a<20000 || b<20000) && !expired()) {
            MonitorClockRecord<CaptureClockReading> ar;MonitorClockRecord<RenderClockReading> br;
            const auto x=rt([&]{return q.read(ar);});const auto y=rt([&]{return q.read(br);});
            if(x.phase==MonitorClockPhase::Fault || y.phase==MonitorClockPhase::Fault)failed.store(true);
            if(x.code==MonitorClockCode::Ready){if(!same(ar.original,capture(c,a)) || ar.sequence!=c.initialRecordSequence+a || ar.runGeneration!=19)failed.store(true);++a;}
            if(y.code==MonitorClockCode::Ready){if(!same(br.original,render(c,b)) || br.sequence!=c.initialRecordSequence+b || br.runGeneration!=19)failed.store(true);++b;}
            std::this_thread::yield();
        }
        if(a!=20000 || b!=20000)failed.store(true);
        audits[2]=rt_audit::counts;
    });
    producerA.join();producerB.join();consumer.join();q.settleAfterJoin();
    check(!failed.load() && q.captureCounts().accepted==20000 && q.captureCounts().delivered==20000 && q.renderCounts().delivered==20000,"Concurrent original readings differ");
    for(const auto &a:audits)check(!a.cppAllocate && !a.cppFree && !a.cAllocate && !a.cFree && !a.blockingLock,"Concurrent clock audit");
    std::cout<<"concurrent clock owners=3 originalReadingsPerSource=20000; logical credit waits outside prepared methods\n";
}
} // namespace
int main() {
    try {rt_audit::reset();recordsAndAdmission();faultsAndTerminal();concurrent();
        check(!rt_audit::counts.cppAllocate && !rt_audit::counts.cppFree && !rt_audit::counts.cAllocate && !rt_audit::counts.cFree && !rt_audit::counts.blockingLock,"Clock prepared RT audit");
        std::cout<<checks<<" original clock handoff checks passed; guarded allocate/free/lock=0\n";return 0;
    }catch(const std::exception &e){rt_audit::active=false;std::cerr<<"Check "<<checks<<": "<<e.what()<<'\n';return 1;}
}
