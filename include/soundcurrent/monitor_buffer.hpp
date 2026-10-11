// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include "resource_ledger.hpp"
#include <optional>
#include <span>

namespace soundcurrent::daw {
struct MonitorBufferConfig {
    std::uint32_t sourceRate = 48000, outputRate = 48000, channels = 1;
    std::uint32_t maximumInputFrames = 4096, inputCapacityFrames = 16384;
    std::uint32_t outputCapacityFrames = 512, retainedSourceFrames = 256;
    std::uint32_t maximumDriftPpm = 2000, maximumStepPpm = 1;
    std::uint64_t runGeneration = 1, outputFrameBase = 0;
    // outputFrameBase is an opaque stream ordinal, NOT a project/native clock.
    std::size_t memoryBudgetBytes = 256 * 1024 * 1024;
    std::optional<ResourceLedger> resources;
};
enum class MonitorBufferCode {
    Accepted, Priming, NeedInput, NeedOutput, NoOutput, OutputReady, Done, Cancelled,
    InvalidInput, InputFull, InvalidOutput, InvalidEvent, EventFull, InvalidEnd, Fault
};
enum class MonitorBufferFaultCode {
    NonfiniteInput, NonfiniteOutput, KernelFailure, KernelInvariant,
    CounterOverflow, InsufficientInput, InputOverrun, OutputUnderflow, ClockLost
};
struct MonitorRatioEvent {
    std::uint64_t runGeneration = 0, outputFrame = 0;
    double ratio = 1;
};
struct MonitorBufferFault {
    MonitorBufferFaultCode code;
    std::uint64_t acceptedInputFrames = 0, nextOutputFrame = 0;
    bool operator==(const MonitorBufferFault &) const = default;
};
struct MonitorBufferStatistics {
    std::uint64_t acceptedInputFrames = 0, consumedInputFrames = 0;
    std::uint64_t generatedOutputFrames = 0, deliveredOutputFrames = 0;
    std::uint64_t discardedMonitorFrames = 0, nextOutputFrame = 0;
    std::uint32_t queuedInputFrames = 0, queuedOutputFrames = 0, pendingGridFrames = 0;
    std::uint32_t pendingEvents = 0, warmupInputFrames = 0;
    double ratio = 1;
    bool primed = false, closing = false, ended = false, cancelled = false;
    bool operator==(const MonitorBufferStatistics &) const = default;
};
struct MonitorBufferRead {
    MonitorBufferCode code;
    std::uint32_t frames = 0;
};
std::size_t monitorBufferPayloadBytes(const MonitorBufferConfig &);
// One serialized processing owner; NOT concurrent control/capture/render access.
// Publish copied output through an external prepared SPSC route before rendering.
// Admission and destruction are off RT, after owners join. Prepared methods do
// bounded copying/kernel work, without allocations, locks, exceptions, I/O or log.
// Preserves finite float headroom. Raw samples/metadata must be routed separately
// before conversion; this class creates no capture/project/native clock mapping.
class PreparedMonitorBuffer {
  public:
    static constexpr std::uint32_t gridFrames = 64, eventCapacity = 64;
    explicit PreparedMonitorBuffer(MonitorBufferConfig);
    ~PreparedMonitorBuffer();
    PreparedMonitorBuffer(const PreparedMonitorBuffer &) = delete;
    PreparedMonitorBuffer &operator=(const PreparedMonitorBuffer &) = delete;
    // Copies whole packets. InputFull accepts nothing: the caller retains input
    // and can service/pull first. Actual loss must signal InputOverrun explicitly.
    MonitorBufferCode push(std::span<const float>) noexcept;
    // Ordered, unique, generation-bound grid events; range and step are checked
    // against the last queued ratio (or current ratio). Refusal is nonmutating.
    MonitorBufferCode enqueue(MonitorRatioEvent) noexcept;
    // At most one kernel call, one <=64-frame publication and bounded input span.
    MonitorBufferCode service() noexcept;
    // Copies available monitor PCM only; never emits silence or stale samples on
    // Priming/NoOutput/fault. NoOutput alone does not mean missing source input;
    // processing may still need service. Native deadlines decide OutputUnderflow.
    MonitorBufferRead pull(std::span<float>) noexcept;
    // Terminal explicit output-stream boundary. Uses remaining ACTUAL input,
    // releases the retained delay reservoir, and never adds future zero padding.
    // Insufficient postroll fails; this is not an exact raw/project alignment map.
    MonitorBufferCode finishAt(std::uint64_t exclusiveOutputFrame) noexcept;
    void cancel() noexcept; // Discard monitor-only backlog; preserve first fault/counts.
    void signalFault(MonitorBufferFaultCode) noexcept;
    MonitorBufferStatistics statistics() const noexcept;
    std::optional<MonitorBufferFault> firstFault() const noexcept;
    std::size_t chargedBytes() const noexcept;
  private:
    struct State;
    std::unique_ptr<State> state_;
};
} // namespace soundcurrent::daw
