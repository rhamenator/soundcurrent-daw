// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include "comp_span_render.hpp"
#include "exact_processing_grid.hpp"
namespace soundcurrent::daw {
inline constexpr std::string_view compUniformContextProcessorId="soundcurrent.comp-uniform-original-context-v1";
// OFF-RT metadata/DSP only. No file verification or adoption authority.
class CompUniformContextPlan {
 public:
  CompUniformContextPlan(CompUniformContextPlan &&) noexcept=default;
  CompUniformContextPlan(const CompUniformContextPlan &)=delete;
  CompUniformContextPlan &operator=(CompUniformContextPlan &&)=delete;
  const Asset &source() const noexcept {return source_;}
  const ExactProcessingGrid &input() const noexcept {return input_;}
  const ExactProcessingGrid &samples() const noexcept {return samples_;}
  SourcePosition rawExtent() const noexcept {return rawExtent_;}
  SourcePosition intrinsicExtent() const noexcept {return intrinsicExtent_;}
  SourcePosition bakedPitchScale() const noexcept {return baked_;}
  Frame inputFrames() const noexcept {return inputFrames_;}
  Frame renderFrames() const noexcept {return renderFrames_;}
  Frame outputFrames() const noexcept {return outputFrames_;}
  std::uint32_t outputSampleRate() const noexcept {return outputRate_;}
  const StretchSettings &settings() const noexcept {return settings_;}
 private:
  friend CompUniformContextPlan prepareUniformContextCompSpan(const CompHandleEnvelope &,ResourceLedger,const std::function<void()> &);
  CompUniformContextPlan(ResourceLease lease,Asset source,ExactProcessingGrid input,ExactProcessingGrid samples,
      SourcePosition rawExtent,SourcePosition intrinsicExtent,SourcePosition baked,Frame inputFrames,Frame renderFrames,
      Frame outputFrames,std::uint32_t outputRate,StretchSettings settings)
      :lease_(std::move(lease)),source_(std::move(source)),input_(input),samples_(samples),rawExtent_(rawExtent),
       intrinsicExtent_(intrinsicExtent),baked_(baked),inputFrames_(inputFrames),renderFrames_(renderFrames),
       outputFrames_(outputFrames),outputRate_(outputRate),settings_(settings){}
  ResourceLease lease_;
  Asset source_;
  ExactProcessingGrid input_,samples_;
  SourcePosition rawExtent_,intrinsicExtent_,baked_;
  Frame inputFrames_,renderFrames_,outputFrames_;
  std::uint32_t outputRate_;
  StretchSettings settings_;
};
// Retains the original nominal analysis domain and all retained ancestry extents.
// Derives the current crop at its exact phase; no rounded-origin or silent padding.
// A genuinely short full original domain can still require a different strategy.
CompUniformContextPlan prepareUniformContextCompSpan(const CompHandleEnvelope &,ResourceLedger,const std::function<void()> &poll);
struct CompUniformContextReport {
 Frame generatedFrames=0,writtenFrames=0;
 std::size_t maximumBufferedFrames=0;
 double floatPeak=0,backendPitchFactor=1;
};
// Fixed 512-frame backend schedule. Bounded slabs independent of context duration.
// Caller owns isolated vendor-memory limits, verified raw reads and publication.
CompUniformContextReport renderUniformContextCompSpan(const CompUniformContextPlan &,const CompSpanRead &,
    const CompSpanWrite &,ResourceLedger,const std::function<void()> &poll);
} // namespace soundcurrent::daw
