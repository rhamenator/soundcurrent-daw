// SPDX-License-Identifier: GPL-3.0-only
#include <soundcurrent/pipewire_receipt_queues.hpp>
namespace soundcurrent::daw {
namespace {
bool valid(const PipeWireCaptureCycle &c){return c.channels && c.channels<=256 && c.returnedChannels<=256 && c.returnClock.has_value();}
bool valid(const PipeWireRenderCycle &c){return c.channels && c.channels<=256 && c.returnedChannels<=256 && c.returnClock.has_value();}
}
PreparedPipeWireReceiptQueues::PreparedPipeWireReceiptQueues(PipeWireReceiptQueueOptions o)
    :lease_([&]{
        if(!o.generation || !o.slots || o.slots>64 || o.firstSequence==UINT64_MAX)
            throw ProjectError(ErrorCode::InvalidParameter,"PipeWire receipt queue admission mismatch");
        PayloadCharge charge("Native PipeWire receipt lanes",o.memoryBudgetBytes);
        charge.add(sizeof(PreparedPipeWireReceiptQueues));charge.add(sizeof(Lane<PipeWireCaptureCycle>));charge.add(sizeof(Lane<PipeWireRenderCycle>));
        return o.resources?o.resources->reserve(charge.bytes()):ResourceLease{};
    }()),options_(std::move(o)),capture_(std::make_unique<Lane<PipeWireCaptureCycle>>(options_.firstSequence)),
    render_(std::make_unique<Lane<PipeWireRenderCycle>>(options_.firstSequence)) {}
template<class Cycle> PipeWireReceiptCode PreparedPipeWireReceiptQueues::publish(Lane<Cycle> &lane,const Cycle &cycle) noexcept {
    if(lane.ended.load(std::memory_order_acquire))return PipeWireReceiptCode::Stopped;
    if(lane.faultReady.load(std::memory_order_relaxed))return PipeWireReceiptCode::Fault;
    const PipeWireReceiptRecord<Cycle> attempted{options_.generation,lane.next,cycle};
    const auto fail=[&](PipeWireReceiptCause cause){lane.fault={cause,attempted};lane.faultReady.store(1,std::memory_order_release);return PipeWireReceiptCode::Fault;};
    if(lost_.load(std::memory_order_acquire))return fail(PipeWireReceiptCause::ConsumerLost);
    if(!valid(cycle))return fail(PipeWireReceiptCause::InvalidCycle);
    if(lane.next==UINT64_MAX)return fail(PipeWireReceiptCause::SequenceOverflow);
    if(lane.published-lane.consumed.load(std::memory_order_acquire)>=options_.slots)return fail(PipeWireReceiptCause::QueueFull);
    if(!lane.queue.tryPush(attempted))return fail(PipeWireReceiptCause::QueueFull);
    ++lane.published;++lane.next;return PipeWireReceiptCode::Accepted;
}
template<class Cycle> PipeWireReceiptCode PreparedPipeWireReceiptQueues::read(Lane<Cycle> &lane,PipeWireReceiptRecord<Cycle> &out) noexcept {
    if(lost_.load(std::memory_order_acquire))return PipeWireReceiptCode::Fault;
    if(lane.queue.tryPop(out)){lane.consumed.fetch_add(1,std::memory_order_release);return PipeWireReceiptCode::Ready;}
    const bool fault=lane.faultReady.load(std::memory_order_acquire)!=0,ended=lane.ended.load(std::memory_order_acquire)!=0;
    // Terminal publication follows the last queue publication. A first empty
    // read may race that publication; acquire terminal before checking again.
    if((fault || ended) && lane.queue.tryPop(out)){lane.consumed.fetch_add(1,std::memory_order_release);return PipeWireReceiptCode::Ready;}
    if(fault)return PipeWireReceiptCode::Fault;
    return ended?PipeWireReceiptCode::Drained:PipeWireReceiptCode::Empty;
}
PipeWireReceiptCode PreparedPipeWireReceiptQueues::publish(const PipeWireCaptureCycle &c) noexcept{return publish(*capture_,c);}
PipeWireReceiptCode PreparedPipeWireReceiptQueues::publish(const PipeWireRenderCycle &c) noexcept{return publish(*render_,c);}
PipeWireReceiptCode PreparedPipeWireReceiptQueues::read(PipeWireReceiptRecord<PipeWireCaptureCycle> &c) noexcept{return read(*capture_,c);}
PipeWireReceiptCode PreparedPipeWireReceiptQueues::read(PipeWireReceiptRecord<PipeWireRenderCycle> &c) noexcept{return read(*render_,c);}
void PreparedPipeWireReceiptQueues::finishCapture() noexcept{capture_->ended.store(1,std::memory_order_release);}
void PreparedPipeWireReceiptQueues::finishRender() noexcept{render_->ended.store(1,std::memory_order_release);}
void PreparedPipeWireReceiptQueues::consumerFailed() noexcept{lost_.store(1,std::memory_order_release);}
bool PreparedPipeWireReceiptQueues::consumerLost() const noexcept{return lost_.load(std::memory_order_acquire)!=0;}
bool PreparedPipeWireReceiptQueues::captureFinishedAndEmpty() const noexcept{return capture_->ended.load(std::memory_order_acquire) && !capture_->queue.consumerAvailable();}
bool PreparedPipeWireReceiptQueues::renderFinishedAndEmpty() const noexcept{return render_->ended.load(std::memory_order_acquire) && !render_->queue.consumerAvailable();}
std::optional<PipeWireReceiptFault<PipeWireCaptureCycle>> PreparedPipeWireReceiptQueues::captureFault() const noexcept {
    return capture_->faultReady.load(std::memory_order_acquire)?std::optional(capture_->fault):std::nullopt;
}
std::optional<PipeWireReceiptFault<PipeWireRenderCycle>> PreparedPipeWireReceiptQueues::renderFault() const noexcept {
    return render_->faultReady.load(std::memory_order_acquire)?std::optional(render_->fault):std::nullopt;
}
bool PreparedPipeWireReceiptQueues::captureSink(void *ctx,const PipeWireCaptureCycle &c) noexcept{return static_cast<PreparedPipeWireReceiptQueues *>(ctx)->publish(c)==PipeWireReceiptCode::Accepted;}
bool PreparedPipeWireReceiptQueues::renderSink(void *ctx,const PipeWireRenderCycle &c) noexcept{return static_cast<PreparedPipeWireReceiptQueues *>(ctx)->publish(c)==PipeWireReceiptCode::Accepted;}
} // namespace soundcurrent::daw
