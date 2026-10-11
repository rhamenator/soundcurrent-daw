// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include "resource_ledger.hpp"
#include <span>

namespace soundcurrent::daw {
// Ordered sparse matrix, using the same zero-based slots and gains as MixPlan.
// Unrouted destinations are silent; no speaker layout or pan law is guessed.
struct MonitorChannelMap {
    std::uint32_t inputChannels = 0, outputChannels = 0;
    std::vector<ChannelMix> routes;
    bool operator==(const MonitorChannelMap &) const = default;
};
MonitorChannelMap monoToStereoMonitor(); // Explicit unity copy to each output.
std::size_t monitorChannelMapPayloadBytes(const MonitorChannelMap &, std::uint32_t maximumFrames,
                                        std::size_t memoryBudgetBytes);
enum class MonitorChannelMapCode { Mapped, InvalidGeometry, NonfiniteInput, NumericOverflow };
// One processing owner. Prepare/retire off RT. Zero added frame latency/tail.
// Work bounded by admitted frames, channels and routes (at most 65,536).
// Double accumulation in declaration order; finite float headroom is preserved.
// Whole-block refusal leaves the destination unchanged, including overlap.
class PreparedMonitorChannelMap {
  public:
    PreparedMonitorChannelMap(MonitorChannelMap, std::uint32_t maximumFrames,
        std::size_t memoryBudgetBytes = 64 * 1024 * 1024, std::optional<ResourceLedger> = {});
    PreparedMonitorChannelMap(const PreparedMonitorChannelMap &) = delete;
    PreparedMonitorChannelMap &operator=(const PreparedMonitorChannelMap &) = delete;
    MonitorChannelMapCode process(std::span<const float> input, std::span<float> output,
                                 std::uint32_t frames) noexcept;
    const MonitorChannelMap &plan() const noexcept { return plan_; }
    std::size_t chargedBytes() const noexcept { return bytes_; }
  private:
    ResourceLease lease_;
    const MonitorChannelMap plan_;
    const std::uint32_t maximumFrames_;
    std::vector<double> sums_;
    const std::size_t bytes_;
};
} // namespace soundcurrent::daw
