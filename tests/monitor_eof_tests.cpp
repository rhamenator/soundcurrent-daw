// SPDX-License-Identifier: GPL-3.0-only
#include <soundcurrent/monitor_processing.hpp>
#include <array>
#include <atomic>
#include <chrono>
#include <iostream>
#include <thread>
using namespace soundcurrent::daw;
namespace {
std::atomic<bool> reached{false},released{false},timedOut{false};
unsigned checks=0;
void check(bool good,const char *message) {++checks;if(!good)throw std::runtime_error(message);}
constexpr std::uint64_t now=(std::uint64_t(1)<<54)+40000000;
void eofCase(bool withInput,bool lateEnd=false,bool planned=false) {
    CaptureFanoutConfig capture;
    capture.capture.identity={11,31,7};capture.capture.channels=3;
    capture.capture.maximumPacketFrames=64;capture.capture.slots=32;capture.monitorSlots=32;
    MonitorOutputBankConfig output;output.channels=3;output.slots=1;output.maximumPullFrames=64;
    output.first.value=std::uint64_t(1)<<54;
    MonitorProcessingConfig config;
    auto &b=config.buffer;b.channels=3;b.maximumInputFrames=64;b.inputCapacityFrames=2048;
    b.outputCapacityFrames=64;b.outputFrameBase=output.first.value;
    auto &d=config.drift;d.captureIdentity=capture.capture.identity;d.inputCapacityFrames=b.inputCapacityFrames;
    d.targetQueuedSourceFrames=1024;d.outputFrameBase=b.outputFrameBase;d.occupancyGainPpmPerFrame=0;
    PreparedCaptureFanout fanout(capture);PreparedMonitorOutputBank out(output);PreparedMonitorProcessing processing(fanout,out,config);
    const auto end=output.first.value+(withInput ? 512 : 0);
    if(planned)check(processing.planEnd({end}),"Planned EOF horizon refused");
    if(withInput) {
        const auto origin=std::uint64_t(1)<<63;
        for(unsigned n=0;n<3;++n) {
            const auto q=now-40000000+std::uint64_t(n)*20000000;
            processing.observe(CaptureClockReading{d.captureIdentity,{origin+n*96000},{q}});
            processing.observe(RenderClockReading{d.renderIdentity,{origin+n*96000},{q},48000});
        }
        std::array<float,192> data{};
        for(unsigned n=0;n<1024;n+=64) {
            const CaptureBankStamp stamp{capture.capture.identity,{origin+n},{now+n*211},64,0};
            const auto result=fanout.publish({stamp,reinterpret_cast<const std::byte *>(data.data()),sizeof(data)});
            check(result.rawAcceptedFrames==64 && result.monitorAcceptedFrames==64,"EOF fixture capture refused");
        }
        for(unsigned i=0;i<128 && processing.statistics().buffer.acceptedInputFrames<1024;++i) {
            processing.step({now});out.pull(data);
        }
        check(processing.statistics().buffer.acceptedInputFrames==1024,"EOF fixture input not consumed");
    }
    if(lateEnd) {
        // With no declared horizon, live processing may pass a future EOF request.
        std::array<float,192> scratch{};
        for(unsigned i=0;i<128 && out.deliveredFrames()<=512;++i) {
            processing.step({now});out.pull(scratch);
        }
        check(out.deliveredFrames()>512,"Late-end fixture did not pass requested frame");
    }
    reached=false;released=false;timedOut=false;
    std::thread producer([&]{fanout.finish();});
    const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(10);
    while(!reached.load(std::memory_order_acquire) && std::chrono::steady_clock::now()<deadline)std::this_thread::yield();
    const bool window=reached.load() && !fanout.producerDone() && fanout.monitorDrained();
    bool premature=false,prematureClosing=false;std::array<float,192> scratch{};
    for(unsigned i=0;i<128 && window;++i) {
        const auto code=processing.step({now});out.pull(scratch);
        const auto state=processing.statistics().buffer;
        prematureClosing=prematureClosing || state.closing || state.ended;
        if(code==MonitorProcessingCode::NeedEnd){premature=true;break;}
        if(code==MonitorProcessingCode::NeedInput)break;
    }
    released.store(true,std::memory_order_release);producer.join();
    check(window && !timedOut,"EOF test did not hold the publication window");
    check(!premature,"NeedEnd escaped before aggregate capture EOF publication");
    check(!prematureClosing,"Buffer finalized before aggregate capture EOF publication");
    if(planned) {
        bool done=false;
        for(unsigned i=0;i<128;++i) {
            const auto code=processing.step({now});out.pull(scratch);
            check(code!=MonitorProcessingCode::NeedEnd && code!=MonitorProcessingCode::Fault,
                  "Planned EOF did not finalize automatically");
            if(code==MonitorProcessingCode::Done){done=true;break;}
        }
        const auto state=processing.statistics().buffer;
        check(done && fanout.producerDone() && state.closing && state.ended,
              "Planned EOF did not complete after aggregate publication");
        check(state.nextOutputFrame==end && out.deliveredFrames()==end-output.first.value,
              "Planned EOF did not stop at the declared horizon");
        return;
    }
    bool needEnd=false;
    for(unsigned i=0;i<128;++i) {
        const auto code=processing.step({now});out.pull(scratch);
        if(code==MonitorProcessingCode::NeedEnd){needEnd=true;break;}
    }
    check(needEnd && fanout.producerDone(),"NeedEnd unavailable after aggregate capture EOF");
    if(withInput) {
        const auto accepted=processing.finishAt({output.first.value+512});
        check(accepted!=lateEnd,lateEnd ? "Retrospective final end was silently accepted" : "Final end refused after aggregate EOF");
    }
}
}
extern "C" void scCaptureFanoutAfterMonitorFinish() noexcept {
    reached.store(true,std::memory_order_release);
    const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(10);
    while(!released.load(std::memory_order_acquire)) {
        if(std::chrono::steady_clock::now()>=deadline){timedOut=true;break;}
        std::this_thread::yield();
    }
}
int main() {
    try {eofCase(true);eofCase(false);eofCase(true,true);eofCase(true,false,true);eofCase(false,false,true);std::cout<<checks<<" deterministic aggregate EOF checks passed\n";return 0;}
    catch(const std::exception &e){std::cerr<<e.what()<<'\n';return 1;}
}
