// SPDX-License-Identifier: GPL-3.0-only
#include <soundcurrent/pipewire_monitor_render_stream.hpp>
#include <soundcurrent/pipewire_receipt_queues.hpp>
#include <soundcurrent/pipewire_receipt_codec.hpp>
#include "rt_audit.hpp"
#include <algorithm>
#include <bit>
#include <cstdlib>
#include <iostream>
#include <thread>
using namespace soundcurrent::daw;
namespace {
constexpr std::uint64_t frames=96123,first=(std::uint64_t(1)<<54)+37;
float sample(std::uint64_t ordinal,unsigned channel){return 3.25F+float(ordinal&65535U)/16384+float(channel)*4;}
void require(bool ok,const char *why){if(!ok)throw std::runtime_error(why);}
struct Audit{std::atomic<bool> bad{false};std::atomic<std::uint64_t> callbacks{0};};
void begin(void *) noexcept {rt_audit::active=true;}
void end(void *ctx) noexcept {
    auto &a=*static_cast<Audit *>(ctx);const auto c=rt_audit::counts;
    if(c.cppAllocate || c.cppFree || c.cAllocate || c.cFree || c.blockingLock)a.bad.store(true);
    ++a.callbacks;rt_audit::active=false;
}
struct Sink {
    std::atomic<std::uint64_t> received{0},padding{0};std::atomic<bool> bad{false};
    static void process(void *ctx,const DeviceBlockClock &clock,std::span<const float *const> in,
                        std::span<float *const>,std::uint32_t capacity) noexcept {
        auto &s=*static_cast<Sink *>(ctx);
        if(clock.duration>capacity || in.size()!=3 || !in[0] || !in[1] || !in[2])return;
        auto at=s.received.load(std::memory_order_relaxed);
        for(std::uint32_t frame=0;frame<clock.duration;++frame) {
            if(!at && in[0][frame]==0 && in[1][frame]==0 && in[2][frame]==0)continue;
            if(at<frames) {
                for(unsigned channel=0;channel<3;++channel)
                    if(std::bit_cast<unsigned>(in[channel][frame])!=std::bit_cast<unsigned>(sample(first+at,channel)))s.bad.store(true);
                ++at;
            } else {
                for(auto *plane:in)if(plane[frame]!=0)s.bad.store(true);
                ++s.padding;
            }
        }
        s.received.store(at,std::memory_order_release);
    }
};
}
int main(){try{
    require(std::getenv("SC_OWNED_PIPEWIRE_RENDER") && std::string(std::getenv("SC_OWNED_PIPEWIRE_RENDER"))=="1","Opt-in private owned fixture required");
    Sink sink;Audit renderAudit;
    PipeWireFilter receiver({"sc-native-render-sink-"+Id::generate().str(),3,0,2048,true},{&sink,Sink::process});
    require(receiver.waitReady(std::chrono::seconds(3)),"Owned sink ports not ready");
    MonitorOutputBankConfig bc;bc.first={first};bc.channels=3;bc.maximumBlockFrames=2048;bc.maximumPullFrames=2048;bc.slots=64;
    PreparedMonitorOutputBank bank(bc);MonitorClockHandoffConfig hc;hc.timestampDomain=MonitorClockDomain::PosixMonotonic100ns;
    PreparedMonitorClockHandoff clocks(hc);
    std::vector<float> block(2048*3);
    for(std::uint64_t offset=0;offset<frames;) {
        const auto count=std::uint32_t(std::min<std::uint64_t>(2048,frames-offset));
        for(unsigned f=0;f<count;++f)for(unsigned c=0;c<3;++c)block[std::size_t(f)*3+c]=sample(first+offset+f,c);
        require(bank.publish({1,{first+offset},count},std::span<const float>(block).first(std::size_t(count)*3))==MonitorOutputCode::Accepted,"Owned finite producer refused");offset+=count;
    }
    PreparedPipeWireReceiptQueues receipts;
    bank.finish();PipeWireMonitorRenderStreamOptions options;options.auditContext=&renderAudit;options.beginCallback=begin;options.endCallback=end;
    options.render.receiptContext=&receipts;options.render.receipt=PreparedPipeWireReceiptQueues::renderSink;
    PipeWireMonitorRenderStream render(bank,clocks,std::move(options));
    const auto all=render.ports();std::vector<AudioPort> selected;
    for(unsigned channel=0;channel<3;++channel) {
        const auto name="input_"+std::to_string(channel+1);
        const auto it=std::find_if(all.begin(),all.end(),[&](const auto &p){return p.nodeId==receiver.nodeId() && p.input && p.portName==name;});
        require(it!=all.end(),"Owned destination channel missing");selected.push_back(*it);
    }
    render.connectOutputs(selected);receiver.activate();render.activate();
    std::uint64_t readings=0,cycles=0,queued=0;const auto until=std::chrono::steady_clock::now()+std::chrono::seconds(8);
    const auto drainReceipts=[&]{PipeWireReceiptRecord<PipeWireRenderCycle> event;
        while(receipts.read(event)==PipeWireReceiptCode::Ready){require(event.sequence==cycles && event.original.allQueued &&
            event.original.returnClock==event.original.original,"Output receipt sequence/facts differ");
            const auto bytes=encodePipeWireReceipt(event);const auto decoded=std::get<PipeWireReceiptRecord<PipeWireRenderCycle>>(decodePipeWireReceipt(bytes));
            require(decoded.original.original==event.original.original && decoded.original.returns[2]==event.original.returns[2],"Per-packet versioned render provenance changed");
            ++cycles;queued+=event.original.monitorFrames;}};
    while((!render.contentComplete() || sink.received.load()<frames || !sink.padding.load()) && std::chrono::steady_clock::now()<until) {
        MonitorClockRecord<RenderClockReading> reading;while(clocks.read(reading).code==MonitorClockCode::Ready)++readings;
        drainReceipts();
        if(render.firstFault())break;
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    render.stop();receipts.finishRender();drainReceipts();receiver.stop();const auto receipt=render.endAfterJoin();
    if(receipt && receipt->fault)std::cerr<<"native_render_fault cause="<<int(receipt->fault->cause)<<" consumed="<<receipt->monitorConsumedFrames<<" queued="<<receipt->monitorQueuedFrames<<'\n';
    require(receipt && receipt->contentComplete && receipt->callbacksJoined && !receipt->fault && !receipt->unavailable,"Genuine render ended with fault/incomplete content");
    require(receipt->monitorConsumedFrames==frames && receipt->monitorQueuedFrames==frames && sink.received.load()==frames && !sink.bad.load(),"Native queued/sink PCM extent or bits differ");
    require(cycles==receipt->cycles && queued==frames,"Per-cycle native return receipts lost");
    require(sink.padding.load()>=133 && receipt->lastContent->requestedFrames==256 && receipt->lastContent->monitorFrames==123,"Final native padding not observed");
    require(!renderAudit.bad.load(),"Actual render callbacks allocated/freed/locked");
    std::cout<<"owned_pipewire_render monitor_consumed="<<receipt->monitorConsumedFrames<<" monitor_queued="<<receipt->monitorQueuedFrames
             <<" native_queued="<<receipt->nativeQueuedFrames<<" sink_received="<<sink.received<<" sink_padding="<<sink.padding<<" clock_readings="<<readings
             <<" exact_queue_result="<<*receipt->lastContent->returns[0].queueResult<<" content_complete_after_return=true"
             <<" final_monitor_frames=123 final_padding_frames=133 float_bit_checks="<<frames*3<<" receipt_cycles="<<cycles
             <<" receipt_monitor_queued="<<queued<<" callback_allocate_free_lock=0 physical_audio=false private_owned_daemon=true\n";
    return 0;
}catch(const std::exception &e){std::cerr<<e.what()<<'\n';return 1;}}
