// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include "pipewire_monitor_capture.hpp"
#include "pipewire_monitor_render.hpp"
#include "spsc_queue.hpp"
namespace soundcurrent::daw {
enum class PipeWireReceiptCode { Accepted, Ready, Empty, Drained, Stopped, Fault };
enum class PipeWireReceiptCause { QueueFull, SequenceOverflow, ConsumerLost, InvalidCycle };
template<class Cycle> struct PipeWireReceiptRecord {
    std::uint64_t generation = 0,sequence = 0;
    Cycle original;
};
template<class Cycle> struct PipeWireReceiptFault {
    PipeWireReceiptCause cause;
    std::optional<PipeWireReceiptRecord<Cycle>> attempted;
};
struct PipeWireReceiptQueueOptions {
    std::uint64_t generation = 1,firstSequence = 0;
    std::uint32_t slots = 64;
    std::size_t memoryBudgetBytes = 2*1024*1024;
    std::optional<ResourceLedger> resources = {};
};
// Two independent native producers, ONE serialized journal consumer. Copies
// complete scalar facts after native returns into fixed 64-cell SPSC lanes.
// No clock fitting, joining, disk, allocation or waiting inside publication/read.
// Raw PCM banks remain independent; receipt refusal cannot erase captured audio.
// Join native producers before finish/retire; join consumer before destruction.
class PreparedPipeWireReceiptQueues {
  public:
    explicit PreparedPipeWireReceiptQueues(PipeWireReceiptQueueOptions = {});
    PipeWireReceiptCode publish(const PipeWireCaptureCycle &) noexcept;
    PipeWireReceiptCode publish(const PipeWireRenderCycle &) noexcept;
    PipeWireReceiptCode read(PipeWireReceiptRecord<PipeWireCaptureCycle> &) noexcept;
    PipeWireReceiptCode read(PipeWireReceiptRecord<PipeWireRenderCycle> &) noexcept;
    void finishCapture() noexcept; // Native capture producer, or joined control.
    void finishRender() noexcept;
    void consumerFailed() noexcept;
    bool consumerLost() const noexcept;
    std::optional<PipeWireReceiptFault<PipeWireCaptureCycle>> captureFault() const noexcept;
    std::optional<PipeWireReceiptFault<PipeWireRenderCycle>> renderFault() const noexcept;
    static bool captureSink(void *,const PipeWireCaptureCycle &) noexcept;
    static bool renderSink(void *,const PipeWireRenderCycle &) noexcept;
  private:
    template<class Cycle> struct Lane {
        SpscQueue<PipeWireReceiptRecord<Cycle>,64> queue;
        PipeWireReceiptFault<Cycle> fault{};
        std::uint64_t next;
        std::uint32_t published = 0;
        std::atomic<std::uint32_t> consumed{0};
        std::atomic<std::uint32_t> ended{0},faultReady{0};
        explicit Lane(std::uint64_t first):next(first){}
    };
    ResourceLease lease_;
    const PipeWireReceiptQueueOptions options_;
    std::unique_ptr<Lane<PipeWireCaptureCycle>> capture_;
    std::unique_ptr<Lane<PipeWireRenderCycle>> render_;
    std::atomic<std::uint32_t> lost_{0};
    template<class Cycle> PipeWireReceiptCode publish(Lane<Cycle> &,const Cycle &) noexcept;
    template<class Cycle> PipeWireReceiptCode read(Lane<Cycle> &,PipeWireReceiptRecord<Cycle> &) noexcept;
};
} // namespace soundcurrent::daw
