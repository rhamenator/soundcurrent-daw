// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include "warp.hpp"
#include <functional>
namespace soundcurrent::daw {
// OFF-RT only. Callbacks perform owned I/O; no real-time invocation is supported.
// Positions/counts are physical frames in the plan's raw grid. Blocks <=512.
using WarpRead=std::function<void(Frame,std::span<float>)>;
using WarpWrite=std::function<void(std::span<const float>)>;
// Admission matches the shortest tested nonunity gap; this is not a quality guarantee.
void validateProtectedWarpRender(const ProtectedWarpPlan &,std::uint32_t rate,std::uint32_t channels);
struct ProtectedWarpReport {Frame writtenFrames=0;std::size_t processedGaps=0;};
ProtectedWarpReport renderProtectedWarp(const ProtectedWarpPlan &,std::uint32_t rate,
    std::uint32_t channels,const WarpRead &,const WarpWrite &,ResourceLedger,
    const std::function<void()> &poll);
} // namespace soundcurrent::daw
