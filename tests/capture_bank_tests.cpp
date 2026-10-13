// SPDX-License-Identifier: GPL-3.0-only
#include <soundcurrent/capture_bank.hpp>
#include <soundcurrent/wasapi_packet_copy.hpp>
#include "rt_audit.hpp"
#include <algorithm>
#include <array>
#include <cstring>
#include <iostream>
#include <thread>
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <audioclient.h>
static_assert(soundcurrent::daw::bankSilent==AUDCLNT_BUFFERFLAGS_SILENT);
static_assert(soundcurrent::daw::bankDiscontinuity==AUDCLNT_BUFFERFLAGS_DATA_DISCONTINUITY);
static_assert(soundcurrent::daw::bankTimestampError==AUDCLNT_BUFFERFLAGS_TIMESTAMP_ERROR);
#endif
using namespace soundcurrent::daw;
namespace {
using S=CaptureBankStatus;
constexpr ClockStreamIdentity identity{11,31,7};
unsigned checks=0;
void require(bool ok,const char *message) { ++checks; if (!ok) throw std::runtime_error(message); }
void requireAudit() {
    require(!rt_audit::counts.cppAllocate && !rt_audit::counts.cppFree,"failure-path C++ RT audit");
#ifdef SC_WRAP_LIBC
    require(!rt_audit::counts.cAllocate && !rt_audit::counts.cFree && !rt_audit::counts.blockingLock,"failure-path libc/pthread RT audit");
#endif
}
CaptureBankConfig config(std::uint32_t channels=3,std::uint32_t slots=4) {
    CaptureBankConfig c; c.identity=identity; c.channels=channels; c.slots=slots;
    c.maximumPacketFrames=31; return c;
}
CaptureBankInput packet(std::span<const float> samples,std::uint32_t frames,
                        std::uint64_t position=0,std::uint64_t qpc=10000000,std::uint32_t flags=0) {
    return {{identity,{position},{qpc},frames,flags},reinterpret_cast<const std::byte *>(samples.data()),samples.size_bytes()};
}
template<class F> void refuses(F fn) {
    try { fn(); } catch(const ProjectError &) { require(true,"admission rejected"); return; }
    require(false,"admission should reject");
}
void fidelityAndLeases() {
    auto c=config(); c.initialQueueSequence=UINT32_MAX-2;
    PreparedCaptureBank bank(c);
    std::array<float,93> data{};
    for (unsigned n=0;n<data.size();++n) data[n]=float(n)-47.25f;
    std::array<float,93> original=data;
    const std::array<std::uint32_t,4> exceptional{0x7fc01234,0xff800000,0x80000000,0x7f800000};
    std::memcpy(data.data(),exceptional.data(),sizeof(exceptional)); original=data;
    const auto first=packet(data,31,123456789,10000000,bankDiscontinuity);
    require(bank.publish(first).acceptedFrames==31,"whole packet admitted");
    std::fill(data.begin(),data.end(),999.f); // Simulate callback scratch/SDK reuse.
    CaptureBankView view;
    require(bank.acquire(view)==CaptureBankRead::Ready,"owned view acquired");
    require(view.token.original==first.stamp && view.token.sequence==0,"original metadata preserved");
    require(std::memcmp(view.interleaved.data(),original.data(),sizeof(original))==0,"all original float bits/headroom/channel order preserved");
    require(bank.firstPacket()==first.stamp,"original startup discontinuity retained");
    CaptureBankView other;
    require(bank.acquire(other)==CaptureBankRead::Outstanding,"one outstanding lease");
    auto changed=view; ++changed.token.sequence;
    require(!bank.release(changed),"forged sequence rejected");
    changed=view; ++changed.token.original.qpc.value;
    require(!bank.release(changed),"changed metadata rejected");
    changed=view; changed.interleaved=changed.interleaved.subspan(1);
    require(!bank.release(changed),"forged backing rejected");
    PreparedCaptureBank foreign(c);
    require(!foreign.release(view),"cross-bank release refused");
    const auto stale=view;
    require(bank.release(view) && !bank.release(view),"release exactly once");
    require(bank.acquire(view)==CaptureBankRead::Empty && !bank.drained(),"temporary empty is explicit, not done or generated silence");
    // Queue counter wrap is exercised repeatedly, including free-slot return.
    std::uint64_t position=first.stamp.position.value+31;
    for (unsigned n=0;n<1000;++n) {
        require(bank.publish(packet(original,31,position,10100000+std::uint64_t(n)*100000)).acceptedFrames==31,"wrapped queue publish");
        require(bank.acquire(view)==CaptureBankRead::Ready && view.token.sequence==n+1,"wrapped FIFO identity");
        require(!bank.release(stale) && bank.release(view),"stale recycled token refused");
        position+=31;
    }
    bank.finish(); bank.finish();
    require(bank.acquire(view)==CaptureBankRead::Drained && bank.drained(),"idempotent finish after lease release");
    require(bank.acceptedFrames()==31*1001 && bank.rejectedFrames()==0,"full extent counters");
    require(bank.publish(packet(original,31,position,200000000)).status==S::Stopped && bank.rejectedFrames()==31,"no packet after finish");
}
void failuresAndPrefix() {
    std::array<float,93> data{};
    for (const auto expected : {S::IdentityChanged,S::InvalidFrames,S::InvalidData,S::InvalidFlags,
                               S::TimestampUnavailable,S::Discontinuity,S::PositionDiscontinuity,
                               S::QpcReset,S::PositionOverflow}) {
        PreparedCaptureBank bank(config());
        const auto prefix=packet(data,31);
        require(bank.publish(prefix).acceptedFrames==31,"prefix before fault");
        auto p=packet(data,31,31,10100000);
        switch(expected) {
        case S::IdentityChanged:++p.stamp.identity.generation;break;
        case S::InvalidFrames:p.stamp.frames=32;break;
        case S::InvalidData:p.bytes=1;break;
        case S::InvalidFlags:p.stamp.flags=8;break;
        case S::TimestampUnavailable:p.stamp.flags=bankTimestampError;break;
        case S::Discontinuity:p.stamp.flags=bankDiscontinuity;break;
        case S::PositionDiscontinuity:p.stamp.position.value=30;break;
        case S::QpcReset:p.stamp.qpc.value=9999999;break;
        case S::PositionOverflow:p.stamp.position.value=UINT64_MAX-30;break;
        default:break;
        }
        CaptureBankPublish result; rt_audit::reset();
        { rt_audit::Guard guard; result=bank.publish(p); }
        requireAudit();
        require(result.status==expected && !result.acceptedFrames && result.rejectedFrames==p.stamp.frames,"whole faulted packet refused");
        require(bank.firstFault() && bank.firstFault()->status==expected && bank.firstFault()->original==p.stamp,"first original error preserved");
        auto next=packet(data,31,31,10200000);
        require(bank.publish(next).status==expected && bank.firstFault()->original==p.stamp,"fault sticky without overwriting receipt");
        bank.finish(); CaptureBankView v;
        require(bank.acquire(v)==CaptureBankRead::Ready && v.token.original==prefix.stamp,"accepted prefix drainable after fault");
        require(bank.release(v) && bank.acquire(v)==CaptureBankRead::Drained,"fault retires after exact lease return");
        require(bank.acceptedFrames()==31 && bank.rejectedFrames()==p.stamp.frames+31,"loss explicitly counted");
    }
    for (const auto zero : {true,false}) {
        PreparedCaptureBank bank(config()); auto p=packet(data,zero ? 0u : 31u);
        if (!zero) p.stamp.qpc.value=0;
        require(bank.publish(p).status==(zero ? S::InvalidFrames : S::TimestampUnavailable),"zero frames/QPC refusal");
        require(!bank.firstPacket() && bank.firstFault()->original==p.stamp,"invalid first packet never invented as an origin");
    }
    auto c=config(); c.initialPacketSequence=UINT64_MAX-1;
    PreparedCaptureBank overflow(c);
    require(overflow.publish(packet(data,31)).acceptedFrames==31,"last nonsentinel sequence accepted");
    require(overflow.publish(packet(data,31,31,10100000)).status==S::SequenceOverflow,"packet sequence does not wrap to stale identities");
    PreparedCaptureBank full(config(3,2));
    require(full.publish(packet(data,31)).acceptedFrames==31 &&
            full.publish(packet(data,31,31,10100000)).acceptedFrames==31,"all slots admitted");
    auto rejected=packet(data,31,62,10200000);
    require(full.publish(rejected).status==S::QueueFull,"overrun terminal and explicit");
    require(full.firstFault()->original==rejected.stamp,"overrun original timing retained");
    CaptureBankView v;
    for (unsigned n=0;n<2;++n) {
        require(full.acquire(v)==CaptureBankRead::Ready && v.token.sequence==n,"overrun keeps FIFO prefix");
        require(full.release(v),"overrun prefix release");
    }
    require(full.publish(rejected).status==S::QueueFull,"freeing a slot cannot hide a recording overrun");
    full.finish(); require(full.drained(),"overrun retirement");
}
void silentStopAndAdmission() {
    std::array<float,93> data{};
    for (unsigned mode=0;mode<5;++mode) {
        PreparedCaptureBank bank(config());
        auto silent=packet({},31,0,10000000,bankSilent|bankDiscontinuity);
        silent.interleaved=reinterpret_cast<const std::byte *>(1); // Never dereferenced.
        require(bank.publish(silent).acceptedFrames==31,"silent stale pointer not read");
        if (mode==0 || mode==4) bank.requestStop();
        if (mode==1) { bank.requestCancel(); bank.requestStop(); }
        if (mode==2) bank.consumerFailed();
        const auto expected=mode==1 || mode==4 ? S::Cancelled : mode==2 ? S::ConsumerFailed : S::Stopped;
        if (mode!=3) require(bank.publish(packet(data,31,31,10100000)).status==(mode==4 ? S::Stopped : expected),"explicit stop/cancel/consumer refusal");
        if (mode==4) bank.requestCancel(); // Cancel after observed Stop, before finish.
        bank.finish(); CaptureBankView v;
        require(bank.acquire(v)==CaptureBankRead::Ready && v.token.original==silent.stamp,"stop preserves silent metadata");
        require(std::all_of(v.interleaved.begin(),v.interleaved.end(),[](float f){return f==0;}),"flagged silent packet materialized as zero");
        require(!bank.drained() && bank.release(v) && bank.drained(),"held prefix lease prevents retirement");
        require(bank.status()==expected && bank.producerDone(),"explicit final status");
        require((mode==2)==bank.firstFault().has_value(),"control stop/cancel never fabricates a packet fault");
    }
    ResourceLedger ledger(256*1024,"Stamped handoff test");
    for (unsigned n=0;n<8;++n) {
        auto c=config();
        if (n==0) c.identity.stream=0;
        if (n==1) c.channels=257;
        if (n==2) c.slots=4097;
        if (n==3) c.maximumPacketFrames=0;
        if (n==4) c.sampleRate=7999;
        if (n==5) c.memoryBudgetBytes=1;
        if (n==6) c.initialPacketSequence=UINT64_MAX;
        if (n==7) { c.channels=256; c.slots=256; c.maximumPacketFrames=65536; }
        const auto before=ledger.usage();
        refuses([&]{PreparedCaptureBank bad(c,ledger);});
        require(ledger.usage().reservedBytes==before.reservedBytes && ledger.usage().owners==before.owners,"failed admission releases credit");
    }
    {
        PreparedCaptureBank bank(config(),ledger);
        require(ledger.usage().reservedBytes==bank.chargedBytes() && ledger.usage().owners==1,"all prepared samples/object charged");
        const auto before=ledger.usage();
        require(bank.publish(packet(data,31)).acceptedFrames==31,"charged bank accepts without reserve changes");
        bank.finish(); CaptureBankView v; bank.acquire(v); bank.release(v);
        require(ledger.usage()==before,"processing and draining cannot release ledger credit on RT");
    }
    require(!ledger.usage().reservedBytes && !ledger.usage().owners,"joined bank retirement returns credit");
    // Every admitted channel is copied in its original order; the widest current
    // layout must not accidentally pass through a stereo-only scratch buffer.
    auto wide=config(256,2); wide.sampleRate=96000; wide.maximumPacketFrames=1;
    PreparedCaptureBank fullWidth(wide);
    std::array<float,256> channels{};
    for (unsigned n=0;n<channels.size();++n) channels[n]=float(n)-128.25f;
    require(fullWidth.publish(packet(channels,1)).acceptedFrames==1,"256-channel original packet admitted");
    CaptureBankView wideView;
    require(fullWidth.acquire(wideView)==CaptureBankRead::Ready && wideView.interleaved.size()==256 &&
            std::memcmp(wideView.interleaved.data(),channels.data(),sizeof(channels))==0,"widest channel order/headroom copied exactly");
    require(fullWidth.release(wideView),"wide packet released"); fullWidth.finish();
}
struct NativeScratch {
    PreparedCaptureBank &bank;
    std::array<float,93> sdk{};
    bool ordered=true; unsigned releases=0;
    static std::int32_t release(void *ctx,std::uint32_t) noexcept {
        auto &self=*static_cast<NativeScratch *>(ctx);
        ++self.releases; std::fill(self.sdk.begin(),self.sdk.end(),-999.f); return 0;
    }
    static void consume(void *ctx,const WasapiPacket &p) noexcept {
        auto &self=*static_cast<NativeScratch *>(ctx);
        self.ordered &= self.releases!=0;
        const auto result=self.bank.publish({{identity,{p.devicePosition},{p.qpc100ns},p.frames,p.flags},p.data,p.bytes});
        self.ordered &= result.acceptedFrames==p.frames;
    }
};
void releasedScratch() {
    static_assert(bankSilent==wasapiSilent && bankDiscontinuity==wasapiDiscontinuity && bankTimestampError==wasapiTimestampError);
    PreparedCaptureBank bank(config()); PreparedWasapiPacketCopy copy(3,31);
    NativeScratch native{bank};
    for (unsigned n=0;n<native.sdk.size();++n) native.sdk[n]=float(n)+2.25f;
    const auto original=native.sdk;
    rt_audit::reset(); bool ok=false;
    {
        rt_audit::Guard guard;
        const auto r=copy.deliver({reinterpret_cast<const std::byte *>(native.sdk.data()),sizeof(native.sdk),31,bankDiscontinuity,0,10000000},
                                  {&native,NativeScratch::release,NativeScratch::consume});
        ok=r.callbackInvoked && r.releaseAttempted && native.ordered;
    }
    // Reuse BOTH SDK and intermediate copied scratch before bank consumption.
    auto reused=WasapiPacket{reinterpret_cast<const std::byte *>(native.sdk.data()),sizeof(native.sdk),31,0,31,10100000};
    const auto reuse=copy.deliver(reused,{&native,NativeScratch::release,nullptr});
    require(reuse.error==WasapiPacketCopyError::InvalidCallbacks && !reuse.callbackInvoked,"intermediate scratch overwritten without a second bank packet");
    CaptureBankView view; bank.acquire(view);
    require(ok && std::memcmp(view.interleaved.data(),original.data(),sizeof(original))==0,"released SDK/scratch reuse cannot corrupt queued bank");
    require(bank.release(view),"copied native packet release"); bank.finish();
    require(!rt_audit::counts.cppAllocate && !rt_audit::counts.cppFree,"SDK-copy-bank C++ RT audit");
#ifdef SC_WRAP_LIBC
    require(!rt_audit::counts.cAllocate && !rt_audit::counts.cFree && !rt_audit::counts.blockingLock,"SDK-copy-bank libc/pthread RT audit");
#endif
}
struct ThreadAudit { rt_audit::Counts counts; bool valid=true; std::uint64_t packets=0,frames=0; };
void concurrentOwners() {
    constexpr unsigned count=20000;
    auto c=config(3,8); c.initialQueueSequence=UINT32_MAX-5;
    ResourceLedger ledger(256*1024,"Concurrent capture bank");
    PreparedCaptureBank bank(c,ledger);
    ThreadAudit producer,consumer;
    std::atomic<unsigned> returned{0}; std::atomic<bool> failed{false};
    const auto admitted=ledger.usage();
    std::thread output([&] {
        ClockCorrelationConfig cfg; cfg.identity=identity;
        PreparedClockCorrelation correlation(cfg); // OFF RT.
        CaptureBankView view; rt_audit::reset();
        while (!failed.load(std::memory_order_acquire)) {
            CaptureBankRead read;
            {
                rt_audit::Guard guard; read=bank.acquire(view);
                if (read==CaptureBankRead::Ready) {
                    const auto n=consumer.packets;
                    consumer.valid &= view.token.sequence==n && view.token.original.position.value==consumer.frames;
                    const auto frames=std::uint32_t(n%31+1);
                    consumer.valid &= view.token.original.frames==frames && view.interleaved.size()==std::size_t(frames)*3;
                    for (std::uint32_t f=0;f<frames;++f)
                        for (unsigned channel=0;channel<3;++channel)
                            consumer.valid &= view.interleaved[std::size_t(f)*3+channel]==float(n*100+f*3+channel)-2.5f;
                    const auto stamp=view.token.original;
                    const auto fit=correlation.observe({stamp.identity,stamp.position,stamp.qpc,ClockReliability::Accurate,(stamp.flags&bankDiscontinuity)!=0});
                    consumer.valid &= fit==ClockCorrelationStatus::Ready || fit==ClockCorrelationStatus::WaitingSpan;
                    consumer.valid &= bank.release(view);
                    ++consumer.packets; consumer.frames+=frames;
                }
            }
            if (read==CaptureBankRead::Ready) returned.store(unsigned(consumer.packets),std::memory_order_release);
            if (!consumer.valid) { failed.store(true,std::memory_order_release); bank.consumerFailed(); break; }
            if (read==CaptureBankRead::Drained) break;
            if (read==CaptureBankRead::Empty) std::this_thread::yield(); // Fixture pacing, OUTSIDE audit.
        }
        consumer.counts=rt_audit::counts;
    });
    std::thread input([&] {
        std::array<float,93> source{}; rt_audit::reset();
        for (unsigned n=0;n<count && !failed.load(std::memory_order_acquire);++n) {
            // Fixture credit avoids deliberately overrunning the finite bank.
            // Production publish itself never retries, waits or paces either owner.
            while (n-returned.load(std::memory_order_acquire)>=c.slots && !failed.load(std::memory_order_acquire))
                std::this_thread::yield();
            if (failed.load(std::memory_order_acquire)) break;
            const auto frames=n%31+1;
            for (std::uint32_t f=0;f<frames;++f)
                for (unsigned channel=0;channel<3;++channel) source[f*3+channel]=float(std::uint64_t(n)*100+f*3+channel)-2.5f;
            const auto stamp=packet(source,frames,producer.frames,10000000+producer.frames*10000000/48000,n==0 ? bankDiscontinuity : 0);
            {
                rt_audit::Guard guard;
                producer.valid &= bank.publish(stamp).acceptedFrames==frames;
                std::fill(source.begin(),source.end(),-999.f);
                ++producer.packets; producer.frames+=frames;
            }
            if (!producer.valid) { failed.store(true,std::memory_order_release); break; }
        }
        { rt_audit::Guard guard; bank.finish(); }
        producer.counts=rt_audit::counts;
    });
    input.join(); output.join();
    require(!failed.load() && producer.valid && consumer.valid && producer.packets==count && consumer.packets==count &&
            producer.frames==consumer.frames && bank.acceptedFrames()==producer.frames && !bank.rejectedFrames() && bank.drained(),"concurrent FIFO copies, metadata, clock observations and complete drain");
    require(ledger.usage()==admitted,"concurrent owners retain fixed admitted credit");
    for (const auto &audit : {producer.counts,consumer.counts}) {
        require(!audit.cppAllocate && !audit.cppFree,"both owners C++ RT allocation/free audit");
#ifdef SC_WRAP_LIBC
        require(!audit.cAllocate && !audit.cFree && !audit.blockingLock,"both owners interposed libc/pthread RT audit");
#endif
    }
    std::cout << "concurrentPackets=" << consumer.packets << " concurrentFrames=" << consumer.frames
              << " cppAllocations=0 cppFrees=0";
#ifdef SC_WRAP_LIBC
    std::cout << " libcInterposition=enabled cAllocations=0 cFrees=0 blockingLocks=0\n";
#else
    std::cout << " libcInterposition=disabled\n";
#endif
}
}
int main() {
    try {
        fidelityAndLeases(); failuresAndPrefix(); silentStopAndAdmission(); releasedScratch(); concurrentOwners();
        std::cout << checks << " stamped capture bank checks passed\n"; return 0;
    } catch(const std::exception &e) {
        rt_audit::active=false; std::cerr << "Check " << checks << ": " << e.what() << '\n'; return 1;
    }
}
