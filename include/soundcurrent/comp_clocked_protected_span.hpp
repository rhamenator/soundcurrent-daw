// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include "comp_handle_plan.hpp"
#include "clocked_protected_warp.hpp"
namespace soundcurrent::daw {
inline constexpr std::string_view compClockedProtectedSpanProcessorId="soundcurrent.comp-span-protected-clocked-v1";
// OFF-RT owned original geometry and candidate sampling. No project/file authority.
class CompClockedProtectedSpanPlan {
 public:
  CompClockedProtectedSpanPlan(CompClockedProtectedSpanPlan &&) noexcept=default;
  CompClockedProtectedSpanPlan(const CompClockedProtectedSpanPlan &)=delete;
  CompClockedProtectedSpanPlan &operator=(CompClockedProtectedSpanPlan &&)=delete;
  const Asset &source() const noexcept {return source_;}
  const ProtectedWarpPlan &geometry() const;
  const PreparedClockedProtectedWarp &sampling() const;
  std::uint32_t outputSampleRate() const noexcept {return outputRate_;}
  SourcePosition bakedPitchScale() const noexcept {return bakedPitch_;}
 private:
  friend CompClockedProtectedSpanPlan prepareClockedProtectedCompSpan(const CompHandleEnvelope &,ResourceLedger,const std::function<void()> &);
  CompClockedProtectedSpanPlan(ResourceLease lease,Asset source,std::uint32_t rate,SourcePosition pitch,
      std::unique_ptr<ProtectedWarpPlan> geometry,std::unique_ptr<PreparedClockedProtectedWarp> sampling)
    :lease_(std::move(lease)),source_(std::move(source)),outputRate_(rate),bakedPitch_(pitch),geometry_(std::move(geometry)),sampling_(std::move(sampling)){}
  ResourceLease lease_;
  Asset source_;
  std::uint32_t outputRate_;
  SourcePosition bakedPitch_;
  std::unique_ptr<ProtectedWarpPlan> geometry_;
  std::unique_ptr<PreparedClockedProtectedWarp> sampling_;
};
// Worker/control-side proof of an affine retained-map clock against full
// original geometry. Checks all knots of both maps; no file/adoption authority.
SourceFrameMap verifyProtectedCompClock(const ProtectedWarpPlan &,
    std::span<const CompHandlePoint>,Frame,const std::function<void()> &poll);
// Reconstructs full original protection including anchors outside a cropped
// artifact. Checks the union of all retained map knots, not just endpoints.
// Output PCM uses the current base clock and preserves baked ancestor varispeed.
// Original nominal pitch modification still requires a protected-pitch producer.
CompClockedProtectedSpanPlan prepareClockedProtectedCompSpan(const CompHandleEnvelope &,
    ResourceLedger,const std::function<void()> &poll);
// Absolute original raw-asset reads; caller verifies media/history and owns
// isolated vendor heap/CPU limits, cancellation and discard-on-failure publication.
ClockedProtectedReport renderClockedProtectedCompSpan(const CompClockedProtectedSpanPlan &,
    const WarpRead &,const WarpWrite &,ResourceLedger,const std::function<void()> &poll);
} // namespace soundcurrent::daw
