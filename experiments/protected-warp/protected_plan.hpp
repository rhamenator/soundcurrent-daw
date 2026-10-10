// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include "../warp-map/warp_map.hpp"
#include <memory>
namespace soundcurrent::daw::experimental {
struct ProtectionPolicy { Frame before=256, after=2048, halo=64, minimumNonunityGap=64; };
struct ProtectedSpan { Id owner; SourcePosition sourceAnchor,outputAnchor,sourceBegin,sourceEnd,outputBegin,outputEnd; };
struct ProtectedGap { SourcePosition sourceBegin,sourceEnd,outputBegin,outputEnd; };
class ProtectedPlan {
 public:
  ProtectedPlan(WarpRegion,std::span<const WarpMarker>,ProtectionPolicy,ResourceLedger,WarpLimits={});
  std::span<const ProtectedSpan> spans() const noexcept { return spans_; }
  std::span<const ProtectedGap> gaps() const noexcept { return gaps_; }
  const WarpMap &map() const noexcept { return *map_; }
  ProtectionPolicy policy() const noexcept { return policy_; }
 private:
  ResourceLease lease_; // retires after owned storage
  ProtectionPolicy policy_;
  std::vector<ProtectedSpan> spans_;
  std::vector<ProtectedGap> gaps_;
  std::unique_ptr<WarpMap> map_;
};
} // namespace soundcurrent::daw::experimental
