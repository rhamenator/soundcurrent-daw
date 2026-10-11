// SPDX-License-Identifier: GPL-3.0-only
#include <soundcurrent/monitor_processing.hpp>
#include "rt_audit.hpp"
#include <array>
#include <bit>
#include <chrono>
#include <iostream>
#include <thread>
using namespace soundcurrent::daw;
namespace {
unsigned checks = 0;
void check(bool ok,const char *why) { ++checks; if (!ok) throw std::runtime_error(why); }
template<class F> auto rt(F f) { rt_audit::Guard g; return f(); }
template<class F> void refuses(F f) {
    try { f(); } catch (const ProjectError &) { check(true,"Refused"); return; }
    check(false,"Bad admission accepted");
}
constexpr std::uint64_t base = std::uint64_t(1)<<54, now = base+40000000;
struct Config {
    CaptureFanoutConfig capture;
    MonitorOutputBankConfig output;
    MonitorProcessingConfig processing;
    MonitorControlConfig control;
    Config() {
        capture.capture.identity = {11,31,7}; capture.capture.channels = 1;
        capture.capture.maximumPacketFrames = 64; capture.capture.slots = 64; capture.monitorSlots = 64;
        output.channels = 1; output.slots = 1; output.maximumPullFrames = 64; output.first.value = base;
        auto &b = processing.buffer; b.channels = 1; b.maximumInputFrames = 64;
        b.inputCapacityFrames = 2048; b.outputCapacityFrames = 64; b.outputFrameBase = base;
        auto &d = processing.drift; d.captureIdentity = capture.capture.identity;
        d.inputCapacityFrames = b.inputCapacityFrames; d.targetQueuedSourceFrames = 1024;
        d.outputFrameBase = base; d.occupancyGainPpmPerFrame = 0;
        control.initialQueueSequence = UINT32_MAX-2;
    }
};
MonitorControlRequest request(std::uint64_t id,unsigned frames=512) {
    return {1,id,MonitorControlKind::Stop,{base+frames}};
}
void seed(PreparedMonitorProcessing &p,const Config &c) {
    for(unsigned n=0;n<3;++n) {
        const auto q=now-40000000+std::uint64_t(n)*20000000;
        const auto pos=(std::uint64_t(1)<<63)+std::uint64_t(n)*96000;
        rt([&]{p.observe(CaptureClockReading{c.processing.drift.captureIdentity,{pos},{q}});
               p.observe(RenderClockReading{c.processing.drift.renderIdentity,{pos},{q},48000});});
    }
}
void submit(PreparedMonitorControl &q,const MonitorControlRequest &r) {
    check(rt([&]{return q.submit(r);})==MonitorControlCode::Accepted,"Intent admission");
}
MonitorControlReceipt receipt(PreparedMonitorControl &q,const MonitorControlRequest &r,MonitorControlResult result) {
    MonitorControlReceipt out;
    check(rt([&]{return q.receive(out);})==MonitorControlCode::Ready && out.original==r && out.result==result,
          "Original intent/result receipt changed or missing"); return out;
}
void audit(const rt_audit::Counts &a) {
    check(!a.cppAllocate && !a.cppFree && !a.cAllocate && !a.cFree && !a.blockingLock,"RT allocation/free/blocking lock");
}
void admission() {
    Config c;ResourceLedger ledger(1024*1024);c.control.resources=ledger;
    const auto charge=monitorControlPayloadBytes(c.control);
    {
        PreparedMonitorControl q(c.control);check(q.chargedBytes()==charge && ledger.usage().reservedBytes==charge,"Queue charge");
        auto wrong=c.processing;++wrong.buffer.runGeneration;
        PreparedCaptureFanout f(c.capture);PreparedMonitorOutputBank o(c.output);
        const auto before=ledger.usage();refuses([&]{PreparedMonitorProcessing p(f,o,wrong,nullptr,&q);});
        check(ledger.usage()==before && !q.counts().accepted,"Mismatched generation admission mutated owners");
        q.finish();q.settleAfterJoin();
        refuses([&]{PreparedMonitorProcessing p(f,o,c.processing,nullptr,&q);});
    }
    check(!ledger.usage().reservedBytes,"Joined control credit leaked");
    for(unsigned k=0;k<4;++k) {
        auto bad=c.control;
        if(k==0)bad.slots=0;else if(k==1)bad.slots=65;else if(k==2)bad.runGeneration=0;else bad.memoryBudgetBytes=charge-1;
        const auto before=ledger.usage();refuses([&]{PreparedMonitorControl q(bad);});check(ledger.usage()==before,"Failed admission charged credit");
    }
    auto tiny=c.control;tiny.resources=ResourceLedger(charge-1);refuses([&]{PreparedMonitorControl q(tiny);});
}
void orderedRefusals() {
    Config c;PreparedMonitorControl q(c.control);PreparedCaptureFanout f(c.capture);PreparedMonitorOutputBank o(c.output);
    PreparedMonitorProcessing p(f,o,c.processing,nullptr,&q);
    std::array<MonitorControlRequest,9> requests{request(1),request(0),request(1),request(2),request(3),request(4),request(5),request(UINT64_MAX),request(6)};
    requests[0].runGeneration=2;requests[4].kind=static_cast<MonitorControlKind>(91);
    requests[5].exclusiveEnd.value=base+513;requests[6].exclusiveEnd.value=base-1;
    const std::array results{MonitorControlResult::Generation,MonitorControlResult::Sequence,MonitorControlResult::Planned,
        MonitorControlResult::Planned,MonitorControlResult::Kind,MonitorControlResult::EndRefused,MonitorControlResult::EndRefused,
        MonitorControlResult::Planned,MonitorControlResult::Sequence};
    for(unsigned n=0;n<requests.size();++n) {
        const auto before=p.statistics();submit(q,requests[n]);
        check(rt([&]{return p.pumpControl();})==MonitorControlCode::Handled,"Bounded handling");
        const auto r=receipt(q,requests[n],results[n]);
        check(r.generatedEnd==before.buffer.nextOutputFrame && r.publishedFrames==before.publishedFrames,"Original before-action snapshot");
        check(p.statistics().serviceCalls==before.serviceCalls && !p.statistics().buffer.generatedOutputFrames,"Intent handling advanced kernel");
    }
    q.finish();check(rt([&]{return q.submit(request(7));})==MonitorControlCode::Stopped,"Closed producer accepted");
    check(q.counts().accepted==9 && q.counts().replied==9 && q.counts().received==9,"Receipt accounting");
    p.cancel();p.settleAfterJoin();f.finish();f.settleAfterJoin();o.settleAfterJoin();q.settleAfterJoin();
    MonitorControlReceipt r;check(q.receive(r)==MonitorControlCode::Drained && !q.failed() && q.abandonedAfterJoin().empty(),"Clean drain/retirement");
}
void pressureAndSettlement() {
    for(unsigned kind=0;kind<6;++kind) {
        Config c;c.control.slots=1;PreparedMonitorControl q(c.control);PreparedCaptureFanout f(c.capture);PreparedMonitorOutputBank o(c.output);
        PreparedMonitorProcessing p(f,o,c.processing,nullptr,&q);
        if(kind==4)seed(p,c);
        const auto first=request(1);submit(q,first);
        check(q.submit(request(2))==MonitorControlCode::Backpressure && q.counts().accepted==1,"Queue full silently accepted intent");
        check(rt([&]{return p.step({now});})==MonitorProcessingCode::Progress,"First intent step");
        auto second=request(2,513);if(kind==1)second.kind=MonitorControlKind::Cancel;
        submit(q,second);const auto before=p.statistics();
        for(unsigned n=0;n<16;++n)check(rt([&]{return p.step({now});})==MonitorProcessingCode::ControlFull &&
            p.statistics().buffer==before.buffer && p.statistics().serviceCalls==before.serviceCalls && !p.statistics().cancelled,
            "Receipt pressure consumed intent or advanced PCM");
        if(kind<2) {
            receipt(q,first,MonitorControlResult::Planned);
            check(rt([&]{return p.step({now});})==(kind==1?MonitorProcessingCode::Cancelled:MonitorProcessingCode::Progress),"Receipt credit did not release intent");
            receipt(q,second,kind==1?MonitorControlResult::Cancelled:MonitorControlResult::EndRefused);
            q.finish();
        } else if(kind<4) {
            if(kind==2)rt([&]{q.controlFailed();});else rt([&]{q.processingFailed();});
            check(rt([&]{return p.step({now});})==MonitorProcessingCode::Fault && p.firstFault()->cause==MonitorProcessingCause::Control,"Owner loss hidden");
            q.finish();
        } else {
            if(kind==5)rt([&]{f.monitorConsumerFailed();});
            const auto time=kind==4?now+c.processing.drift.maximumAge100ns+1:now;
            check(rt([&]{return p.step({time});})==MonitorProcessingCode::Fault &&
                p.firstFault()->cause==(kind==4?MonitorProcessingCause::Clock:MonitorProcessingCause::CaptureRoute),
                "Receipt pressure hid clock age or route loss");
            receipt(q,first,MonitorControlResult::Planned);
            check(rt([&]{return p.pumpControl();})==MonitorControlCode::Handled,"Faulted owner lost terminal intent");
            receipt(q,second,MonitorControlResult::Terminal);q.finish();
        }
        p.cancel();p.settleAfterJoin();f.finish();f.settleAfterJoin();o.settleAfterJoin();q.settleAfterJoin();q.settleAfterJoin();
        if(kind==2 || kind==3) {
            receipt(q,first,MonitorControlResult::Planned);
            const auto abandoned=q.abandonedAfterJoin();
            check(abandoned.size()==1 && abandoned[0].original==second && abandoned[0].result==MonitorControlResult::Abandoned,"Lost accepted intent diagnostic");
        }
        check(q.counts().accepted==q.counts().replied+q.counts().abandoned,"Joined accepted intent disposition mismatch");
    }
    // Maximum unhandled prefix and missing producer finish; bounded joined drain.
    Config c;PreparedMonitorControl q(c.control);
    for(unsigned n=0;n<64;++n)submit(q,request(n+1));
    q.settleAfterJoin();check(q.failed() && q.abandonedAfterJoin().size()==64,"Abandoned producer prefix");
    for(unsigned n=0;n<64;++n)check(q.abandonedAfterJoin()[n].original==request(n+1),"Joined abandoned order");
}
void finiteOutput(unsigned length,bool dynamic=false) {
    Config c;PreparedMonitorControl q(c.control);PreparedCaptureFanout f(c.capture);PreparedMonitorOutputBank o(c.output);
    PreparedMonitorProcessing p(f,o,c.processing,nullptr,&q);seed(p,c);
    std::array<float,1024> pcm{};for(unsigned n=0;n<pcm.size();++n)pcm[n]=float(n%47)/13.f-1.5f;
    for(unsigned n=0;n<1024;n+=64) {
        const CaptureBankStamp stamp{c.capture.capture.identity,{(std::uint64_t(1)<<63)+n},{now+n*211},64,0};
        const auto r=rt([&]{return f.publish({stamp,reinterpret_cast<const std::byte *>(pcm.data()+n),64*sizeof(float)});});
        check(r.rawAcceptedFrames==64 && r.monitorAcceptedFrames==64,"Original PCM publication");
    }
    const auto stop=request(dynamic?2:1,length);
    bool planned=!dynamic;
    if(!dynamic) {
        submit(q,stop);f.finish();
        check(rt([&]{return p.step({now});})==MonitorProcessingCode::Progress,"Stop owner application");
        receipt(q,stop,MonitorControlResult::Planned);
    }
    std::vector<float> rendered;rendered.reserve(length);std::array<float,64> scratch{};
    unsigned pressure=0;
    for(unsigned n=0;n<20000 && !p.statistics().done;++n) {
        if(dynamic && !planned && p.statistics().buffer.generatedOutputFrames>=64) {
            const auto before=p.statistics();const auto late=request(1,63);submit(q,late);
            check(rt([&]{return p.step({now});})==MonitorProcessingCode::Progress,"Late request delivery");
            const auto refused=receipt(q,late,MonitorControlResult::EndRefused);
            check(refused.generatedEnd==before.buffer.nextOutputFrame && p.statistics().buffer==before.buffer &&
                p.statistics().pendingOutputFrames==before.pendingOutputFrames,"Late receipt mutated generated/pending PCM");
            submit(q,stop);check(rt([&]{return p.step({now});})==MonitorProcessingCode::Progress,"Dynamic owner Stop");
            receipt(q,stop,MonitorControlResult::Planned);planned=true;f.finish();
        }
        const auto code=rt([&]{return p.step({now});});
        check(code!=MonitorProcessingCode::Fault && code!=MonitorProcessingCode::NeedEnd,"Finite intent failed");
        if(code==MonitorProcessingCode::OutputFull)++pressure;
        // Exercise retained PCM under output pressure before allowing pull.
        if(n%7==0 || p.statistics().done) {
            const auto r=rt([&]{return o.pull(scratch);});rendered.insert(rendered.end(),scratch.begin(),scratch.begin()+r.frames);
        }
        check(p.statistics().buffer.generatedOutputFrames<=length,"Control end overshoot");
    }
    while(!o.drained()) { const auto r=rt([&]{return o.pull(scratch);});check(r.frames>0,"Output drain");rendered.insert(rendered.end(),scratch.begin(),scratch.begin()+r.frames); }
    check(p.statistics().done && rendered.size()==length && p.statistics().buffer.acceptedInputFrames==1024,"Exact Stop output/postroll");
    if(length>=512)check(pressure>0,"Output pressure not exercised");
    PreparedMonitorBuffer reference(c.processing.buffer);
    for(unsigned n=0;n<1024;n+=64)check(reference.push(std::span<const float>(pcm).subspan(n,64))==MonitorBufferCode::Accepted,"Reference input");
    check(reference.finishAt(base+length)==MonitorBufferCode::Accepted,"Reference end");
    std::size_t compared=0;
    for(unsigned n=0;n<20000 && compared<rendered.size();++n) {
        check(reference.service()!=MonitorBufferCode::Fault,"Reference kernel");const auto r=reference.pull(scratch);
        for(unsigned j=0;j<r.frames;++j)check(compared<rendered.size() && std::bit_cast<std::uint32_t>(rendered[compared++])==std::bit_cast<std::uint32_t>(scratch[j]),"Stop PCM differs from independent buffer");
    }
    check(compared==length,"Reference extent");
    std::size_t raw=0;
    for(;;) { CaptureBankView v;const auto r=rt([&]{return f.acquireRaw(v);});if(r==CaptureBankRead::Drained)break;
        check(r==CaptureBankRead::Ready,"Raw packet missing");for(float x:v.interleaved)check(std::bit_cast<std::uint32_t>(x)==std::bit_cast<std::uint32_t>(pcm[raw++]),"Stop modified raw take");
        check(rt([&]{return f.releaseRaw(v);}),"Raw release"); }
    check(raw==1024,"Raw take extent");
    const auto late=request(3,0);submit(q,late);check(rt([&]{return p.step({now});})==MonitorProcessingCode::Done,"Terminal intent revived processing");
    receipt(q,late,MonitorControlResult::Terminal);q.finish();p.settleAfterJoin();f.settleAfterJoin();o.settleAfterJoin();q.settleAfterJoin();
}
void concurrentOwners() {
    Config c;c.control.slots=7;PreparedMonitorControl q(c.control);PreparedCaptureFanout f(c.capture);PreparedMonitorOutputBank o(c.output);
    PreparedMonitorProcessing p(f,o,c.processing,nullptr,&q);
    constexpr unsigned staleCount=20000,total=staleCount+2;std::atomic<bool> failed{false},closed{false};
    const auto intent=[&](unsigned n) {
        auto r=request(n<staleCount?n+1:n-staleCount+1);
        if(n<staleCount)r.runGeneration=2;
        if(n==staleCount+1)r.kind=MonitorControlKind::Cancel;
        return r;
    };
    std::array<rt_audit::Counts,2> audits{};
    const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(10);
    const auto expired=[&]{return failed.load() || std::chrono::steady_clock::now()>=deadline;};
    std::thread control([&]{
        rt_audit::reset();unsigned sent=0,received=0;
        while(received<total && !expired()) {
            if(sent<total) { const auto r=intent(sent);
                const auto code=rt([&]{return q.submit(r);});if(code==MonitorControlCode::Accepted)++sent;else if(code!=MonitorControlCode::Backpressure)failed.store(true); }
            MonitorControlReceipt r;const auto code=rt([&]{return q.receive(r);});
            if(code==MonitorControlCode::Ready) { const auto expected=intent(received);
                const auto result=received<staleCount?MonitorControlResult::Generation:received==staleCount?MonitorControlResult::Planned:MonitorControlResult::Cancelled;
                if(r.original!=expected || r.result!=result || r.generatedEnd!=base || r.publishedFrames)failed.store(true);
                ++received; }
            else if(code!=MonitorControlCode::Empty)failed.store(true);
            std::this_thread::yield();
        }
        if(received!=total || sent!=total)failed.store(true);
        rt([&]{q.finish();});closed.store(true,std::memory_order_release);audits[0]=rt_audit::counts;
    });
    std::thread processing([&]{
        rt_audit::reset();
        while(!closed.load(std::memory_order_acquire) && !expired()) {
            const auto code=rt([&]{return p.pumpControl();});
            if(code!=MonitorControlCode::Handled && code!=MonitorControlCode::Empty && code!=MonitorControlCode::ReceiptFull)failed.store(true);
            std::this_thread::yield();
        }
        audits[1]=rt_audit::counts;
    });
    control.join();processing.join();q.settleAfterJoin();
    check(!failed.load() && q.counts().accepted==total && q.counts().received==total && q.counts().replied==total && !q.counts().abandoned,"Concurrent intent/receipt prefix");
    check(!p.statistics().buffer.generatedOutputFrames && p.statistics().buffer.plannedOutputEnd==base+512 && p.statistics().cancelled,
        "Concurrent accepted Stop/Cancel result mismatch");
    for(const auto &a:audits)audit(a);
    p.cancel();p.settleAfterJoin();f.finish();f.settleAfterJoin();o.settleAfterJoin();
    std::cout<<"two concurrent owners; original generation-refused requests/receipts=20000 plus accepted Stop/Cancel; queue counters wrap; yield/deadline outside guarded methods\n";
}
} // namespace
int main() {
    try {
        rt_audit::reset();admission();orderedRefusals();pressureAndSettlement();
        finiteOutput(0);finiteOutput(7);finiteOutput(512);finiteOutput(512,true);concurrentOwners();audit(rt_audit::counts);
        std::cout<<checks<<" monitor Stop handoff checks passed; guarded allocate/free/lock=0; no native Stop/alignment claim\n";return 0;
    } catch(const std::exception &e) { rt_audit::active=false;std::cerr<<"Check "<<checks<<": "<<e.what()<<'\n';return 1; }
}
