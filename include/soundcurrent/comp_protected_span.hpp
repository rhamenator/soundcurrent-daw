// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include "comp_handle_plan.hpp"
#include "protected_warp_render.hpp"
namespace soundcurrent::daw {
inline constexpr std::string_view compProtectedSpanProcessorId =
    "soundcurrent.comp-span-protected-rubberband4-r3-v1";
// Immutable OFF-RT plan. Owns raw declaration, exact map and protected semantic
// spans; never borrows the envelope. File admission remains the caller's duty.
class CompProtectedSpanPlan {
  public:
    CompProtectedSpanPlan(CompProtectedSpanPlan &&) noexcept = default;
    CompProtectedSpanPlan &operator=(CompProtectedSpanPlan &&) = delete;
    CompProtectedSpanPlan(const CompProtectedSpanPlan &) = delete;
    const Asset &source() const noexcept { return source_; }
    std::span<const CompHandlePoint> rawMap() const noexcept { return rawMap_; }
    const ProtectedWarpPlan &geometry() const;
  private:
    friend CompProtectedSpanPlan prepareProtectedCompSpan(const CompHandleEnvelope &,
        ResourceLedger, const std::function<void()> &);
    CompProtectedSpanPlan(ResourceLease lease, Asset source,
        std::vector<CompHandlePoint> map, std::unique_ptr<ProtectedWarpPlan> geometry)
        : lease_(std::move(lease)), source_(std::move(source)), rawMap_(std::move(map)),
          geometry_(std::move(geometry)) {}
    ResourceLease lease_; // Retire storage before returning its payload credit.
    Asset source_;
    std::vector<CompHandlePoint> rawMap_;
    std::unique_ptr<ProtectedWarpPlan> geometry_;
};
// First adapter admits original protected clips at integer acoustic positions and
// unchanged sample clocks. Unsupported history/fractional/clock paths refuse.
// Does not activate the uniform comp worker or change its identity/protocol.
CompProtectedSpanPlan prepareProtectedCompSpan(const CompHandleEnvelope &,
    ResourceLedger, const std::function<void()> &poll);
struct CompProtectedSpanReport {
    Frame writtenFrames=0;
    std::size_t processedGaps=0;
    double floatPeak=0;
};
// Read positions are ABSOLUTE raw-asset frames. Caller owns I/O, source admission,
// child-process memory ceiling, deadline/cancellation poll and atomic publication.
// NaN poisoning detects incomplete reads. Partial writes never imply completion.
CompProtectedSpanReport renderProtectedCompSpan(const CompProtectedSpanPlan &,
    const WarpRead &, const WarpWrite &, ResourceLedger, const std::function<void()> &poll);
} // namespace soundcurrent::daw
