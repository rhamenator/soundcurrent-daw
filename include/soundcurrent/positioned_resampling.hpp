// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include "clip_timing.hpp"
#include <span>
#include <string_view>

namespace soundcurrent::daw {
inline constexpr std::string_view positionedResamplingAlgorithmId="soundcurrent.src-positioned-best-v1";
struct SourceReadRange { Frame first=0; std::size_t frames=0; };
// Immutable worker-side FIR kernel. Disk/cache, buffers and memory leases belong
// to the caller. Every sample uses its exact absolute rational source coordinate;
// no accumulated phase, reset history, seek replay or private upstream ABI.
class PreparedPositionedResampling {
  public:
    PreparedPositionedResampling(std::uint32_t sourceRate,std::uint32_t projectRate,
                                std::uint32_t channels);
    std::uint32_t sourceContextFrames() const noexcept;
    std::size_t maximumSourceWindowFrames(std::uint32_t outputFrames) const;
    SourceReadRange sourceRange(const SourceFrameMap &,Frame firstProject,
                               std::uint32_t outputFrames,Frame sourceFrames) const;
    bool exactCopy(const SourceFrameMap &) const;
    // Input/output storage must be disjoint; overlapping spans are refused.
    void process(const SourceFrameMap &,Frame firstProject,Frame sourceFrames,
                 Frame firstSource,std::span<const float> source,std::span<float> output) const;
  private:
    std::uint32_t sourceRate_,projectRate_,channels_;
    double floatIncrement_,scale_;
    std::int64_t increment_;
};
} // namespace soundcurrent::daw
