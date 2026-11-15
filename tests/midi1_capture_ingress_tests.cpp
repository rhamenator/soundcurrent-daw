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
void retainedFaultAndWatermark() {
    PreparedMidi1CaptureIngress ingress(id,1,std::numeric_limits<std::uint32_t>::max());
    hot([&] {
        require(ingress.trySubmit(event(4))==Midi1CaptureIngressStatus::Accepted);
        auto original=event(100,0xffffffffu);
        require(ingress.trySubmit(original)==Midi1CaptureIngressStatus::Full);
        original=event(200);
        const auto fault=ingress.firstFault();
        require(fault && fault->status==Midi1CaptureIngressStatus::Full && fault->rejected==event(100,0xffffffffu));
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
    PreparedMidi1CaptureIngress ingress(id,4,0,2);
    hot([&] {
        require(ingress.capacity()==4 && ingress.releaseReserve()==2);
        const auto ordinaryEvent=event(0);
        require(!isMidi1CaptureReleaseMessage(ordinaryEvent.message()));
        const auto make=[&](std::uint32_t packed) {
            return *Midi1PackedMillisecondCapture::fromOriginal(
                *MidiMillisecondCaptureStamp::fromOriginal(id,1,0,MidiCaptureNotice::Data,0),packed);
        };
        for(const auto packed:std::array<std::uint32_t,8>{
                0xa5003c80u, 0xa5003c90u, 0xa50040b0u, 0xa00078b0u,
                0xa00079b0u, 0xa0007bb0u, 0xa00000fcu, 0xa00000ffu}) {
            const auto classified=make(packed);
            require(isMidi1CaptureReleaseMessage(classified.message()));
        }
        const auto sustainOn=make(0xa50140b0u);
        require(!isMidi1CaptureReleaseMessage(sustainOn.message()));
        require(ingress.trySubmit(event(1))==Midi1CaptureIngressStatus::Accepted);
        require(ingress.trySubmit(event(2))==Midi1CaptureIngressStatus::Accepted);
        require(ingress.trySubmit(event(3))==Midi1CaptureIngressStatus::ReservedForRelease);
        require(ingress.firstFault()->status==Midi1CaptureIngressStatus::ReservedForRelease);
        require(!ingress.firstReleaseFault());
        require(ingress.trySubmit(event(3,0,id,0xa5003c80u))==Midi1CaptureIngressStatus::Accepted);
        require(ingress.trySubmit(event(4,0,id,0xa5003d80u))==Midi1CaptureIngressStatus::Accepted);
        require(ingress.trySubmit(event(5,0,id,0xa5003e80u))==Midi1CaptureIngressStatus::ReleaseReserveFull);
        const auto releaseFault=ingress.firstReleaseFault();
        require(releaseFault && releaseFault->status==Midi1CaptureIngressStatus::ReleaseReserveFull &&
                releaseFault->rejected==event(5,0,id,0xa5003e80u));
        require(ingress.firstFault()->status==Midi1CaptureIngressStatus::ReservedForRelease);
        std::optional<Midi1PackedMillisecondCapture> out;
        for(const auto expected:std::array{event(1),event(2),event(3,0,id,0xa5003c80u),
                                            event(4,0,id,0xa5003d80u)})
            require(ingress.tryPop(out) && out && *out==expected);
        require(!ingress.tryPop(out) && !ingress.consumerAvailable());
        require(ingress.firstReleaseFault()==releaseFault);
    });
}
void identityAndSequenceLimit() {
    PreparedMidi1CaptureIngress ingress(id,2), limit(id,1);
    hot([&] {
        const auto wrong=event(std::numeric_limits<std::uint64_t>::max(),0,{2,3,6});
        require(ingress.trySubmit(wrong)==Midi1CaptureIngressStatus::InvalidIdentity);
        require(ingress.firstFault()->rejected==wrong && !ingress.consumerAvailable());
        require(ingress.trySubmit(event(0))==Midi1CaptureIngressStatus::Accepted);
        std::optional<Midi1PackedMillisecondCapture> out;
        require(ingress.tryPop(out) && *out==event(0));
        require(limit.trySubmit(event(std::numeric_limits<std::uint64_t>::max()))==Midi1CaptureIngressStatus::Accepted);
        require(limit.tryPop(out));
        require(limit.trySubmit(event(0))==Midi1CaptureIngressStatus::SequenceNotIncreasing);
        require(limit.firstFault()->status==Midi1CaptureIngressStatus::SequenceNotIncreasing);
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
        std::optional<Midi1PackedMillisecondCapture> out; bool popped=false;
        { rt_audit::Guard guard; popped=ingress.tryPop(out); }
        if(fault->status!=Midi1CaptureIngressStatus::Full || fault->rejected!=event(1,0xffffffffu) ||
           !popped || !out || *out!=event(0) || !auditZero()) failed.store(true);
    });
    std::jthread producer([&] {
        while(!consumerReady.load(std::memory_order_acquire)) {
            if(std::chrono::steady_clock::now()>=deadline) { failed.store(true); return; }
            std::this_thread::yield();
        }
        rt_audit::reset(); Midi1CaptureIngressStatus first,second;
        { rt_audit::Guard guard;
          first=ingress.trySubmit(event(0)); second=ingress.trySubmit(event(1,0xffffffffu)); }
        if(first!=Midi1CaptureIngressStatus::Accepted || second!=Midi1CaptureIngressStatus::Full || !auditZero())
            failed.store(true);
    });
    producer.join(); consumer.join(); require(!failed.load());
    require(ingress.firstFault()->rejected==event(1,0xffffffffu));
}
}
int main() {
    try { preparationAndWrap(); retainedFaultAndWatermark(); releaseReserveAndFaultVisibility(); identityAndSequenceLimit();
          maximumCapacity(); concurrentFirstFault();
        std::cout << "MIDI1 capture ingress: owned raw FIFO, identity+sequence, timestamp wrap, immutable first fault publication, joined owners, audit0\n";
        return 0;
    } catch(const std::exception &e) { std::cerr<<e.what()<<'\n'; return 1; }
}
