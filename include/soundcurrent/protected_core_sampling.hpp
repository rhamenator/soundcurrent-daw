// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include "warp.hpp"
#include "positioned_resampling.hpp"
#include <functional>
namespace soundcurrent::daw {
// Full semantic core boundaries stay rational and uncropped. The half-open
// integer output range selects only samples whose clock positions lie inside
// that core and the requested output window. rawSamples starts at firstOutput.
struct ProtectedCoreWindow {
    Id owner;
    SourcePosition rawBegin,rawEnd,outputBegin,outputEnd;
    Frame firstOutput=0,endOutput=0;
    SourceFrameMap rawSamples;
};
// OFF-RT prepared component for the direct protected portions of a render.
// The clock maps destination sample indices into the original geometry's output
// units; its origin may crop through a core. It never alters the semantic map.
// Nonunity gaps and halo blends belong to the separate stretch renderer. This
// component alone does not enable fractional/changed-clock worker publication.
class PreparedProtectedCoreSampling {
  public:
    PreparedProtectedCoreSampling(const ProtectedWarpPlan &,SourceFrameMap outputClock,
        Frame outputFrames,std::uint32_t channels,ResourceLedger,
        const std::function<void()> &poll,ClipWarpLimits={});
    PreparedProtectedCoreSampling(const PreparedProtectedCoreSampling &)=delete;
    PreparedProtectedCoreSampling &operator=(const PreparedProtectedCoreSampling &)=delete;
    PreparedProtectedCoreSampling(PreparedProtectedCoreSampling &&) noexcept=default;
    PreparedProtectedCoreSampling &operator=(PreparedProtectedCoreSampling &&)=delete;
    std::span<const ProtectedCoreWindow> windows() const noexcept {return windows_;}
    // Caller owns disjoint source/output buffers and their memory admission.
    // Ranges include actual raw-asset FIR context, with the positioned kernel's
    // explicit zero extension at asset edges. The caller must admit and bind the
    // immutable original raw asset; this geometry-only component does no file I/O.
    std::size_t maximumSourceWindowFrames() const;
    SourceReadRange sourceRange(std::size_t window,Frame offset,std::uint32_t frames) const;
    void process(std::size_t window,Frame offset,Frame firstSource,
        std::span<const float> source,std::span<float> output) const;
  private:
    const ProtectedCoreWindow &checked(std::size_t,Frame,std::uint32_t) const;
    ResourceLease lease_; // Return credits after owned windows retire.
    std::vector<ProtectedCoreWindow> windows_;
    PreparedPositionedResampling kernel_;
    Frame sourceFrames_;
    std::uint32_t channels_;
};
} // namespace soundcurrent::daw
