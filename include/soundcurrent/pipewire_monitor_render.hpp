// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include "pipewire_cycle.hpp"
#include "monitor_clock_handoff.hpp"
#include "monitor_output_bank.hpp"
#include "monitor_startup_handoff.hpp"
#include <array>
namespace soundcurrent::daw {
enum class PipeWireRenderCause {
    InvalidBuffers, InvalidQuantum, RateChanged, MissingTimestamp, ClockChanged,
    CycleDiscontinuity, PositionJump, TimestampReset, PositionOverflow, ClockRejected,
    Underflow, ProducerFault, WarmupLimit, NativeUnavailable, MissingReturn,
    InvalidReturn, QueueFailure, CounterOverflow, ReceiptPressure, Startup
};
struct PipeWireRenderFault {
    PipeWireRenderCause cause;
    std::optional<DeviceBlockClock> original;
    std::optional<std::uint32_t> channel;
    std::optional<PipeWirePortLeaseResult> nativeReturn;
};
struct PipeWireRenderCycle {
    DeviceBlockClock original;
    std::optional<DeviceBlockClock> returnClock; // Actual receipt; separately bound/checked.
    std::uint32_t requestedFrames = 0, monitorFrames = 0, channels = 0;
    std::uint64_t returnedChannels = 0;
    bool warmup = false, finalContent = false, allQueued = false;
    std::array<PipeWirePortLeaseResult,256> returns{};
};
struct PipeWireRenderEnd {
    std::optional<PipeWireRenderCycle> first, last, lastContent, faultCycle;
    std::optional<PipeWireRenderCycle> completion; // First valid final return, survives later silent callbacks.
    std::optional<PipeWireRenderFault> fault;
    std::optional<AudioBridgeStatus> unavailable;
    std::uint64_t cycles = 0, monitorConsumedFrames = 0, monitorQueuedFrames = 0,
                  nativeQueuedFrames = 0, warmupQueuedFrames = 0, silentQueuedFrames = 0;
    bool contentComplete = false, callbacksJoined = false, stopRequested = false;
};
struct PipeWireMonitorRenderOptions {
    std::uint64_t maximumWarmupFrames = 0; // Zero: six seconds at declared graph rate.
    std::uint64_t maximumWarmupNs = 5000000000ULL;
    std::size_t memoryBudgetBytes = 256*1024*1024;
    std::optional<ResourceLedger> resources;
    void *receiptContext = nullptr;
    bool (*receipt)(void *,const PipeWireRenderCycle &) noexcept = nullptr; // Bounded RT-safe handoff.
    PreparedMonitorStartupHandoff *startup = nullptr; // Outlives joined processing/render owners.
};
enum class PipeWireRenderCode { Content, Warmup, Complete, Stopped, Fault };
// One native process/return owner. Interleaved bank -> planar native buffers;
// float headroom retained. completeCycle receives actual post-return SDK facts.
// Queued monitor frames never mean audible frames or a project clock anchor.
class PreparedPipeWireMonitorRender {
  public:
    PreparedPipeWireMonitorRender(PreparedMonitorOutputBank &, PreparedMonitorClockHandoff &,
                                 PipeWireMonitorRenderOptions = {});
    PipeWireRenderCode process(const DeviceBlockClock &, std::span<float *const>,
                              std::uint32_t bufferFrames, std::optional<Qpc100ns> monotonicNow = {}) noexcept;
    // Startup requires independent current CLOCK_MONOTONIC floor(ns/100),
    // despite legacy type spelling. Graph timestamp is preserved separately.
    void completeCycle(const PipeWireCycleReceipt &) noexcept;
    void requestStop() noexcept;
    void unavailable(AudioBridgeStatus) noexcept;
    bool contentComplete() const noexcept;
    std::optional<MonitorStartupReady> startupReadyAfterJoin() const noexcept; // Joined native owner only.
    std::optional<PipeWireRenderFault> firstFault() const noexcept;
    std::optional<AudioBridgeStatus> firstUnavailable() const noexcept;
    void finishAfterJoin() noexcept;
    std::optional<PipeWireRenderEnd> endAfterJoin() const noexcept;
  private:
    ResourceLease lease_;
    PreparedMonitorOutputBank &bank_;
    PreparedMonitorClockHandoff &clocks_;
    const PipeWireMonitorRenderOptions options_;
    const std::uint64_t maximumWarmupFrames_;
    std::vector<float> interleaved_;
    PipeWireRenderEnd end_;
    PipeWireRenderCycle pending_{};
    PipeWireRenderFault fault_{};
    std::optional<DeviceBlockClock> previous_;
    std::atomic<std::uint32_t> stopped_{0}, faultReady_{0}, unavailable_{0}, complete_{0};
    std::uint64_t warmupFrames_ = 0, warmupStartNs_ = 0;
    bool pendingReturn_ = false, live_ = false, joined_ = false;
    std::optional<MonitorStartupReady> acquiredReady_;
    PipeWireRenderCode fail(PipeWireRenderCause, std::optional<DeviceBlockClock>,
        std::optional<std::uint32_t> = {}, std::optional<PipeWirePortLeaseResult> = {}) noexcept;
    void silence(std::span<float *const>,std::uint32_t) noexcept;
};
} // namespace soundcurrent::daw
