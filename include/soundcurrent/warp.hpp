// SPDX-License-Identifier: GPL-3.0-only
// Original GPL planner promoted from PR89; control-side immutable geometry.
#pragma once
#include <soundcurrent/session.hpp>
#include <soundcurrent/resource_ledger.hpp>
#include <map>
#include <memory>
#include <optional>
#include <span>
#include <vector>

namespace soundcurrent::daw {
inline constexpr std::string_view clipWarpGeometryId = "soundcurrent.warp-piecewise-rational-v1";
struct ClipWarpPoint {
    std::optional<Id> id; // implicit endpoints have no object identity
    SourcePosition source, output;
};
struct ClipWarpLimits {
    std::size_t maximumMarkers = 4096;
    std::size_t maximumPayloadBytes = 1024 * 1024;
};
struct ClipWarpRegion {
    SourcePosition rawOrigin;
    std::uint32_t physicalRate = 48000;
    Frame availableSourceFrames = 0, inputFrames = 0, outputFrames = 0;
    SourcePosition visibleSourceBegin, visibleSourceEnd;
};
class ClipWarpMap {
  public:
    ClipWarpMap(ClipWarpRegion, std::span<const WarpAnchor>, ResourceLedger, ClipWarpLimits = {});
    const ClipWarpRegion &region() const noexcept { return region_; }
    std::span<const ClipWarpPoint> points() const noexcept { return points_; }
    SourcePosition sourceToOutput(SourcePosition) const;
    SourcePosition outputToSource(SourcePosition) const;
    SourcePosition rawAt(SourcePosition sourceOffset) const;
    SourcePosition visibleOutputBegin() const;
    SourcePosition visibleOutputEnd() const;
    // OFF-RT integer-only vendor adapter. Refuses fractional markers instead
    // of rounding. Excludes BOTH implicit endpoints, especially R3's 0->0.
    std::map<std::size_t, std::size_t> vendorInteriorFrames() const;
    std::size_t chargedBytes() const noexcept { return lease_.bytes(); }
  private:
    ResourceLease lease_; // destroyed after owned points
    ClipWarpRegion region_;
    std::vector<ClipWarpPoint> points_;
};
struct ProtectedWarpSpan { Id owner; SourcePosition sourceAnchor,outputAnchor,sourceBegin,sourceEnd,outputBegin,outputEnd; };
struct ProtectedWarpGap { SourcePosition sourceBegin,sourceEnd,outputBegin,outputEnd; };
class ProtectedWarpPlan {
 public:
  ProtectedWarpPlan(ClipWarpRegion,std::span<const WarpAnchor>,WarpProtection,ResourceLedger,ClipWarpLimits={});
  std::span<const ProtectedWarpSpan> spans() const noexcept { return spans_; }
  std::span<const ProtectedWarpGap> gaps() const noexcept { return gaps_; }
  const ClipWarpMap &map() const noexcept { return *map_; }
  WarpProtection policy() const noexcept { return policy_; }
 private:
  ResourceLease lease_; // retires after owned storage
  WarpProtection policy_;
  std::vector<ProtectedWarpSpan> spans_;
  std::vector<ProtectedWarpGap> gaps_;
  std::unique_ptr<ClipWarpMap> map_;
};


std::size_t warpPayloadBytes(const WarpSettings &);
void validateWarpSettings(const WarpSettings &);
} // namespace soundcurrent::daw
