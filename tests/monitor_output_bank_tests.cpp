// SPDX-License-Identifier: GPL-3.0-only
#include <soundcurrent/monitor_output_bank.hpp>
#include "rt_audit.hpp"
#include <algorithm>
#include <array>
#include <bit>
#include <chrono>
#include <iostream>
#include <limits>
#include <thread>

using namespace soundcurrent::daw;
namespace {
using C = MonitorOutputCode;
using P = MonitorOutputPhase;
unsigned checks = 0;
void check(bool ok, const char *message) { ++checks; if (!ok) throw std::runtime_error(message); }
template<class F> auto rt(F f) { rt_audit::Guard guard; return f(); }
bool audited(const rt_audit::Counts &c) {
    return !c.cppAllocate && !c.cppFree && !c.cAllocate && !c.cFree && !c.blockingLock;
}
template<class F> void refuses(F f) {
    try { f(); } catch (const ProjectError &) { check(true, "refused"); return; }
    check(false, "Invalid admission accepted");
}
MonitorOutputBankConfig config(std::uint32_t channels = 3, std::uint32_t slots = 4) {
    MonitorOutputBankConfig c; c.channels = channels; c.slots = slots; c.maximumBlockFrames = 31;
    c.maximumPullFrames = 97; c.first.value = (std::uint64_t{1} << 63) + 71;
    c.initialQueueSequence = UINT32_MAX - 2; return c;
}
float sample(std::uint64_t frame, unsigned channel) {
    if ((frame + channel) % 29 == 0) return std::bit_cast<float>(std::uint32_t{0x80000000});
    const auto bits = std::uint32_t(0x41000000u | ((frame * 71 + channel * 101) & 0x7fffffu));
    return std::bit_cast<float>(bits | ((frame & 1) ? 0x80000000u : 0u)); // >1 headroom.
}
void admissionAndPartial() {
    auto c = config(); const auto bytes = monitorOutputBankPayloadBytes(c); ResourceLedger ledger(bytes);
    c.resources = ledger; const auto before = ledger.usage();
    {
        PreparedMonitorOutputBank b(c); check(b.chargedBytes() == bytes && ledger.usage().reservedBytes == bytes, "Whole credit");
        const auto admitted = ledger.usage();
        refuses([&] { PreparedMonitorOutputBank duplicate(c); });
        check(ledger.usage() == admitted, "Failed shared credit changed usage");
        std::array<float,93> input{};
        for (unsigned n = 0; n < input.size(); ++n) input[n] = sample(n / 3, n % 3);
        const auto original = input; const MonitorOutputStamp p{c.generation,c.first,31};
        check(rt([&] { return b.publish(p,input); }) == C::Accepted, "Whole copied block");
        input.fill(999.f);
        std::array<float,291> out; out.fill(777.f);
        auto r = rt([&] { return b.pull(std::span(out).first(7 * 3)); });
        check(r.code == C::Ready && r.phase == P::Running && r.first == c.first && r.frames == 7, "Partial first ordinal");
        for (unsigned n = 0; n < 21; ++n) check(std::bit_cast<std::uint32_t>(out[n]) == std::bit_cast<std::uint32_t>(original[n]), "Partial bits");
        for (unsigned n = 21; n < out.size(); ++n) check(out[n] == 777.f, "Partial suffix changed");
        out.fill(777.f);
        check(rt([&] { return b.pull(std::span(out).first(1)); }).code == C::InvalidPull && out[0] == 777.f, "Non-frame destination");
        std::array<float,294> oversized; oversized.fill(777.f);
        check(rt([&] { return b.pull(oversized); }).code == C::InvalidPull && oversized[0] == 777.f, "Unbounded pull accepted");
        check(rt([&] { return b.pull({}); }).code == C::InvalidPull, "Zero-sized pull accepted");
        r = rt([&] { return b.pull(out); });
        check(r.code == C::Ready && r.frames == 24 && r.first.value == c.first.value + 7, "Remaining exact prefix");
        for (unsigned n = 0; n < 72; ++n) check(std::bit_cast<std::uint32_t>(out[n]) == std::bit_cast<std::uint32_t>(original[n + 21]), "Remaining bits");
        for (unsigned n = 72; n < out.size(); ++n) check(out[n] == 777.f, "Unavailable tail synthesized");
        out.fill(777.f); r = rt([&] { return b.pull(out); });
        check(r.code == C::Empty && !r.frames && out[0] == 777.f && !b.consumerFault(), "Empty invented underflow/silence");
        rt([&] { b.finish(); });
        r = rt([&] { return b.pull(out); });
        check(r.code == C::Drained && r.phase == P::Stopped && b.drained(), "Exact end");
        check(b.acceptedFrames() == 31 && b.deliveredFrames() == 31 && !b.discardedFrames(), "Partial totals");
        b.settleAfterJoin(); b.settleAfterJoin(); check(ledger.usage() == admitted, "Premature retirement");
    }
    check(ledger.usage().reservedBytes == before.reservedBytes && ledger.usage().owners == before.owners, "Joined credit leak");
    for (unsigned kind = 0; kind < 9; ++kind) {
        auto bad = config();
        switch (kind) {
        case 0:bad.generation=0;break; case 1:bad.channels=257;break;case 2:bad.channels=0;break;
        case 3:bad.sampleRate=7999;break;case 4:bad.sampleRate=384001;break;
        case 5:bad.maximumBlockFrames=0;break;case 6:bad.maximumPullFrames=65537;break;
        case 7:bad.slots=257;break;case 8:bad.memoryBudgetBytes=1;break;
        }
        refuses([&] { PreparedMonitorOutputBank unused(bad); });
    }
    auto tight = config(); tight.memoryBudgetBytes = bytes - 1;
    refuses([&] { PreparedMonitorOutputBank unused(tight); });
}
void backpressureAndFaults() {
    auto c = config(2,2); std::array<float,62> input{}; input.fill(3.25f);
    PreparedMonitorOutputBank b(c);
    auto p = MonitorOutputStamp{c.generation,c.first,31};
    for (unsigned n=0;n<2;++n) { check(rt([&] { return b.publish(p,input); })==C::Accepted,"Fill slots");p.first.value+=31; }
    const auto accepted = b.acceptedFrames();
    for (unsigned n=0;n<100;++n) check(rt([&] { return b.publish(p,input); })==C::Backpressure,"Stable refusal");
    check(b.acceptedFrames()==accepted && !b.producerFault(),"Backpressure changed cursor/fault");
    std::array<float,194> out{};
    auto r=rt([&] { return b.pull(out); }); check(r.frames==62,"Snapshot drain");
    check(rt([&] { return b.publish(p,input); })==C::Accepted,"Exact retained-block retry");
    rt([&] { b.requestStop(); }); check(rt([&] { return b.publish(p,input); })==C::Stopped,"Stop accepted new block");
    rt([&] { b.finish(); }); r=rt([&] { return b.pull(out); });
    check(r.frames==31 && r.phase==P::Stopped && b.drained(),"Stop lost accepted prefix");
    for (unsigned kind=0;kind<7;++kind) {
        PreparedMonitorOutputBank f(c); auto attempted=MonitorOutputStamp{c.generation,c.first,31};
        check(rt([&] { return f.publish(attempted,input); })==C::Accepted,"Fault prefix");
        attempted.first.value+=31; auto bad=input; std::span<const float> data=bad;
        MonitorOutputProducerCause expected=MonitorOutputProducerCause::Size;
        switch(kind) {
        case 0:++attempted.generation;expected=MonitorOutputProducerCause::Generation;break;
        case 1:--attempted.first.value;expected=MonitorOutputProducerCause::Ordinal;break;
        case 2:attempted.frames=0;break;case 3:attempted.frames=32;break;
        case 4:data=data.first(61);break;
        case 5:bad[61]=std::numeric_limits<float>::infinity();expected=MonitorOutputProducerCause::Nonfinite;break;
        case 6:bad[0]=std::numeric_limits<float>::quiet_NaN();expected=MonitorOutputProducerCause::Nonfinite;break;
        }
        check(rt([&] { return f.publish(attempted,data); })==C::Fault && f.acceptedFrames()==31,"Invalid whole block accepted");
        const auto fault=f.producerFault(); check(fault && fault->cause==expected && fault->attempted==attempted,"Original producer fault");
        rt([&] { f.processingFailed(); f.finish(); }); check(f.producerFault()==fault,"Producer fault overwritten");
        r=rt([&] { return f.pull(out); }); check(r.code==C::Ready && r.frames==31 && r.phase==P::Fault,"Fault prefix or receipt lost");
        check(rt([&] { return f.pull(out); }).code==C::Drained,"Fault end not explicit");
    }
    auto overflow=config(2); overflow.first.value=UINT64_MAX-30; PreparedMonitorOutputBank f(overflow);
    p={overflow.generation,overflow.first,31};
    check(rt([&] { return f.publish(p,input); })==C::Fault && f.producerFault()->cause==MonitorOutputProducerCause::CounterOverflow,"Ordinal wrap");
    for (unsigned kind=0;kind<4;++kind) {
        PreparedMonitorOutputBank loss(c);p={c.generation,c.first,31};
        check(rt([&] { return loss.publish(p,input); })==C::Accepted,"Cancel/loss prefix");
        r=rt([&] { return loss.pull(std::span(out).first(14)); });check(r.frames==7,"Cancel partial lease");
        if(kind==0)rt([&] { loss.requestCancel(); });
        if(kind==1)rt([&] { loss.signalUnderflow(97); });
        if(kind==2)rt([&] { loss.renderFailed(); });
        if(kind==3) { rt([&] { loss.finish(); loss.renderFailed(); loss.processingFailed(); }); }
        const auto renderFault=loss.consumerFault();const auto processingFault=loss.producerFault();
        out.fill(777.f);r=rt([&] { return loss.pull(out); });
        check(!r.frames && (r.code==(kind==0?C::Cancelled:C::Fault)) && out[0]==777.f,"Cancel/loss wrote stale audio");
        if(kind==1)check(renderFault->next.value==c.first.value+7 && renderFault->requestedFrames==97,"Actual consumer cursor/request");
        rt([&] { loss.finish(); });loss.settleAfterJoin();loss.settleAfterJoin();
        check(loss.drained() && loss.deliveredFrames()==7 && loss.discardedFrames()==24,"Joined partial backlog disposition");
        check(loss.consumerFault()==renderFault && loss.producerFault()==processingFault,"Independent owner faults lost");
    }
    // Cancellation after producer completion still cancels remaining monitor PCM.
    PreparedMonitorOutputBank late(c);p={c.generation,c.first,31};late.publish(p,input);late.finish();late.requestCancel();
    check(rt([&] { return late.pull(out); }).code==C::Cancelled,"Late monitor cancel");late.settleAfterJoin();check(late.discardedFrames()==31,"Late queued cancellation");
    PreparedMonitorOutputBank abandoned(c);p={c.generation,c.first,31};abandoned.publish(p,input);
    abandoned.settleAfterJoin();abandoned.settleAfterJoin();
    check(abandoned.producerDone() && abandoned.producerFault()->cause==MonitorOutputProducerCause::ProcessingFailed &&
          !abandoned.producerFault()->attempted,"Joined abandonment fabricated success/packet");
    r=rt([&] { return abandoned.pull(out); });check(r.frames==31 && r.phase==P::Fault && abandoned.drained(),"Abandoned producer lost diagnostic prefix");
    auto end=config(1);end.first.value=UINT64_MAX-1;PreparedMonitorOutputBank last(end);std::array<float,1> one{3.25f};
    p={end.generation,end.first,1};check(rt([&] { return last.publish(p,one); })==C::Accepted,"Last integer ordinal refused");
    last.finish();r=rt([&] { return last.pull(one); });check(r.frames==1 && r.first==end.first && last.drained(),"Last ordinal rounded/wrapped");
}
void concurrent(std::uint32_t channels, unsigned packets) {
    auto c=config(channels,8); c.maximumPullFrames=17; PreparedMonitorOutputBank b(c);
    std::vector<float> source(std::size_t(channels)*31), output(std::size_t(channels)*17);
    std::atomic<bool> bad{false};std::uint64_t readFrames=0, bitChecks=0;
    rt_audit::Counts consumerAudit;
    const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(10);
    std::thread render([&] {
        rt_audit::reset();
        for (;;) {
            const auto r=rt([&] { return b.pull(output); });
            if(r.code==C::Drained)break;
            if(r.code==C::Empty) {
                if(std::chrono::steady_clock::now()>=deadline){bad.store(true);break;}
                std::this_thread::yield();continue;
            }
            if(r.code!=C::Ready || r.phase==P::Fault || r.first.value!=c.first.value+readFrames){bad.store(true);break;}
            for(unsigned frame=0;frame<r.frames;++frame)for(unsigned ch=0;ch<channels;++ch) {
                if(std::bit_cast<std::uint32_t>(output[std::size_t(frame)*channels+ch])!=std::bit_cast<std::uint32_t>(sample(readFrames+frame,ch)))bad.store(true);
                ++bitChecks;
            }
            readFrames+=r.frames;
            if((readFrames&31)==0)std::this_thread::yield();
        }
        consumerAudit=rt_audit::counts;
    });
    std::uint64_t written=0;unsigned pressure=0;
    for(unsigned n=0;n<packets && !bad.load();++n) {
        const auto frames=1+n%31;
        for(unsigned frame=0;frame<frames;++frame)for(unsigned ch=0;ch<channels;++ch)source[std::size_t(frame)*channels+ch]=sample(written+frame,ch);
        const MonitorOutputStamp p{c.generation,{c.first.value+written},frames};
        for(;;) {
            auto r=rt([&] { return b.publish(p,std::span(source).first(std::size_t(frames)*channels)); });
            if(r==C::Accepted){written+=frames;break;}
            if(r!=C::Backpressure || bad.load() || std::chrono::steady_clock::now()>=deadline){bad.store(true);break;}
            ++pressure;std::this_thread::yield(); // Synthetic producer; never wait in prepared method/native callback.
        }
        std::fill(source.begin(),source.end(),999.f);
    }
    rt([&] { b.finish(); });render.join();b.settleAfterJoin();
    check(!bad.load() && b.drained() && readFrames==written && b.acceptedFrames()==written && b.deliveredFrames()==written,"Concurrent exact output extent");
    check(!b.producerFault() && !b.consumerFault() && !b.discardedFrames() && audited(consumerAudit),"Concurrent fault/audit");
    std::cout<<"concurrent channels="<<channels<<" packets="<<packets<<" frames="<<written<<" floatBitComparisons="<<bitChecks<<" observedBackpressure="<<pressure<<'\n';
}
} // namespace
int main() {
    try {
        rt_audit::reset();admissionAndPartial();backpressureAndFaults();concurrent(3,20000);concurrent(256,1024);
        check(audited(rt_audit::counts),"Prepared producer/consumer/refusal RT audit");
        std::cout<<checks<<" monitor output bank checks passed cppAllocations=0 cppFrees=0";
#ifdef SC_WRAP_LIBC
        std::cout<<" libcInterposition=enabled cAllocations=0 cFrees=0 blockingLocks=0\n";
#else
        std::cout<<" libcInterposition=disabled (C allocations/locks not intercepted)\n";
#endif
        return 0;
    }catch(const std::exception &e){rt_audit::active=false;std::cerr<<"Check "<<checks<<": "<<e.what()<<'\n';return 1;}
}
