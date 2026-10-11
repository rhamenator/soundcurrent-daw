// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include "wasapi_render.hpp"
#include "monitor_output_bank.hpp"
#include "monitor_clock_handoff.hpp"
#include "monitor_startup_handoff.hpp"
namespace soundcurrent::daw {
struct WasapiMonitorRenderOptions {
    std::size_t memoryBudgetBytes = 65536;
    std::optional<ResourceLedger> resources;
    std::optional<WasapiWarmupLimits> warmup = {}; // Declared before SDK/binding preparation.
};
enum class WasapiMonitorRenderCause { InvalidLease, Clock, Output, Underflow, FillAfterFinish, CounterOverflow, Startup };
struct WasapiMonitorRenderAttempt { std::uint32_t frames = 0; WasapiRenderClock original; };
struct WasapiMonitorRenderFault {
    WasapiMonitorRenderCause cause;
    std::optional<WasapiMonitorRenderAttempt> attempted;
};
enum class WasapiMonitorRenderStatus { Complete, Cancelled, Fault, NativeFailure, InvalidReceipt, Interrupted, OwnerLost };
struct WasapiMonitorRenderResult {
    WasapiMonitorRenderStatus status = WasapiMonitorRenderStatus::OwnerLost;
    bool cancelRequested = false;
    std::optional<WasapiRenderEnd> original;
    std::optional<WasapiMonitorRenderFault> firstFault;
    std::optional<std::int32_t> firstUnavailable;
    std::uint64_t callbackCalls = 0, requestedNativeFrames = 0, consumedMonitorFrames = 0;
    std::uint32_t finalSlackFrames = 0;
    std::optional<MonitorOutputOrdinal> monitorEnd; // Exclusive bank ordinal, NOT SDK/project/capture position.
    std::optional<WasapiWarmupLimits> warmupLimits = {};
    std::uint64_t preLeaseCalls = 0, readyAcquiredQpc100ns = 0;
    std::optional<MonitorStartupReady> startupReady = {}; // Original immutable evidence, even after terminal state.
    std::optional<MonitorStartupHandoffPhase> startupPhaseAtEnd = {};
};
// Prepare after actual SDK descriptor admission, before binding/activation.
// ONE render owner consumes the bank and produces original clock/end facts.
// External bank/clocks outlive all owners and joins. No routing/remapping/fitting.
class PreparedWasapiMonitorRender {
  public:
    PreparedWasapiMonitorRender(WasapiPreparedRenderFormat, PreparedMonitorOutputBank &,
        PreparedMonitorClockHandoff &, WasapiMonitorRenderOptions = {}, PreparedMonitorStartupHandoff * = nullptr);
    PreparedWasapiMonitorRender(const PreparedWasapiMonitorRender &) = delete;
    PreparedWasapiMonitorRender &operator=(const PreparedWasapiMonitorRender &) = delete;
    WasapiRenderAction fill(float *,std::uint32_t,const WasapiRenderClock &) noexcept;
    WasapiPreLeaseAction beforeLease(std::uint32_t,const WasapiRenderClock &,Qpc100ns) noexcept;
    void unavailable(std::int32_t) noexcept;
    void finished(const WasapiRenderEnd &) noexcept;
    bool shouldStop() const noexcept; // Render owner; content Finish does NOT request SDK Stop.
    void requestCancel() noexcept; // Serialized control, atomic intent only.
    bool bindable() const noexcept; // Control before ownership transfer.
    void settleAfterJoin() noexcept; // Missing SDK result remains OwnerLost.
    std::optional<WasapiMonitorRenderResult> result() const noexcept;
    std::optional<WasapiMonitorRenderFault> firstFault() const noexcept;
    std::optional<std::int32_t> firstUnavailable() const noexcept;
    const WasapiPreparedRenderFormat &format() const noexcept { return format_; }
    const std::optional<WasapiWarmupLimits> &warmupLimits() const noexcept { return warmup_; }
    std::size_t chargedBytes() const noexcept { return sizeof(*this); }
  private:
    ResourceLease lease_;
    const WasapiPreparedRenderFormat format_;
    PreparedMonitorOutputBank &bank_;
    PreparedMonitorClockHandoff &clocks_;
    PreparedMonitorStartupHandoff *const startup_;
    const std::optional<WasapiWarmupLimits> warmup_;
    WasapiMonitorRenderResult result_;
    WasapiMonitorRenderFault fault_{};
    std::int32_t unavailable_ = 0;
    std::atomic<std::uint32_t> resultReady_{0},faultReady_{0},unavailableReady_{0},cancel_{0};
    std::uint64_t calls_ = 0, requested_ = 0, consumed_ = 0;
    std::uint32_t slack_ = 0;
    bool finalReturned_ = false;
    std::optional<WasapiMonitorRenderAttempt> last_;
    std::optional<WasapiMonitorRenderAttempt> authorizedContent_;
    std::optional<MonitorStartupReady> acquiredReady_;
    std::uint64_t preLeaseCalls_ = 0, firstPreLeaseTime_ = 0, lastPreLeaseTime_ = 0,
        readyTime_ = 0, warmupRequests_ = 0, warmupRequestedFrames_ = 0;
    std::optional<WasapiWarmupLeaseResult> firstWarmupRequest_, lastWarmupRequest_;
    WasapiRenderAction fail(WasapiMonitorRenderCause,std::optional<WasapiMonitorRenderAttempt>) noexcept;
    WasapiRenderAction publishClock(const WasapiMonitorRenderAttempt &) noexcept;
    bool startupTerminal() const noexcept;
    void publish(std::optional<WasapiRenderEnd>,WasapiMonitorRenderStatus) noexcept;
};
enum class WasapiRenderBindingPhase : std::uint32_t { Unbound, Bound, Ending, Ended };
struct WasapiRenderBindingFault {
    bool afterEnd = false;
    WasapiMonitorRenderAttempt attempted; // Scalar metadata only, never native backing.
};
// Stable table exists before SDK stream construction. ONE serialized control
// bind races ONE SDK owner's exit. An unbound original end refuses activation.
class PreparedWasapiRenderBinding {
  public:
    explicit PreparedWasapiRenderBinding(WasapiMonitorRenderOptions = {});
    PreparedWasapiRenderBinding(const PreparedWasapiRenderBinding &) = delete;
    PreparedWasapiRenderBinding &operator=(const PreparedWasapiRenderBinding &) = delete;
    bool bind(PreparedWasapiMonitorRender &,WasapiPreparedRenderFormat actual);
    WasapiRenderCallbacks callbacks() noexcept;
    WasapiRenderBindingPhase phase() const noexcept;
    std::optional<WasapiRenderEnd> originalEnd() const noexcept;
    std::optional<WasapiRenderBindingFault> firstFault() const noexcept;
    std::optional<std::int32_t> firstUnavailable() const noexcept;
    void requestCancel() noexcept; // Serialized control, safe before bind.
    std::size_t chargedBytes() const noexcept { return sizeof(*this); }
  private:
    ResourceLease lease_;
    const std::optional<WasapiWarmupLimits> warmup_;
    PreparedWasapiMonitorRender *context_ = nullptr;
    WasapiRenderEnd end_;
    WasapiRenderBindingFault fault_{};
    std::int32_t unavailable_ = 0;
    std::atomic<std::uint32_t> phase_{0},faultReady_{0},unavailableReady_{0},cancel_{0};
    static WasapiRenderAction fill(void *,float *,std::uint32_t,const WasapiRenderClock &) noexcept;
    static void unavailable(void *,std::int32_t) noexcept;
    static void finished(void *,const WasapiRenderEnd &) noexcept;
    static bool shouldStop(void *) noexcept;
    static WasapiPreLeaseAction beforeLease(void *,std::uint32_t,const WasapiRenderClock &,Qpc100ns) noexcept;
};
class WasapiMonitorRenderStream {
  public:
    explicit WasapiMonitorRenderStream(WasapiRenderOptions,WasapiMonitorRenderOptions = {});
    ~WasapiMonitorRenderStream();
    WasapiMonitorRenderStream(const WasapiMonitorRenderStream &) = delete;
    WasapiMonitorRenderStream &operator=(const WasapiMonitorRenderStream &) = delete;
    WasapiPreparedRenderFormat preparedFormat() const noexcept;
    void bind(PreparedWasapiMonitorRender &);
    void activate();
    void requestStop() noexcept; // Cancel intent + OS notification, no join.
    void stop() noexcept;
    std::optional<WasapiRenderEnd> endAfterJoin() const noexcept;
    std::optional<WasapiRenderBindingFault> bindingFault() const noexcept;
  private:
    struct State;
    std::unique_ptr<State> state_;
};
} // namespace soundcurrent::daw
