// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include "comp_handle_plan.hpp"
#include "stretch_render_protocol.hpp"
namespace soundcurrent::daw {
// First producer: exact unity maps with zero independent pitch. Other processors
// still require their own handle-generation path; never reinterpret their speed.
ClipStretchPlan prepareUnityCompHandleRender(const CompHandleEnvelope &);
class VerifiedCompHandlePair {
  public:
    VerifiedCompHandlePair(VerifiedCompHandlePair &&) noexcept=default;
    const ApplyCompHandlePair &edit() const noexcept {return edit_;}
  private:
    friend VerifiedCompHandlePair verifyUnityCompHandlePair(const std::filesystem::path &,
        const Session &,const CompHandlePlanContext &,const CompTransitionHandlePlan &,
        const VerifiedClipStretch *,const VerifiedClipStretch *,const StretchRenderPolicy &,
        ResourceLedger,std::stop_token);
    VerifiedCompHandlePair(ResourceLease lease,ApplyCompHandlePair edit)
        : lease_(std::move(lease)),edit_(std::move(edit)) {}
    ResourceLease lease_;
    ApplyCompHandlePair edit_;
};
// Child results must have passed the existing owned marker/raw/full-PCM verifier.
// Here current/nominal media are pinned, hashed and decoded; both inner buffers
// must be bit-preserved inside the candidate. No mutation or file publication.
VerifiedCompHandlePair verifyUnityCompHandlePair(const std::filesystem::path &,
    const Session &,const CompHandlePlanContext &,const CompTransitionHandlePlan &,
    const VerifiedClipStretch *outgoing,const VerifiedClipStretch *incoming,
    const StretchRenderPolicy &,ResourceLedger,std::stop_token={});
} // namespace soundcurrent::daw
