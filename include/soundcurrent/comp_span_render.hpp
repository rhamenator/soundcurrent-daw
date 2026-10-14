// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include "comp_handle_plan.hpp"
#include "clip_timing.hpp"
#include <functional>
namespace soundcurrent::daw {
inline constexpr std::string_view compSpanProcessorId="soundcurrent.comp-span-positioned-rubberband4-r3-v1";
class CompSpanPlan {
  public:
    CompSpanPlan(CompSpanPlan &&) noexcept=default;
    CompSpanPlan &operator=(CompSpanPlan &&)=delete;
    CompSpanPlan(const CompSpanPlan &)=delete;
    const Asset &source() const noexcept {return source_;}
    const SourceFrameMap &grid() const noexcept {return grid_;}
    SourcePosition rawSpan() const noexcept {return span_;}
    Frame processingFrames() const noexcept {return processing_;}
    Frame outputFrames() const noexcept {return output_;}
    const StretchSettings &settings() const noexcept {return settings_;}
    std::uint32_t outputSampleRate() const noexcept {return outputRate_;}
    SourcePosition backendClockScale() const noexcept {return clockScale_;}
  private:
    friend CompSpanPlan prepareCompSpan(const CompHandleEnvelope &,ResourceLedger);
    CompSpanPlan(ResourceLease lease,Asset source,SourceFrameMap grid,SourcePosition span,Frame processing,Frame output,StretchSettings settings,std::uint32_t outputRate,SourcePosition clockScale)
        :lease_(std::move(lease)),source_(std::move(source)),grid_(grid),span_(span),processing_(processing),output_(output),settings_(settings),outputRate_(outputRate),clockScale_(clockScale){}
    ResourceLease lease_;
    Asset source_;
    SourceFrameMap grid_;
    SourcePosition span_;
    Frame processing_,output_;
    StretchSettings settings_;
    std::uint32_t outputRate_;
    SourcePosition clockScale_;
};
// Exact uniform-map span preparation, including fractional origin/endpoints.
// Nonuniform maps remain required; refuse explicitly until their producer exists.
// This prepares physical candidate PCM at the current artifact's sample clock.
// Preserves already baked varispeed while excluding current playback speed.
// Metadata/DSP only: callers must verify original owned files independently.
// Owned spans require version-two clock binding and historical-file review;
// paired publication/adoption remains gated until its integration is qualified.
CompSpanPlan prepareCompSpan(const CompHandleEnvelope &,ResourceLedger);
using CompSpanRead=std::function<void(Frame,std::span<float>)>;
using CompSpanWrite=std::function<void(std::span<const float>)>;
struct CompSpanReport {Frame writtenFrames=0;double floatPeak=0,backendPitchFactor=1;bool copied=false;};
// Off audio/GUI only. Caller owns I/O, publication and cancellation/deadline poll.
// Ledger admits explicit slabs; third-party R3 allocations require a containing
// child-process ceiling in the owned worker. Partial output is never completion.
CompSpanReport renderCompSpan(const CompSpanPlan &,const CompSpanRead &,const CompSpanWrite &,
    ResourceLedger,const std::function<void()> &poll);
} // namespace soundcurrent::daw
