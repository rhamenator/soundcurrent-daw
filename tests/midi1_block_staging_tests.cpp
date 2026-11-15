// SPDX-License-Identifier: GPL-3.0-only
#include <soundcurrent/midi1_block_staging.hpp>
#include "rt_audit.hpp"
#include <array>
#include <iostream>
#include <limits>
#include <stdexcept>
using namespace soundcurrent::daw;
namespace {
void require(bool v) { if (!v) throw std::runtime_error("MIDI block staging contract failed"); }
constexpr Midi1IngressIdentity id{7,3};
TimedMidi1Event event(Frame frame, std::uint8_t key) {
    const std::array<std::uint8_t,3> bytes{0x90,key,0};
    return {frame,id.generation,id.endpoint,*Midi1ShortMessage::fromBytes(bytes)};
}
template<class F> void hot(F run) {
    rt_audit::reset(); { rt_audit::Guard guard; run(); }
    require(!rt_audit::counts.cppAllocate && !rt_audit::counts.cppFree &&
            !rt_audit::counts.cAllocate && !rt_audit::counts.cFree && !rt_audit::counts.blockingLock);
}
void boundaries() {
    PreparedMidi1Ingress ingress(id,8); PreparedMidi1BlockStaging stage(ingress);
    for (const auto &e : std::array{event(9,1),event(10,2),event(15,3),event(15,4),
                                    event(19,5),event(20,6),event(29,7)})
        require(ingress.trySubmit(e)==Midi1IngressStatus::Accepted);
    std::array<std::optional<Midi1BlockEvent>,8> out{};
    hot([&] {
        auto r=stage.tryRender({0,10,3},out);
        require(r.block.status==Midi1BlockStatus::Ok && r.block.emitted==1 && r.transferred==2 && r.pending==1);
        require(out[0]->offset==9 && out[0]->message.bytes()[1]==1 && ingress.consumerAvailable()==5);
        require(stage.pendingEvents()[0]==event(10,2));
        r=stage.tryRender({10,10,3},out);
        require(r.block.status==Midi1BlockStatus::Ok && r.block.emitted==4 && r.transferred==4 && r.pending==1);
        require(out[0]->offset==0 && out[1]->offset==5 && out[2]->offset==5 && out[3]->offset==9);
        require(out[1]->message.bytes()[1]==3 && out[2]->message.bytes()[1]==4 && ingress.consumerAvailable()==1);
        r=stage.tryRender({20,10,3},out);
        require(r.block.status==Midi1BlockStatus::Ok && r.block.emitted==2 && r.transferred==1 && r.pending==0);
        require(out[0]->offset==0 && out[1]->offset==9 && ingress.consumerAvailable()==0);
        r=stage.tryRender({30,10,3},out);
        require(r.block.status==Midi1BlockStatus::Ok && !r.block.emitted && !r.transferred && !r.pending);
    });
}
void refusalAndLate() {
    PreparedMidi1Ingress ingress(id,8); PreparedMidi1BlockStaging stage(ingress);
    for (const auto &e : std::array{event(0,1),event(1,2),event(10,3)})
        require(ingress.trySubmit(e)==Midi1IngressStatus::Accepted);
    std::array<std::optional<Midi1BlockEvent>,8> out{};
    for (auto &o : out) o=Midi1BlockEvent{99,7,event(0,99).message};
    const auto original=out;
    hot([&] {
        auto r=stage.tryRender({0,10,3},std::span(out).first(1));
        require(r.block.status==Midi1BlockStatus::OutputCapacityExceeded && !r.block.emitted);
        require(r.transferred==3 && r.pending==3 && ingress.consumerAvailable()==0 && out==original);
        require(stage.pendingEvents()[0]==event(0,1) && stage.pendingEvents()[2]==event(10,3));
        r=stage.tryRender({0,10,3},out);
        require(r.block.status==Midi1BlockStatus::Ok && r.block.emitted==2 && !r.transferred && r.pending==1);
        require(out[0]->message.bytes()[1]==1 && out[1]->message.bytes()[1]==2 && out[2]==original[2]);
        require(ingress.trySubmit(event(11,4))==Midi1IngressStatus::Accepted);
        const auto beforeLate=out;
        r=stage.tryRender({11,1,3},out);
        require(r.block.status==Midi1BlockStatus::InvalidEvent && !r.transferred && r.pending==1);
        require(ingress.consumerAvailable()==1 && out==beforeLate && stage.pendingEvents()[0]==event(10,3));
    });
}
void identityAndWindow() {
    PreparedMidi1Ingress ingress(id,2); PreparedMidi1BlockStaging stage(ingress);
    require(stage.identity()==id);
    require(ingress.trySubmit(event(0,1))==Midi1IngressStatus::Accepted);
    std::array<std::optional<Midi1BlockEvent>,2> out{};
    hot([&] {
        for (auto window : std::array{Midi1BlockWindow{0,0,3},
                     Midi1BlockWindow{std::numeric_limits<Frame>::max(),2,3}, Midi1BlockWindow{0,1,0}}) {
            const auto r=stage.tryRender(window,out);
            require(r.block.status==Midi1BlockStatus::InvalidWindow && !r.transferred && !r.pending);
        }
        const auto r=stage.tryRender({0,1,4},out);
        require(r.block.status==Midi1BlockStatus::InvalidEvent && !r.transferred && !r.pending);
        require(ingress.consumerAvailable()==1 && !out[0] && !out[1]);
    });
}
void exactLimit() {
    PreparedMidi1Ingress ingress(id,2048); PreparedMidi1BlockStaging stage(ingress);
    for(unsigned i=0;i<1024;++i) require(ingress.trySubmit(event(0,std::uint8_t(i%128)))==Midi1IngressStatus::Accepted);
    require(ingress.trySubmit(event(1,77))==Midi1IngressStatus::Accepted);
    std::array<std::optional<Midi1BlockEvent>,1024> out{};
    hot([&] {
        auto r=stage.tryRender({0,1,3},out);
        require(r.block.status==Midi1BlockStatus::Ok && r.block.emitted==1024 && r.transferred==1025 && r.pending==1);
        require(stage.pendingEvents()[0]==event(1,77) && ingress.consumerAvailable()==0);
        for(unsigned i=0;i<1024;++i) require(out[i] && out[i]->offset==0 && out[i]->message.bytes()[1]==i%128);
        r=stage.tryRender({1,1,3},out);
        require(r.block.status==Midi1BlockStatus::Ok && r.block.emitted==1 && !r.transferred && !r.pending);
        require(out[0]->message.bytes()[1]==77);
    });
}
void overload() {
    PreparedMidi1Ingress ingress(id,2048); PreparedMidi1BlockStaging stage(ingress);
    for (unsigned i=0;i<1026;++i) require(ingress.trySubmit(event(0,std::uint8_t(i%128)))==Midi1IngressStatus::Accepted);
    std::array<std::optional<Midi1BlockEvent>,1024> out{};
    hot([&] {
        auto r=stage.tryRender({0,1,3},out);
        require(r.block.status==Midi1BlockStatus::EventBudgetExceeded && !r.block.emitted);
        require(r.transferred==1025 && r.pending==1025 && ingress.consumerAvailable()==1);
        for(const auto &o:out) require(!o);
        require(stage.pendingEvents().front()==event(0,0) && stage.pendingEvents().back()==event(0,0));
        r=stage.tryRender({0,1,3},out);
        require(r.block.status==Midi1BlockStatus::EventBudgetExceeded && !r.transferred && r.pending==1025);
        require(ingress.consumerAvailable()==1);
    });
}
}
int main() {
    try { boundaries(); refusalAndLate(); identityAndWindow(); exactLimit(); overload();
        std::cout << "MIDI1 block staging: bounded transfer, future FIFO carry, refused output+owned pending, late+overload retention, audit0\n";
        return 0;
    } catch(const std::exception &e) { std::cerr<<e.what()<<'\n'; return 1; }
}
