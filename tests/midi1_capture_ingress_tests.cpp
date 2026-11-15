// SPDX-License-Identifier: GPL-3.0-only
#include <soundcurrent/midi1_capture_ingress.hpp>
#include "rt_audit.hpp"
#include <array>
#include <atomic>
#include <chrono>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <thread>
using namespace soundcurrent::daw;
namespace {
void require(bool value) { if (!value) throw std::runtime_error("MIDI capture ingress contract failed"); }
constexpr MidiCaptureStreamIdentity id{2,3,5};
Midi1PackedMillisecondCapture event(std::uint64_t sequence, std::uint32_t ms=0,
                                   MidiCaptureStreamIdentity source=id,
                                   std::uint32_t packed=0xa5013c90u) {
    const auto stamp=MidiMillisecondCaptureStamp::fromOriginal(source,sequence,ms,
        MidiCaptureNotice::ConsumerBehind,std::uint64_t(10000));
    return *Midi1PackedMillisecondCapture::fromOriginal(*stamp,packed);
}
bool auditZero() {
    return !rt_audit::counts.cppAllocate && !rt_audit::counts.cppFree &&
           !rt_audit::counts.cAllocate && !rt_audit::counts.cFree && !rt_audit::counts.blockingLock;
}
struct ExpectedConcurrentEvent {
    Midi1PackedMillisecondCapture capture;
    bool release;
};
ExpectedConcurrentEvent concurrentReserveEvent(std::uint64_t sequence) {
    const bool release=sequence>=3 && (sequence<=5 || sequence%4==3);
    const auto packed=!release ? 0xa5013c90u :
        (sequence==3 ? 0xa5003c80u : sequence==4 ? 0xa5003d80u :
         sequence==5 ? 0xa5003e80u : 0xa5003c80u);
    return {event(sequence,0,id,packed),release};
}
template<class F> void hot(F run) {
    rt_audit::reset(); { rt_audit::Guard guard; run(); } require(auditZero());
}
void preparationAndWrap() {
    for(auto bad:std::array{MidiCaptureStreamIdentity{0,3,5},MidiCaptureStreamIdentity{2,0,5},
                             MidiCaptureStreamIdentity{2,3,0}}) {
        bool threw=false; try { PreparedMidi1CaptureIngress invalid(bad,1); }
        catch(const ProjectError &) { threw=true; } require(threw);
    }
    for(std::uint32_t capacity:{0u,4097u}) {
        bool threw=false; try { PreparedMidi1CaptureIngress invalid(id,capacity); }
        catch(const ProjectError &) { threw=true; } require(threw);
    }
    PreparedMidi1CaptureIngress ingress(id,3,std::numeric_limits<std::uint32_t>::max()-1);
    require(ingress.capacity()==4 && ingress.identity()==id);
    hot([&] {
        require(!ingress.firstFault());
        for(std::uint64_t seq=0;seq<16;++seq) {
            const auto expected=event(seq,seq==0?0xffffffffu:0u);
            require(ingress.trySubmit(expected)==Midi1CaptureIngressStatus::Accepted);
            std::optional<Midi1PackedMillisecondCapture> out;
            require(ingress.tryPop(out) && out && *out==expected);
            require(!ingress.tryPop(out) && out && *out==expected);
        }
        require(!ingress.firstFault() && !ingress.consumerAvailable());
    });
}
void producerAvailableBoundaries() {
    PreparedSpscQueue<std::uint32_t> queue(3,std::numeric_limits<std::uint32_t>::max()-1);
    hot([&] {
        require(PreparedSpscQueue<std::uint32_t>::storageCapacity(3)==4 &&
                queue.producerAvailable()==4);
        require(queue.tryPush(1) && queue.producerAvailable()==3);
        require(queue.tryPush(2) && queue.producerAvailable()==2);
        require(queue.tryPush(3) && queue.producerAvailable()==1);
        require(queue.tryPush(4) && queue.producerAvailable()==0);
        require(!queue.tryPush(5) && queue.producerAvailable()==0);
        std::uint32_t out=0;
        require(queue.tryPop(out) && out==1 && queue.producerAvailable()==1);
        require(queue.tryPop(out) && out==2 && queue.producerAvailable()==2);
        require(queue.tryPush(5) && queue.producerAvailable()==1);
    });
}
void retainedFaultAndWatermark() {
    PreparedMidi1CaptureIngress ingress(id,1,std::numeric_limits<std::uint32_t>::max());
    hot([&] {
        require(ingress.trySubmit(event(4))==Midi1CaptureIngressStatus::Accepted);
        auto original=event(100,0xffffffffu,id,0xa5003c90u);
        require(ingress.trySubmit(original)==Midi1CaptureIngressStatus::Full);
        original=event(200);
        const auto fault=ingress.firstFault();
        require(fault && fault->status==Midi1CaptureIngressStatus::Full &&
                fault->rejected==event(100,0xffffffffu,id,0xa5003c90u));
        const auto releaseFault=ingress.firstReleaseOverflowFault();
        require(releaseFault && releaseFault->status==Midi1CaptureIngressStatus::Full &&
                releaseFault->rejected==event(100,0xffffffffu,id,0xa5003c90u));
        std::optional<Midi1PackedMillisecondCapture> out;
        require(ingress.tryPop(out) && *out==event(4));
        require(ingress.trySubmit(event(50))==Midi1CaptureIngressStatus::Accepted); // Failed100 did not advance accepted watermark.
        require(ingress.trySubmit(event(50))==Midi1CaptureIngressStatus::SequenceNotIncreasing);
        require(ingress.tryPop(out) && *out==event(50));
        require(ingress.trySubmit(event(49))==Midi1CaptureIngressStatus::SequenceNotIncreasing);
        require(ingress.firstFault()==fault && !ingress.consumerAvailable() && original.stamp().sequence()==200);
    });
}
void releaseReserveAndFaultVisibility() {
    bool threw=false;
    try { PreparedMidi1CaptureIngress invalid(id,3,0,5); }
    catch(const ProjectError &) { threw=true; }
    require(threw);
    PreparedMidi1CaptureIngress ingress(id,4,std::numeric_limits<std::uint32_t>::max()-1,2);
    hot([&] {
        require(ingress.capacity()==4 && ingress.releaseReserve()==2);
        require(!ingress.firstFault() && !ingress.firstReservedRefusal());
        const auto ordinaryEvent=event(0);
        require(!isMidi1CaptureReleaseMessage(ordinaryEvent.message()));
        const auto make=[&](std::uint32_t packed) {
            const auto stamp=MidiMillisecondCaptureStamp::fromOriginal(
                id,1,0,MidiCaptureNotice::Data,0);
            require(stamp.has_value());
            const auto capture=Midi1PackedMillisecondCapture::fromOriginal(*stamp,packed);
            require(capture.has_value());
            return *capture;
        };
        for(const auto packed:std::array<std::uint32_t,24>{
                0xa5003c80u, 0xa5003c90u, 0xa50040b0u, 0xa00078b0u,
                0xa00079b0u, 0xa0007bb0u, 0xa00042b0u, 0xa00045b0u,
                0xa0007cb0u, 0xa0007db0u, 0xa0007eb0u, 0xa0007fb0u,
                0xa00000fcu, 0xa00000ffu, 0xa0003c81u, 0xa0003c91u,
                0xa00040bfu, 0xa07f78b0u, 0xa07f79b0u, 0xa07f7bb0u,
                0xa07f7cb0u, 0xa07f7db0u, 0xa07f7eb0u, 0xa07f7fb0u}) {
            const auto classified=make(packed);
            require(isMidi1CaptureReleaseMessage(classified.message()));
        }
        for(const auto packed:std::array<std::uint32_t,24>{
                0xa04040b0u, 0xa07f40b0u, 0xa04042b0u, 0xa07f42b0u,
                0xa04045b0u, 0xa07f45b0u, 0xa00041b0u, 0xa00043b0u,
                0xa00044b0u, 0xa0007ab0u, 0xa00000e0u, 0xa00000fbu,
                0xa0013c90u, 0xa04040b1u, 0xa00000fau, 0xa07f3ca0u,
                0xa00040c0u, 0xa00040d0u, 0xa00000f8u, 0xa00000feu,
                0xa00040f1u, 0xa00040f3u, 0xa00040f2u, 0xa00000f6u}) {
            const auto classified=make(packed);
            require(!isMidi1CaptureReleaseMessage(classified.message()));
        }
        for(const auto packed:std::array<std::uint32_t,3>{
                0xa03f40b0u, 0xa03f42b0u, 0xa03f45b0u}) {
            const auto classified=make(packed);
            require(isMidi1CaptureReleaseMessage(classified.message()));
        }
        require(ingress.trySubmit(event(1))==Midi1CaptureIngressStatus::Accepted);
        require(ingress.trySubmit(event(2))==Midi1CaptureIngressStatus::Accepted);
        require(ingress.trySubmit(event(3))==Midi1CaptureIngressStatus::ReservedForRelease);
        require(!ingress.firstFault());
        const auto backpressure=ingress.firstReservedRefusal();
        require(backpressure && backpressure->status==Midi1CaptureIngressStatus::ReservedForRelease &&
                backpressure->rejected==event(3));
        require(!ingress.firstReleaseOverflowFault());
        require(ingress.trySubmit(event(4,0,id,0xa5003c80u))==Midi1CaptureIngressStatus::Accepted);
        require(ingress.trySubmit(event(5,0,id,0xa5003d80u))==Midi1CaptureIngressStatus::Accepted);
        require(ingress.trySubmit(event(6,0,id,0xa5003e80u))==Midi1CaptureIngressStatus::ReleaseReserveFull);
        const auto releaseFault=ingress.firstReleaseOverflowFault();
        require(releaseFault && releaseFault->status==Midi1CaptureIngressStatus::ReleaseReserveFull &&
                releaseFault->rejected==event(6,0,id,0xa5003e80u));
        require(ingress.firstFault()->status==Midi1CaptureIngressStatus::ReleaseReserveFull);
        std::optional<Midi1PackedMillisecondCapture> out;
        for(const auto expected:std::array{event(1),event(2),event(4,0,id,0xa5003c80u),
                                            event(5,0,id,0xa5003d80u)})
            require(ingress.tryPop(out) && out && *out==expected);
        require(!ingress.tryPop(out) && !ingress.consumerAvailable());
        require(ingress.firstReleaseOverflowFault()==releaseFault);
        require(ingress.firstReservedRefusal()==backpressure);
        require(ingress.trySubmit(event(7))==Midi1CaptureIngressStatus::Accepted);
        require(ingress.tryPop(out) && out && *out==event(7));
    });
    PreparedMidi1CaptureIngress releaseOnly(id,4,0,4);
    hot([&] {
        require(releaseOnly.trySubmit(event(1))==Midi1CaptureIngressStatus::ReservedForRelease);
        require(!releaseOnly.firstFault() && releaseOnly.firstReservedRefusal());
        require(releaseOnly.trySubmit(event(2,0,id,0xa5003c80u))==Midi1CaptureIngressStatus::Accepted);
    });
    PreparedMidi1CaptureIngress oneOrdinarySlot(id,4,0,3);
    hot([&] {
        require(oneOrdinarySlot.trySubmit(event(1))==Midi1CaptureIngressStatus::Accepted);
        require(oneOrdinarySlot.trySubmit(event(2))==Midi1CaptureIngressStatus::ReservedForRelease);
        require(oneOrdinarySlot.trySubmit(event(3,0,id,0xa5003c80u))==Midi1CaptureIngressStatus::Accepted);
        require(oneOrdinarySlot.trySubmit(event(4))==Midi1CaptureIngressStatus::ReservedForRelease);
        require(oneOrdinarySlot.trySubmit(event(5,0,id,0xa5003d80u))==Midi1CaptureIngressStatus::Accepted);
        require(oneOrdinarySlot.firstReservedRefusal() &&
                oneOrdinarySlot.firstReservedRefusal()->rejected==event(2));
        require(!oneOrdinarySlot.firstFault());
        require(oneOrdinarySlot.trySubmit(event(6,0,id,0xa5003e80u))==Midi1CaptureIngressStatus::Accepted);
        require(oneOrdinarySlot.trySubmit(event(7,0,id,0xa5003f80u))==Midi1CaptureIngressStatus::ReleaseReserveFull);
    });
    PreparedMidi1CaptureIngress noReserve(id,1);
    hot([&] {
        require(noReserve.trySubmit(event(1))==Midi1CaptureIngressStatus::Accepted);
        require(noReserve.trySubmit(event(2,0,id,0xa5003c80u))==Midi1CaptureIngressStatus::Full);
        require(noReserve.firstFault()->status==Midi1CaptureIngressStatus::Full);
        require(noReserve.firstReleaseOverflowFault()->status==Midi1CaptureIngressStatus::Full);
    });
    PreparedMidi1CaptureIngress preserveFault(id,1);
    hot([&] {
        require(preserveFault.trySubmit(event(1))==Midi1CaptureIngressStatus::Accepted);
        require(preserveFault.trySubmit(event(2))==Midi1CaptureIngressStatus::Full);
        const auto ordinary=preserveFault.firstFault();
        require(ordinary && ordinary->rejected==event(2));
        require(preserveFault.trySubmit(event(3,0,id,0xa5003c80u))==Midi1CaptureIngressStatus::Full);
        require(preserveFault.firstFault()==ordinary);
        require(preserveFault.firstReleaseOverflowFault() &&
                preserveFault.firstReleaseOverflowFault()->rejected==event(3,0,id,0xa5003c80u));
    });
    PreparedMidi1CaptureIngress ordinaryAfterReleaseExhaustion(id,1,0,1);
    hot([&] {
        require(ordinaryAfterReleaseExhaustion.trySubmit(event(1))==Midi1CaptureIngressStatus::ReservedForRelease);
        require(ordinaryAfterReleaseExhaustion.trySubmit(event(2,0,id,0xa5003c80u))==Midi1CaptureIngressStatus::Accepted);
        require(ordinaryAfterReleaseExhaustion.trySubmit(event(3))==Midi1CaptureIngressStatus::Full);
        require(ordinaryAfterReleaseExhaustion.firstFault() &&
                ordinaryAfterReleaseExhaustion.firstFault()->status==Midi1CaptureIngressStatus::Full);
        require(ordinaryAfterReleaseExhaustion.firstReservedRefusal() &&
                ordinaryAfterReleaseExhaustion.firstReservedRefusal()->rejected==event(1));
    });
}
void reserveRefusalRecoveryAndWatermark() {
    PreparedMidi1CaptureIngress ingress(id,4,0,2);
    hot([&] {
        require(ingress.trySubmit(event(1))==Midi1CaptureIngressStatus::Accepted);
        require(ingress.trySubmit(event(2))==Midi1CaptureIngressStatus::Accepted);
        const auto refused=event(3);
        require(ingress.producerAvailable()==2);
        require(ingress.trySubmit(refused)==Midi1CaptureIngressStatus::ReservedForRelease);
        std::optional<Midi1PackedMillisecondCapture> out;
        require(ingress.tryPop(out) && out && *out==event(1));
        // A refusal does not advance the accepted watermark; the same sequence
        // can be retried after consumer progress and is then accepted.
        require(ingress.trySubmit(refused)==Midi1CaptureIngressStatus::Accepted);
        require(ingress.trySubmit(event(3))==Midi1CaptureIngressStatus::SequenceNotIncreasing);
        require(ingress.tryPop(out) && out && *out==event(2));
        require(ingress.tryPop(out) && out && *out==refused);
        require(!ingress.consumerAvailable());
    });
}
void reservePreconditionPrecedence() {
    PreparedMidi1CaptureIngress ingress(id,4,0,3);
    hot([&] {
        require(ingress.trySubmit(event(1))==Midi1CaptureIngressStatus::Accepted);
        const auto wrongIdentity=event(2,0,{2,3,6});
        require(ingress.trySubmit(wrongIdentity)==Midi1CaptureIngressStatus::InvalidIdentity);
        require(!ingress.firstReservedRefusal());
        require(ingress.trySubmit(event(1))==Midi1CaptureIngressStatus::SequenceNotIncreasing);
        require(!ingress.firstReservedRefusal());
        require(ingress.trySubmit(event(2))==Midi1CaptureIngressStatus::ReservedForRelease);
        require(ingress.firstReservedRefusal() && ingress.firstReservedRefusal()->rejected==event(2));
    });
    PreparedMidi1CaptureIngress laterFaults(id,4,0,3);
    hot([&] {
        require(laterFaults.trySubmit(event(1))==Midi1CaptureIngressStatus::Accepted);
        require(laterFaults.trySubmit(event(2))==Midi1CaptureIngressStatus::ReservedForRelease);
        const auto refusal=laterFaults.firstReservedRefusal();
        const auto wrong=event(3,0,{2,3,6});
        require(laterFaults.trySubmit(wrong)==Midi1CaptureIngressStatus::InvalidIdentity);
        require(laterFaults.trySubmit(event(1))==Midi1CaptureIngressStatus::SequenceNotIncreasing);
        require(laterFaults.firstFault() && laterFaults.firstFault()->rejected==wrong);
        require(laterFaults.firstReservedRefusal()==refusal);
    });
}
void identityAndSequenceLimit() {
    PreparedMidi1CaptureIngress ingress(id,2), limit(id,1);
    hot([&] {
        const auto wrong=event(std::numeric_limits<std::uint64_t>::max(),0,{2,3,6},0xa5003c80u);
        require(ingress.trySubmit(wrong)==Midi1CaptureIngressStatus::InvalidIdentity);
        require(ingress.firstFault()->rejected==wrong && !ingress.consumerAvailable() &&
                !ingress.firstReleaseOverflowFault());
        require(ingress.trySubmit(event(0))==Midi1CaptureIngressStatus::Accepted);
        std::optional<Midi1PackedMillisecondCapture> out;
        require(ingress.tryPop(out) && *out==event(0));
        require(limit.trySubmit(event(std::numeric_limits<std::uint64_t>::max()))==Midi1CaptureIngressStatus::Accepted);
        require(limit.tryPop(out));
        require(limit.trySubmit(event(0,0,id,0xa5003c80u))==Midi1CaptureIngressStatus::SequenceNotIncreasing);
        require(limit.firstFault()->status==Midi1CaptureIngressStatus::SequenceNotIncreasing);
        require(!limit.firstReleaseOverflowFault());
    });
}
void maximumCapacity() {
    PreparedMidi1CaptureIngress ingress(id,4096);
    hot([&] {
        for(std::uint64_t seq=0;seq<4096;++seq)
            require(ingress.trySubmit(event(seq,std::uint32_t(seq)))==Midi1CaptureIngressStatus::Accepted);
        require(ingress.consumerAvailable()==4096);
        std::optional<Midi1PackedMillisecondCapture> out;
        for(std::uint64_t seq=0;seq<4096;++seq)
            require(ingress.tryPop(out) && *out==event(seq,std::uint32_t(seq)));
        require(!ingress.firstFault() && !ingress.consumerAvailable());
    });
}
void concurrentFirstFault() {
    PreparedMidi1CaptureIngress ingress(id,1);
    const auto rejected=event(1,0xffffffffu,id,0xa5003c80u);
    std::atomic<bool> consumerReady{false}, failed{false};
    const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(20);
    // No second publication flag is used to hand off the fault payload.
    std::jthread consumer([&] {
        rt_audit::reset(); consumerReady.store(true,std::memory_order_release);
        std::optional<Midi1CaptureIngressFault> fault;
        for(;;) {
            { rt_audit::Guard guard; fault=ingress.firstFault(); }
            if(fault) break;
            if(std::chrono::steady_clock::now()>=deadline) { failed.store(true); return; }
            std::this_thread::yield();
        }
        std::optional<Midi1CaptureIngressFault> releaseFault;
        for(;;) {
            { rt_audit::Guard guard; releaseFault=ingress.firstReleaseOverflowFault(); }
            if(releaseFault) break;
            if(std::chrono::steady_clock::now()>=deadline) { failed.store(true); return; }
            std::this_thread::yield();
        }
        std::optional<Midi1PackedMillisecondCapture> out; bool popped=false;
        { rt_audit::Guard guard; popped=ingress.tryPop(out); }
        if(fault->status!=Midi1CaptureIngressStatus::Full || fault->rejected!=rejected ||
           releaseFault->status!=Midi1CaptureIngressStatus::Full || releaseFault->rejected!=rejected ||
           !popped || !out || *out!=event(0) || !auditZero()) failed.store(true);
    });
    std::jthread producer([&] {
        while(!consumerReady.load(std::memory_order_acquire)) {
            if(std::chrono::steady_clock::now()>=deadline) { failed.store(true); return; }
            std::this_thread::yield();
        }
        rt_audit::reset(); Midi1CaptureIngressStatus first,second;
        { rt_audit::Guard guard;
          first=ingress.trySubmit(event(0)); second=ingress.trySubmit(rejected); }
        if(first!=Midi1CaptureIngressStatus::Accepted || second!=Midi1CaptureIngressStatus::Full || !auditZero())
            failed.store(true);
    });
    producer.join(); consumer.join(); require(!failed.load());
    require(ingress.firstFault()->rejected==rejected);
}
void concurrentReservePublication() {
    PreparedMidi1CaptureIngress ingress(id,2,0,1);
    std::atomic<bool> consumerReady{false}, failed{false};
    const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(20);
    std::jthread consumer([&] {
        rt_audit::reset(); consumerReady.store(true,std::memory_order_release);
        std::optional<Midi1CaptureIngressFault> refused, overflow;
        for(;;) {
            { rt_audit::Guard guard; refused=ingress.firstReservedRefusal(); }
            if(refused) break;
            if(std::chrono::steady_clock::now()>=deadline) { failed.store(true); return; }
            std::this_thread::yield();
        }
        for(;;) {
            { rt_audit::Guard guard; overflow=ingress.firstReleaseOverflowFault(); }
            if(overflow) break;
            if(std::chrono::steady_clock::now()>=deadline) { failed.store(true); return; }
            std::this_thread::yield();
        }
        std::optional<Midi1PackedMillisecondCapture> first,second;
        { rt_audit::Guard guard;
          const bool firstOk=ingress.tryPop(first), secondOk=ingress.tryPop(second);
          if(!firstOk || !secondOk || !first || !second ||
          *first!=event(1) || *second!=event(3,0,id,0xa5003c80u) || !auditZero()) failed.store(true); }
        const auto expectedRefusal=event(2);
        const auto expectedOverflow=event(4,0,id,0xa5003e80u);
        if(refused->status!=Midi1CaptureIngressStatus::ReservedForRelease ||
           refused->rejected!=expectedRefusal ||
           overflow->status!=Midi1CaptureIngressStatus::ReleaseReserveFull ||
           overflow->rejected!=expectedOverflow || !auditZero()) failed.store(true);
    });
    std::jthread producer([&] {
        while(!consumerReady.load(std::memory_order_acquire)) {
            if(std::chrono::steady_clock::now()>=deadline) { failed.store(true); return; }
            std::this_thread::yield();
        }
        rt_audit::reset();
        Midi1CaptureIngressStatus a,b,c,d;
        { rt_audit::Guard guard;
          a=ingress.trySubmit(event(1));
          b=ingress.trySubmit(event(2));
          c=ingress.trySubmit(event(3,0,id,0xa5003c80u));
          d=ingress.trySubmit(event(4,0,id,0xa5003e80u)); }
        if(a!=Midi1CaptureIngressStatus::Accepted || b!=Midi1CaptureIngressStatus::ReservedForRelease ||
           c!=Midi1CaptureIngressStatus::Accepted || d!=Midi1CaptureIngressStatus::ReleaseReserveFull ||
           !auditZero()) failed.store(true);
    });
    producer.join(); consumer.join(); require(!failed.load());
}
void concurrentReserveAndConsumption() {
    PreparedMidi1CaptureIngress ingress(id,4,std::numeric_limits<std::uint32_t>::max()-1,2);
    constexpr std::uint64_t attempts=20000;
    std::atomic<bool> consumerReady{false}, drainEnabled{false}, consumerDrainReady{false};
    std::atomic<bool> allowDrain{false}, oneSlotFreedForRefusal{false}, finishRefusalHandshake{false};
    std::atomic<bool> queueDrainedForHandshake{false}, producerDone{false}, failed{false};
    std::atomic<std::uint32_t> accepted{0}, consumed{0};
    std::atomic<std::uint32_t> ordinaryAcceptedAfterDrain{0}, ordinaryRefusedAfterDrain{0};
    const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(20);
    std::jthread consumer([&] {
        rt_audit::reset(); consumerReady.store(true,std::memory_order_release);
        while(!drainEnabled.load(std::memory_order_acquire) && !producerDone.load(std::memory_order_acquire)) {
            if(std::chrono::steady_clock::now()>=deadline) { failed.store(true); return; }
            std::this_thread::yield();
        }
        if(!drainEnabled.load(std::memory_order_acquire)) return;
        consumerDrainReady.store(true,std::memory_order_release);
        while(!allowDrain.load(std::memory_order_acquire) && !producerDone.load(std::memory_order_acquire)) {
            if(std::chrono::steady_clock::now()>=deadline) { failed.store(true); return; }
            std::this_thread::yield();
        }
        bool haveSequence=false; std::uint64_t lastSequence=0;
        for(;;) {
            std::optional<Midi1PackedMillisecondCapture> out; bool popped=false;
            { rt_audit::Guard guard; popped=ingress.tryPop(out); }
            if(popped) {
                if(!out || (haveSequence && out->stamp().sequence()<=lastSequence)) failed.store(true);
                else {
                    const auto seq=out->stamp().sequence();
                    const auto expected=concurrentReserveEvent(seq);
                    if(*out!=expected.capture ||
                       isMidi1CaptureReleaseMessage(out->message())!=expected.release) failed.store(true);
                    lastSequence=seq; haveSequence=true; consumed.fetch_add(1,std::memory_order_relaxed);
                }
                if(!oneSlotFreedForRefusal.exchange(true,std::memory_order_acq_rel)) {
                    while(!finishRefusalHandshake.load(std::memory_order_acquire)) {
                        if(std::chrono::steady_clock::now()>=deadline) { failed.store(true); return; }
                        std::this_thread::yield();
                    }
                }
            } else if(!ingress.consumerAvailable()) {
                queueDrainedForHandshake.store(true,std::memory_order_release);
                if(producerDone.load(std::memory_order_acquire)) break;
                std::this_thread::yield();
            }
            else if(std::chrono::steady_clock::now()>=deadline) { failed.store(true); break; }
            else std::this_thread::yield();
        }
        if(!auditZero()) failed.store(true);
    });
    std::jthread producer([&] {
        while(!consumerReady.load(std::memory_order_acquire)) {
            if(std::chrono::steady_clock::now()>=deadline) { failed.store(true); producerDone.store(true,std::memory_order_release); return; }
            std::this_thread::yield();
        }
        rt_audit::reset();
        Midi1CaptureIngressStatus a,b,c,d,e,f;
        { rt_audit::Guard guard;
          a=ingress.trySubmit(event(0)); b=ingress.trySubmit(event(1)); c=ingress.trySubmit(event(2));
          d=ingress.trySubmit(event(3,0,id,0xa5003c80u)); e=ingress.trySubmit(event(4,0,id,0xa5003d80u));
          f=ingress.trySubmit(event(5,0,id,0xa5003e80u)); }
        if(a!=Midi1CaptureIngressStatus::Accepted || b!=Midi1CaptureIngressStatus::Accepted ||
           c!=Midi1CaptureIngressStatus::ReservedForRelease || d!=Midi1CaptureIngressStatus::Accepted ||
           e!=Midi1CaptureIngressStatus::Accepted || f!=Midi1CaptureIngressStatus::ReleaseReserveFull ||
           !auditZero()) failed.store(true);
        accepted.fetch_add(4,std::memory_order_relaxed);
        drainEnabled.store(true,std::memory_order_release);
        while(!consumerDrainReady.load(std::memory_order_acquire)) {
            if(std::chrono::steady_clock::now()>=deadline) {
                failed.store(true); producerDone.store(true,std::memory_order_release); return;
            }
            std::this_thread::yield();
        }
        allowDrain.store(true,std::memory_order_release);
        while(!oneSlotFreedForRefusal.load(std::memory_order_acquire)) {
            if(std::chrono::steady_clock::now()>=deadline) {
                failed.store(true); producerDone.store(true,std::memory_order_release); return;
            }
            std::this_thread::yield();
        }
        const auto refusedAfterDrain=concurrentReserveEvent(6);
        Midi1CaptureIngressStatus refusedStatus;
        { rt_audit::Guard guard; refusedStatus=ingress.trySubmit(refusedAfterDrain.capture); }
        if(refusedStatus!=Midi1CaptureIngressStatus::ReservedForRelease) failed.store(true);
        else ordinaryRefusedAfterDrain.fetch_add(1,std::memory_order_relaxed);
        finishRefusalHandshake.store(true,std::memory_order_release);
        while(!queueDrainedForHandshake.load(std::memory_order_acquire)) {
            if(std::chrono::steady_clock::now()>=deadline) {
                failed.store(true); producerDone.store(true,std::memory_order_release); return;
            }
            std::this_thread::yield();
        }
        for(const std::uint64_t seq:{7u,8u}) {
            const auto expected=concurrentReserveEvent(seq);
            Midi1CaptureIngressStatus status;
            { rt_audit::Guard guard; status=ingress.trySubmit(expected.capture); }
            if(status!=Midi1CaptureIngressStatus::Accepted) failed.store(true);
            else {
                accepted.fetch_add(1,std::memory_order_relaxed);
                if(!expected.release) ordinaryAcceptedAfterDrain.fetch_add(1,std::memory_order_relaxed);
            }
        }
        for(std::uint64_t seq=9;seq<attempts;++seq) {
            const auto expected=concurrentReserveEvent(seq);
            const bool release=expected.release;
            Midi1CaptureIngressStatus status;
            { rt_audit::Guard guard; status=ingress.trySubmit(expected.capture); }
            if(status==Midi1CaptureIngressStatus::Accepted) {
                accepted.fetch_add(1,std::memory_order_relaxed);
                if(!release) ordinaryAcceptedAfterDrain.fetch_add(1,std::memory_order_relaxed);
                if(!release && ingress.producerAvailable()<ingress.releaseReserve()) failed.store(true);
            } else {
                if(!release && (status==Midi1CaptureIngressStatus::ReservedForRelease ||
                                status==Midi1CaptureIngressStatus::Full))
                    ordinaryRefusedAfterDrain.fetch_add(1,std::memory_order_relaxed);
                if(release ? status!=Midi1CaptureIngressStatus::ReleaseReserveFull :
                             (status!=Midi1CaptureIngressStatus::ReservedForRelease &&
                              status!=Midi1CaptureIngressStatus::Full)) failed.store(true);
            }
            if(std::chrono::steady_clock::now()>=deadline) { failed.store(true); break; }
        }
        producerDone.store(true,std::memory_order_release);
        if(!auditZero()) failed.store(true);
    });
    producer.join(); consumer.join();
    require(!failed.load() && consumed.load()==accepted.load() && accepted.load()>0 &&
                ordinaryAcceptedAfterDrain.load()>0 && ordinaryRefusedAfterDrain.load()>0 &&
                ingress.firstReservedRefusal() && ingress.firstReservedRefusal()->rejected==event(2) &&
                ingress.firstReleaseOverflowFault() && ingress.firstReleaseOverflowFault()->rejected==event(5,0,id,0xa5003e80u));
}
}
int main() {
    try { preparationAndWrap(); producerAvailableBoundaries(); retainedFaultAndWatermark(); releaseReserveAndFaultVisibility(); identityAndSequenceLimit();
          maximumCapacity(); concurrentFirstFault(); concurrentReservePublication(); reserveRefusalRecoveryAndWatermark();
          concurrentReserveAndConsumption(); reservePreconditionPrecedence();
        std::cout << "MIDI1 capture ingress: owned FIFO, release reserve, fault latches, concurrent SPSC, wrap, audit0\n";
        return 0;
    } catch(const std::exception &e) { std::cerr<<e.what()<<'\n'; return 1; }
}
