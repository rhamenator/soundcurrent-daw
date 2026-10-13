// SPDX-License-Identifier: GPL-3.0-only
#include <soundcurrent/pipewire_receipt_queues.hpp>
#include "rt_audit.hpp"
#include <array>
#include <iostream>
#include <thread>
using namespace soundcurrent::daw;
namespace {
unsigned checks=0;
void check(bool ok,const char *why){++checks;if(!ok)throw std::runtime_error(why);}
template<class F> auto rt(F f){rt_audit::Guard guard;return f();}
template<class F> void refuses(F f){try{f();}catch(const ProjectError &){++checks;return;}throw std::runtime_error("Receipt admission accepted invalid policy");}
DeviceBlockClock clock(unsigned n=0){return {1000+std::uint64_t(n)*17,17,1000000000+std::uint64_t(n)*354167,10,n+4,1,48000,-37,false,false};}
PipeWireCaptureCycle capture(unsigned n=0){
    PipeWireCaptureCycle c;c.original=clock(n);c.returnClock=c.original;c.requestedFrames=c.rawFrames=c.monitorFrames=17;
    c.channels=3;c.returnedChannels=3;c.allReturned=true;
    for(unsigned k=0;k<3;++k)c.returns[k]={k==1?PipeWireBufferStatus::Silence:PipeWireBufferStatus::Ready,true,int(k)};
    return c;
}
PipeWireRenderCycle render(unsigned n=0){
    PipeWireRenderCycle c;c.original=clock(n);c.returnClock=c.original;c.requestedFrames=c.monitorFrames=17;
    c.channels=3;c.returnedChannels=3;c.allQueued=true;
    for(unsigned k=0;k<3;++k)c.returns[k]={PipeWireBufferStatus::Ready,true,int(k)};
    return c;
}
void transport(){
    ResourceLedger ledger(2*1024*1024,"Receipt test");
    {
        PreparedPipeWireReceiptQueues q({99,123,4,2*1024*1024,ledger});check(ledger.usage().reservedBytes>0,"Whole fixed lanes not charged");
        for(unsigned n=0;n<4;++n){const auto c=capture(n);const auto r=render(n);
            check(rt([&]{return q.publish(c);})==PipeWireReceiptCode::Accepted && rt([&]{return q.publish(r);})==PipeWireReceiptCode::Accepted,"Receipt lanes rejected valid records");}
        q.finishCapture();q.finishRender();
        for(unsigned n=0;n<4;++n){PipeWireReceiptRecord<PipeWireCaptureCycle> c;PipeWireReceiptRecord<PipeWireRenderCycle> r;
            check(rt([&]{return q.read(c);})==PipeWireReceiptCode::Ready && rt([&]{return q.read(r);})==PipeWireReceiptCode::Ready,"Joined receipt prefix missing");
            check(c.generation==99 && r.generation==99 && c.sequence==123+n && r.sequence==123+n && c.original.returnClock==clock(n) &&
                r.original.original==clock(n) && c.original.returns[1].status==PipeWireBufferStatus::Silence && c.original.returns[2].queueResult==2,
                "Copied original fields/sequence/domain changed");}
        PipeWireReceiptRecord<PipeWireCaptureCycle> c;PipeWireReceiptRecord<PipeWireRenderCycle> r;
        check(q.read(c)==PipeWireReceiptCode::Drained && q.read(r)==PipeWireReceiptCode::Drained && q.publish(capture())==PipeWireReceiptCode::Stopped,"Receipt EOF allowed later publications");
    }
    check(!ledger.usage().reservedBytes,"Receipt ownership credit did not retire");
}
void pressure(){
    PreparedPipeWireReceiptQueues q({1,0,2});
    for(unsigned n=0;n<2;++n)check(rt([&]{return q.publish(capture(n));})==PipeWireReceiptCode::Accepted,"Pressure setup failed");
    auto bad=capture(2);bad.returns[1].queueResult=-32;bad.allReturned=false;
    check(rt([&]{return q.publish(bad);})==PipeWireReceiptCode::Fault,"Full native metadata lane accepted another record");
    const auto original=q.captureFault();check(original && original->cause==PipeWireReceiptCause::QueueFull && original->attempted->sequence==2 &&
        original->attempted->original.returns[1].queueResult==-32,"Rejected original native result lost");
    check(q.publish(capture(3))==PipeWireReceiptCode::Fault && q.captureFault()->attempted->original.returnClock==bad.returnClock,"First fault overwritten");
    for(unsigned n=0;n<2;++n){PipeWireReceiptRecord<PipeWireCaptureCycle> c;check(q.read(c)==PipeWireReceiptCode::Ready && c.sequence==n,"Fault erased accepted metadata prefix");}
    PipeWireReceiptRecord<PipeWireCaptureCycle> c;check(q.read(c)==PipeWireReceiptCode::Fault,"Faulty drained lane became healthy EOF");
    check(q.publish(render())==PipeWireReceiptCode::Accepted,"Capture lane fault spuriously erased independent render lane");
    PreparedPipeWireReceiptQueues overflow({1,UINT64_MAX-1,2});check(overflow.publish(render())==PipeWireReceiptCode::Accepted &&
        overflow.publish(render(1))==PipeWireReceiptCode::Fault && overflow.renderFault()->cause==PipeWireReceiptCause::SequenceOverflow,"Receipt sequence wrapped");
    PreparedPipeWireReceiptQueues lost;lost.consumerFailed();check(lost.publish(capture())==PipeWireReceiptCode::Fault && lost.captureFault()->cause==PipeWireReceiptCause::ConsumerLost,"Consumer loss hidden");
    PreparedPipeWireReceiptQueues invalid;auto wrong=capture();wrong.returnClock.reset();check(invalid.publish(wrong)==PipeWireReceiptCode::Fault && invalid.captureFault()->cause==PipeWireReceiptCause::InvalidCycle,"Missing original native return accepted");
    refuses([]{PreparedPipeWireReceiptQueues invalid({0,0,2});});refuses([]{PreparedPipeWireReceiptQueues invalid({1,0,0});});
    refuses([]{PreparedPipeWireReceiptQueues invalid({1,0,2,16});});
}
CaptureFanoutConfig fanConfig(){CaptureFanoutConfig c;c.capture.identity={11,31,7};c.capture.channels=3;c.capture.maximumPacketFrames=17;c.capture.slots=4;c.monitorSlots=4;return c;}
MonitorClockHandoffConfig handoff(){MonitorClockHandoffConfig c;c.captureIdentity={11,31,7};c.timestampDomain=MonitorClockDomain::PosixMonotonic100ns;return c;}
void captureReturns(){
    for(unsigned mode=0;mode<7;++mode){
        PreparedCaptureFanout f(fanConfig());PreparedMonitorClockHandoff h(handoff());PipeWireMonitorCaptureOptions options;options.requireNativeReturns=true;
        PreparedPipeWireMonitorCapture c(f,h,options);std::array<float,17> data{},neutral{};data.fill(2.5F);std::array<const float *,3> planes{data.data(),neutral.data(),data.data()};
        check(rt([&]{return c.process(clock(),planes,17);})==PipeWireCaptureCode::Accepted,"Return fixture raw publication failed");
        auto native=capture().returns;auto actual=clock();
        if(mode==1)native[1].queueResult=-32;
        if(mode==2)native[1].queueResult.reset();
        if(mode==3)native[1].acquired=false;
        if(mode==4)native[1].status=PipeWireBufferStatus::Unavailable;
        if(mode==5)++actual.cycle;
        if(mode!=6)rt([&]{c.completeCycle({actual,true,std::span(native).first(3),{}});});
        c.finishAfterJoin();const auto e=c.endAfterJoin();
        check(e->acceptedRawFrames==17 && e->acceptedMonitorFrames==17,"Failed native return erased retained raw/monitor packet");
        if(!mode)check(!e->fault && e->nativeReturnedFrames==17 && e->lastCycle->returns[1].status==PipeWireBufferStatus::Silence,"Actual neutral input/native return not retained");
        else check(e->fault && !e->nativeReturnedFrames,"Bad native return became successful native-return extent");
        if(mode==5)check(e->lastCycle->original==clock() && e->lastCycle->returnClock==actual,"Return clock rebound to process clock");
        if(mode==6)check(e->fault->cause==PipeWireCaptureCause::MissingReturn && !e->lastCycle,"Missing return invented native operation");
    }
    PreparedPipeWireReceiptQueues q({1,0,1});PreparedCaptureFanout f(fanConfig());PreparedMonitorClockHandoff h(handoff());
    PipeWireMonitorCaptureOptions o;o.requireNativeReturns=true;o.receiptContext=&q;o.receipt=PreparedPipeWireReceiptQueues::captureSink;
    PreparedPipeWireMonitorCapture c(f,h,o);std::array<float,17> data{};std::array<const float *,3> planes{data.data(),data.data(),data.data()};
    const auto native=capture().returns;
    for(unsigned n=0;n<2;++n){check(rt([&]{return c.process(clock(n),planes,17);})==PipeWireCaptureCode::Accepted,"Metadata pressure source packet refused too early");rt([&]{c.completeCycle({clock(n),true,std::span(native).first(3),{}});});}
    c.finishAfterJoin();check(c.firstFault()->cause==PipeWireCaptureCause::ReceiptPressure && c.endAfterJoin()->faultCycle &&
        c.endAfterJoin()->acceptedRawFrames==34 && c.endAfterJoin()->nativeReturnedFrames==34 && q.captureFault()->attempted->original.rawFrames==17,
        "Metadata refusal erased raw/native facts or was hidden");
}
void owners(){
    PreparedPipeWireReceiptQueues q({11,0,64});std::atomic<unsigned> allowed{0},finished{0};std::atomic<bool> bad{false};std::array<rt_audit::Counts,2> audits{};
    auto run=[&](unsigned lane){rt_audit::reset();for(unsigned n=0;n<1000;++n){while(allowed.load(std::memory_order_acquire)!=n)std::this_thread::yield();
        const auto code=lane?rt([&]{return q.publish(render(n));}):rt([&]{return q.publish(capture(n));});if(code!=PipeWireReceiptCode::Accepted)bad=true;
        finished.fetch_add(1,std::memory_order_release);}audits[lane]=rt_audit::counts;};
    std::jthread captureOwner(run,0),renderOwner(run,1);
    for(unsigned n=0;n<1000;++n){while(finished.load(std::memory_order_acquire)<(n+1)*2)std::this_thread::yield();
        PipeWireReceiptRecord<PipeWireCaptureCycle> c;PipeWireReceiptRecord<PipeWireRenderCycle> r;
        check(q.read(c)==PipeWireReceiptCode::Ready && q.read(r)==PipeWireReceiptCode::Ready && c.sequence==n && r.sequence==n &&
            c.original.original==clock(n) && r.original.returnClock==clock(n),"Independent native owners corrupted/lost receipt");allowed.store(n+1,std::memory_order_release);}
    captureOwner.join();renderOwner.join();check(!bad,"Native receipt owner failed");
    for(auto a:audits)check(!a.cppAllocate && !a.cppFree && !a.cAllocate && !a.cFree && !a.blockingLock,"Native handoff allocated/freed/locked");
}
void finalPublication(){
    for(unsigned n=0;n<100;++n){
        PreparedPipeWireReceiptQueues q({1,0,1});std::atomic<bool> begin{false};
        std::jthread producer([&]{while(!begin.load(std::memory_order_acquire))std::this_thread::yield();
            q.publish(capture(n));q.finishCapture();});
        begin.store(true,std::memory_order_release);unsigned count=0;
        for(;;){PipeWireReceiptRecord<PipeWireCaptureCycle> c;const auto code=q.read(c);
            if(code==PipeWireReceiptCode::Ready){check(c.original.returnClock==clock(n),"Final original receipt changed");++count;}
            else if(code==PipeWireReceiptCode::Drained)break;
            else {check(code==PipeWireReceiptCode::Empty,"Final publication became fault");std::this_thread::yield();}
        }
        producer.join();check(count==1,"EOF overtook final native publication");
    }
}
void extendedCapacity(){
    for(unsigned slots:{300u,4096u}){
        PipeWireReceiptQueueOptions o;o.slots=slots;o.memoryBudgetBytes=64*1024*1024;
        const auto bytes=pipeWireReceiptPayloadBytes(o);ResourceLedger ledger(bytes);o.resources=ledger;
        {PreparedPipeWireReceiptQueues q(o);check(ledger.usage().reservedBytes==bytes,"Rounded queue arrays not fully charged");
            for(unsigned n=0;n<slots;++n)check(rt([&]{return q.publish(capture(n));})==PipeWireReceiptCode::Accepted,"Admitted large lane rejected prefix");
            check(rt([&]{return q.publish(capture(slots));})==PipeWireReceiptCode::Fault && q.captureFault()->cause==PipeWireReceiptCause::QueueFull && q.captureFault()->attempted->sequence==slots,"Rounded queue cells exceeded requested capacity");
            for(unsigned n=0;n<slots;++n){PipeWireReceiptRecord<PipeWireCaptureCycle> c;check(rt([&]{return q.read(c);})==PipeWireReceiptCode::Ready && c.sequence==n && c.original.original==clock(n),"Large faulted prefix lost/order changed");}}
        check(!ledger.usage().reservedBytes,"Large queue credit leaked");o.resources.reset();o.memoryBudgetBytes=bytes-1;refuses([&]{PreparedPipeWireReceiptQueues q(o);});
    }
    refuses([]{PipeWireReceiptQueueOptions o;o.slots=4097;PreparedPipeWireReceiptQueues q(o);});
}
}
int main(){try{rt_audit::reset();extendedCapacity();transport();pressure();captureReturns();owners();finalPublication();const auto a=rt_audit::counts;
    check(!a.cppAllocate && !a.cppFree && !a.cAllocate && !a.cFree && !a.blockingLock,"Prepared capture/receipt operations allocated/freed/locked");
    std::cout<<checks<<" native PipeWire receipt checks passed; exact returns, prefix retention, bounded pressure, sequence, distinct native producers/one consumer; callback allocate/free/lock=0; persistence=false\n";return 0;
}catch(const std::exception &e){std::cerr<<"Check "<<checks<<": "<<e.what()<<'\n';return 1;}}
