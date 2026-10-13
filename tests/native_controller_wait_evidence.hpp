// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include "native_monitor_controller.hpp"
#include "fake_native_monitor_streams.hpp"
#include <nlohmann/json.hpp>
#include <chrono>
#include <ostream>
#include <iostream>
#include <source_location>
#include <thread>
namespace native_monitor_fixture {
using namespace soundcurrent::daw::ui;
struct ControlWaitContext {
    const NativeMonitorController &controller;
    const Spy *spy;
    std::string_view operation;
    unsigned variant=0;
};
inline nlohmann::json controlWaitObservation(const ControlWaitContext &c,std::uint64_t elapsedNs,
    std::source_location caller=std::source_location::current()) {
    const auto s=c.controller.snapshot(); // ONE immutable controller publication.
    const auto &p=s->progress;
    const auto utf8=[](const std::filesystem::path &path){const auto t=path.generic_u8string();return std::string(reinterpret_cast<const char *>(t.data()),t.size());};
    nlohmann::json r={{"schema",1},{"operation",c.operation},{"variant",c.variant},{"elapsedLocalNs",elapsedNs},
        {"limitMilliseconds",5000},{"sourceLine",caller.line()},{"sourceFunction",caller.function_name()},
        {"capturedBeforeUnwind",true},{"controllerSnapshotImmutable",true},{"controllerPhase",unsigned(s->phase)},
        {"generation",s->generation},{"completedCommands",s->completedCommands},{"errorSerial",s->errorSerial},
        {"acceptedEqRevision",s->acceptedEqRevision},{"appliedEqRevision",s->appliedEqRevision},
        {"cancelAcknowledged",s->cancelAcknowledged},{"resultSequence",s->resultSequence},
        {"resultPresent",bool(s->result)||bool(s->pipewireResult)},{"reopenedPresent",bool(s->reopened)||bool(s->pipewireReopened)},
        {"root",utf8(s->root)},{"job",utf8(s->job)},{"diagnostic",s->diagnostic},
        {"progress",{{"processingDone",p.processingDone},{"diskDone",p.diskDone},{"captureEnded",p.captureEnded},
        {"renderEnded",p.renderEnded},{"rawAccepted",p.rawAcceptedFrames},{"monitorAccepted",p.monitorAcceptedFrames},
        {"publishedMonitor",p.publishedMonitorFrames},{"deliveredMonitor",p.deliveredMonitorFrames},{"sourceCopied",p.copiedMonitorSourceFrames}}}};
    if(s->error)r["errorCode"]=unsigned(*s->error);else r["errorCode"]=nullptr;
    if(!c.spy){r["sdk"]=nullptr;return r;}
    const auto &spy=*c.spy;
    std::uint64_t sequence=0,packets=0,qpc=0;std::uint32_t stage=0;bool coherent=false;
    for(unsigned n=0;n<8;++n) {
        const auto before=spy.sdkObservationSequence.load(std::memory_order_acquire);
        stage=spy.sdkStage.load(std::memory_order_acquire);packets=spy.sdkRawPackets.load(std::memory_order_acquire);
        qpc=spy.qpc.load(std::memory_order_acquire);sequence=spy.sdkObservationSequence.load(std::memory_order_acquire);
        if(before==sequence && !(before&1)){coherent=true;break;}
    }
    r["sdk"]={{"serviceTupleCoherent",coherent},{"serviceSequence",sequence},{"stage",stage},{"rawPackets",packets},
        {"nowCoordinate100ns",qpc},{"coordinateDomain","simulated-device"},
        {"writtenPacketsIndependent",spy.writtenPackets.load(std::memory_order_acquire)},
        {"maximumCycleWorkNsIndependent",spy.maximumCycleWorkNs.load(std::memory_order_acquire)},
        {"maximumCycleNsIndependent",spy.maximumCycleNs.load(std::memory_order_acquire)},
        {"maximumPaceWaitNsIndependent",spy.maximumPaceWaitNs.load(std::memory_order_acquire)},
        {"captureActiveIndependent",spy.captureActive.load(std::memory_order_acquire)},
        {"captureStopIndependent",spy.captureStop.load(std::memory_order_acquire)},
        {"renderStopIndependent",spy.renderStop.load(std::memory_order_acquire)}};
    return r;
}
inline void reportControlWait(std::ostream &out,const ControlWaitContext &context,std::uint64_t elapsedNs,
    std::source_location caller) noexcept {
    try{out<<"native_control_wait_expired="<<controlWaitObservation(context,elapsedNs,caller).dump()<<'\n';}catch(...){}
}
template<class F> void controlAwait(const ControlWaitContext &context,F f,
    std::source_location caller=std::source_location::current()) {
    const auto begin=std::chrono::steady_clock::now(),deadline=begin+std::chrono::seconds(5);
    while(!f()) {
        const auto now=std::chrono::steady_clock::now();
        if(now>=deadline) {
            reportControlWait(std::cerr,context,std::uint64_t(std::chrono::duration_cast<std::chrono::nanoseconds>(now-begin).count()),caller);
            throw std::runtime_error("Native desktop control wait exceeded original five seconds");
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
}
}
