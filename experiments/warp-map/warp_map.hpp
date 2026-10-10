// SPDX-License-Identifier: GPL-3.0-only
// Original experimental control-side geometry, outside the shipping build.
#pragma once
#include <soundcurrent/stretch.hpp>
#include <soundcurrent/resource_ledger.hpp>
#include <map>
#include <optional>
#include <span>
#include <vector>

namespace soundcurrent::daw::experimental {
inline constexpr std::string_view warpGeometryId = "soundcurrent.experimental.warp-piecewise-rational-v1";
struct WarpMarker {
    Id id;
    // Source is a physical-frame OFFSET from rawOrigin; output is a derived
    // output-frame offset. Neither is a project/tempo coordinate.
    SourcePosition source, output;
};
struct WarpPoint {
    std::optional<Id> id; // implicit endpoints have no object identity
    SourcePosition source, output;
};
struct WarpLimits {
    std::size_t maximumMarkers = 4096;
    std::size_t maximumPayloadBytes = 1024 * 1024;
};
struct WarpRegion {
    SourcePosition rawOrigin;
    std::uint32_t physicalRate = 48000;
    Frame availableSourceFrames = 0, inputFrames = 0, outputFrames = 0;
    SourcePosition visibleSourceBegin, visibleSourceEnd;
};
class WarpMap {
  public:
    WarpMap(WarpRegion, std::span<const WarpMarker>, ResourceLedger, WarpLimits = {});
    const WarpRegion &region() const noexcept { return region_; }
    std::span<const WarpPoint> points() const noexcept { return points_; }
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
    WarpRegion region_;
    std::vector<WarpPoint> points_;
};
} // namespace soundcurrent::daw::experimental
