// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include "resource_ledger.hpp"
#include <functional>
#include <optional>
#include <span>
namespace soundcurrent::daw {
struct CompHandlePcmSpec {
    std::uint32_t channels=0, blockFrames=512;
    Frame currentFrames=0, prefixFrames=0, suffixFrames=0;
};
struct CompHandleJoinMetrics {
    double stepPeak=0;
    // Requires two samples on each side. Measures how far the join step
    // differs from either neighboring first difference; not a sonic verdict.
    std::optional<double> slopeDeviationPeak;
};
struct CompHandlePcmReport {
    Frame writtenFrames=0;
    double floatPeak=0;
    std::optional<CompHandleJoinMetrics> prefixJoin,suffixJoin;
};
// Current-buffer physical frames. These permissions must eventually be derived
// from the requested project transition by the trusted control owner. Supplying
// them here does not confer filesystem or model-adoption authority.
struct CompHandleInnerBlend {
    Frame frames=0, permittedFirst=0, permittedEnd=0;
};
struct CompHandlePcmBlends {
    std::optional<CompHandleInnerBlend> prefix,suffix;
};
using CompHandlePcmRead=std::function<void(Frame,std::span<float>)>;
using CompHandlePcmWrite=std::function<void(std::span<const float>)>;
// Off audio only. Readers must bind immutable, independently verified media.
// Candidate addresses the expanded frame domain. Emit its prefix/suffix and
// the unchanged current inner buffer in bounded slabs. Readers receive at most
// max(blockFrames,2) frames for boundary inspection. Caller owns scratch-file
// publication/rollback; a late failure can leave partial output. This function
// never grants filesystem access, admits project state or repairs poor joins.
CompHandlePcmReport assembleCompHandlePcm(const CompHandlePcmSpec &,
    const CompHandlePcmRead &current,const CompHandlePcmRead &candidate,
    const CompHandlePcmWrite &,ResourceLedger,const std::function<void()> &poll);
// Separate explicit operation: quintic complementary blending only inside the
// declared permitted inner regions. Candidate must include real rendered context
// for those samples. Original readers are immutable; all unblended current PCM
// and outer edges are copied unchanged. No automatic quality/model admission.
CompHandlePcmReport blendCompHandlePcm(const CompHandlePcmSpec &,
    const CompHandlePcmBlends &,const CompHandlePcmRead &current,
    const CompHandlePcmRead &candidate,const CompHandlePcmWrite &,
    ResourceLedger,const std::function<void()> &poll);
} // namespace soundcurrent::daw
