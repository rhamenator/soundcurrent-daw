// SPDX-License-Identifier: GPL-3.0-only
#include <soundcurrent/pipewire_monitor_capture_stream.hpp>
#include <soundcurrent/pipewire_receipt_queues.hpp>
#include <soundcurrent/pipewire_receipt_codec.hpp>
#include "rt_audit.hpp"
#include <array>
#include <algorithm>
#include <bit>
#include <cstdlib>
#include <iostream>
#include <thread>
using namespace soundcurrent::daw;
namespace {
float sample(std::uint64_t position, unsigned channel) {
    return float(position & 65535U) / 32768 - 1 + float(channel) * 4;
}
void require(bool b,const char *why){if(!b)throw std::runtime_error(why);}
struct Audit { std::atomic<bool> bad{false};std::atomic<std::uint64_t> callbacks{0}; };
void begin(void *) noexcept {rt_audit::active=true;}
void end(void *ctx) noexcept {
    auto &a=*static_cast<Audit *>(ctx);const auto c=rt_audit::counts;
    if(c.cppAllocate || c.cppFree || c.cAllocate || c.cFree || c.blockingLock)a.bad.store(true);
    ++a.callbacks;rt_audit::active=false;
}
void generate(void *,const DeviceBlockClock &c,std::span<const float *const>,
              std::span<float *const> out,std::uint32_t capacity) noexcept {
    if(c.duration>capacity)return;
    for(unsigned ch=0;ch<out.size();++ch)if(out[ch])
        for(std::uint32_t f=0;f<c.duration;++f)out[ch][f]=sample(c.position+f,ch);
}
}
int main() {
    try {
        require(std::getenv("SC_OWNED_PIPEWIRE_CAPTURE") && std::string(std::getenv("SC_OWNED_PIPEWIRE_CAPTURE"))=="1","Opt-in owned private PipeWire fixture required");
        Audit sourceAudit,captureAudit;
        PipeWireFilter source({"sc-native-capture-source-"+Id::generate().str(),0,3,2048,true},
                              {&sourceAudit,generate,nullptr,begin,end});
        require(source.waitReady(std::chrono::seconds(3)),"Owned source ports not ready");
        CaptureFanoutConfig fc;fc.capture.identity={42,1,1};fc.capture.channels=3;
        fc.capture.maximumPacketFrames=2048;fc.capture.slots=64;fc.monitorSlots=64;
        PreparedCaptureFanout f(fc);MonitorClockHandoffConfig hc;hc.captureIdentity=fc.capture.identity;
        hc.timestampDomain=MonitorClockDomain::PosixMonotonic100ns;PreparedMonitorClockHandoff h(hc);
        PreparedPipeWireReceiptQueues receipts;
        PipeWireMonitorCaptureStreamOptions options;options.auditContext=&captureAudit;
        options.capture.receiptContext=&receipts;options.capture.receipt=PreparedPipeWireReceiptQueues::captureSink;
        options.beginCallback=begin;options.endCallback=end;
        PipeWireMonitorCaptureStream capture(f,h,std::move(options));
        std::array<AudioPort,3> selected;
        const auto ports=capture.ports();
        for(unsigned channel=0;channel<3;++channel){
            const auto name="output_"+std::to_string(channel+1);
            const auto it=std::find_if(ports.begin(),ports.end(),[&](const auto &p){return p.nodeId==source.nodeId() && !p.input && p.portName==name;});
            require(it!=ports.end(),"Owned source channel missing");selected[channel]=*it;
        }
        capture.connectInputs({selected.begin(),selected.end()});
        source.activate();capture.activate();
        std::uint64_t raw=0,monitor=0,readings=0,samples=0,cycles=0,returned=0,receiptRaw=0;
        const auto drain=[&] {
            for(unsigned side=0;side<2;++side)for(;;){
                CaptureBankView v;const auto code=side?f.acquireMonitor(v):f.acquireRaw(v);
                if(code!=CaptureBankRead::Ready)break;
                for(unsigned frame=0;frame<v.token.original.frames;++frame)for(unsigned channel=0;channel<3;++channel){
                    require(std::bit_cast<std::uint32_t>(v.interleaved[std::size_t(frame)*3+channel])==std::bit_cast<std::uint32_t>(sample(v.token.original.position.value+frame,channel)),"Real planar callback changed raw float bits/position/channel order");++samples;
                }
                (side?monitor:raw)+=v.token.original.frames;
                require(side?f.releaseMonitor(v):f.releaseRaw(v),"Owned packet release failed");
            }
            MonitorClockRecord<CaptureClockReading> clock;
            while(h.read(clock).code==MonitorClockCode::Ready){require(clock.original.identity==fc.capture.identity && clock.original.qpc.value,"Real capture clock missing");++readings;}
            PipeWireReceiptRecord<PipeWireCaptureCycle> event;
            while(receipts.read(event)==PipeWireReceiptCode::Ready){
                require(event.sequence==cycles && event.original.returnClock==event.original.original &&
                    event.original.allReturned && event.original.returns[0].queueResult==0,"Actual native input return facts differ");
                const auto bytes=encodePipeWireReceipt(event);
                const auto decoded=std::get<PipeWireReceiptRecord<PipeWireCaptureCycle>>(decodePipeWireReceipt(bytes));
                require(decoded.original.original==event.original.original && decoded.original.returns[2]==event.original.returns[2],"Per-packet versioned capture provenance changed");
                ++cycles;returned+=event.original.requestedFrames;receiptRaw+=event.original.rawFrames;
            }
        };
        const auto until=std::chrono::steady_clock::now()+std::chrono::seconds(8);
        while(raw<96000 && std::chrono::steady_clock::now()<until){drain();std::this_thread::sleep_for(std::chrono::milliseconds(1));}
        capture.stop();receipts.finishCapture();drain();source.stop();const auto receipt=capture.endAfterJoin();
        if(receipt && (receipt->fault || receipt->unavailable)) {
            std::cerr<<"native_fault cause="<<(receipt->fault ? int(receipt->fault->cause) : -1)
                     <<" unavailable="<<(receipt->unavailable ? int(*receipt->unavailable) : -1)
                     <<" raw="<<receipt->acceptedRawFrames<<" monitor="<<receipt->acceptedMonitorFrames;
            if(receipt->fault && receipt->fault->original){const auto &v=*receipt->fault->original;
                std::cerr<<" pos="<<v.position<<" duration="<<v.duration<<" nsec="<<v.monotonicNs<<" id="<<v.id<<" cycle="<<v.cycle
                         <<" num="<<v.rateNumerator<<" denom="<<v.rateDenominator<<" xrun="<<v.xrun<<" discont="<<v.discontinuity;
            }
            if(receipt->lastAccepted)std::cerr<<" previous_cycle="<<receipt->lastAccepted->cycle<<" previous_pos="<<receipt->lastAccepted->position;
            std::cerr<<'\n';
        }
        require(receipt && receipt->callbacksJoined && !receipt->fault && !receipt->unavailable,"Native capture ended with fault");
        require(raw>=96000 && raw==monitor && raw==receipt->acceptedRawFrames && readings==receipt->acceptedPackets,"Owned native counts differ");
        require(cycles==receipt->completedCycles && receiptRaw==raw && returned>=raw && returned==receipt->nativeReturnedFrames,"Original returned/captured cycles lost");
        require(!sourceAudit.bad.load() && !captureAudit.bad.load(),"Actual PipeWire callbacks allocated/freed/locked");
        require(f.producerDone() && h.captureDone(),"Native join did not publish EOF");
        std::cout<<"owned_pipewire_capture raw_frames="<<raw<<" monitor_frames="<<monitor<<" packets="<<readings<<" bit_checks="<<samples
                 <<" original_nsec_first="<<receipt->firstAccepted->monotonicNs<<" original_nsec_last="<<receipt->lastAccepted->monotonicNs
                 <<" graph_clock="<<receipt->firstAccepted->id<<" graph_cycle="<<receipt->firstAccepted->cycle
                 <<" source_callbacks="<<sourceAudit.callbacks<<" capture_callbacks="<<captureAudit.callbacks
                 <<" returned_frames="<<returned<<" receipt_cycles="<<cycles<<" exact_input_queue_result=0"
                 <<" callback_allocate_free_lock=0 native_daemon=true private_owned_route=true physical_audio=false monitor_render=false\n";
        return 0;
    } catch(const std::exception &e){std::cerr<<e.what()<<'\n';return 1;}
}
