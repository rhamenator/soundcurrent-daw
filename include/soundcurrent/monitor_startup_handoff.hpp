// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include "capture_bank.hpp"
#include "resource_ledger.hpp"
#include <atomic>
#include "monitor_channel_map.hpp"
#include "monitor_eq.hpp"
namespace soundcurrent::daw {
enum class MonitorStartupPhase { Disabled, Observing, Transferring, Live };
struct MonitorStartupSource {
    CaptureBankStamp original;
    std::uint32_t packetOffsetFrames = 0;
    bool operator==(const MonitorStartupSource &) const = default;
};
struct MonitorStartupStatistics {
    MonitorStartupPhase phase = MonitorStartupPhase::Disabled;
    std::uint64_t acceptedFrames = 0, discardedFrames = 0, transferredFrames = 0, abandonedFrames = 0;
    std::uint32_t retainedFrames = 0, capacityFrames = 0;
    // Kernel context begins here; this is NOT a first-audible-frame/alignment map.
    std::optional<MonitorStartupSource> firstTransferred;
    std::optional<CaptureBankStamp> lastAccepted;
    bool contentReady = false;
    bool operator==(const MonitorStartupStatistics &) const = default;
};

struct MonitorStartupAdmission {
    std::uint64_t runGeneration = 0;
    ClockStreamIdentity captureIdentity, renderIdentity;
    std::uint32_t sourceRate = 0, outputRate = 0, channels = 0;
    std::uint64_t renderFrequency = 0;
    std::uint32_t nativeCapacityFrames = 0, maximumNativeFillFrames = 0;
    std::uint64_t maximumWait100ns = 0;
    std::optional<MonitorChannelMapReceipt> channelMap = {};
    std::optional<MonitorEqReceipt> equalizer = {};
    bool operator==(const MonitorStartupAdmission &) const = default;
};
struct MonitorStartupReady {
    MonitorStartupAdmission admitted;
    MonitorStartupStatistics original;
    std::uint64_t publishedFrames = 0, asOfQpc100ns = 0;
    bool operator==(const MonitorStartupReady &) const = default;
};
enum class MonitorStartupHandoffPhase { Pending, Ready, Fault, InputEnd, Cancelled, OwnerLost };
// ONE processing producer publishes once. A native consumer/control can acquire
// immutable evidence; it never reads processing statistics. Control cancellation
// is atomic intent and may race one in-flight publication. Ready evidence remains
// immutable even after terminal failure/cancel. The external output bank, clock
// handoff and cancel/fault checks remain necessary before native content pulls.
// Prepare/retire off RT; all owners join before destruction. No reset/reuse.
class PreparedMonitorStartupHandoff {
  public:
    explicit PreparedMonitorStartupHandoff(MonitorStartupAdmission,
        std::size_t memoryBudgetBytes = 65536, std::optional<ResourceLedger> = {});
    PreparedMonitorStartupHandoff(const PreparedMonitorStartupHandoff &) = delete;
    PreparedMonitorStartupHandoff &operator=(const PreparedMonitorStartupHandoff &) = delete;
    bool claimProcessingOwner() noexcept; // Preparation only, once; no reset.
    bool claimed() const noexcept;
    bool publish(const MonitorStartupReady &) noexcept; // Processing owner only.
    void fail(bool inputEnded = false) noexcept; // Processing owner only.
    void cancel() noexcept; // Processing owner, independent of control intent.
    void requestCancel() noexcept; // Control, no join/mutable receipt access.
    void settleAfterProducerJoin() noexcept; // Control only AFTER producer joins.
    MonitorStartupHandoffPhase phase() const noexcept;
    std::optional<MonitorStartupReady> ready() const noexcept; // Original, not permission after terminal state.
    const MonitorStartupAdmission &admission() const noexcept { return admission_; }
    std::size_t chargedBytes() const noexcept { return sizeof(*this); }
  private:
    ResourceLease lease_;
    const MonitorStartupAdmission admission_;
    MonitorStartupReady ready_;
    std::atomic<std::uint32_t> published_{0}, terminal_{0}, cancelled_{0}, claimed_{0};
};
} // namespace soundcurrent::daw
