// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include "comp_handle_plan.hpp"
#include "comp_handle_pcm.hpp"
namespace soundcurrent::daw {
enum class CompProcessingDomain { ExistingMedia=0, PhysicalArtifact=1, ProjectAfterSrc=2 };
struct CompFragmentProcessing {
    CompProcessingDomain domain=CompProcessingDomain::ExistingMedia;
    CompHandlePcmSpec pcm;
    CompHandlePcmBlends blends;
};
// Control-side immutable permissions derived from the actual requested project
// transition. No file access, project mutation, renderer or quality admission.
class CompProcessingPlan {
  public:
    CompProcessingPlan(CompProcessingPlan &&) noexcept=default;
    CompProcessingPlan &operator=(CompProcessingPlan &&)=delete;
    CompProcessingPlan(const CompProcessingPlan &)=delete;
    CompProcessingPlan &operator=(const CompProcessingPlan &)=delete;
    const CompTransitionHandlePlan &handles() const {return handles_;}
    const CompFragmentProcessing &outgoing() const;
    const CompFragmentProcessing &incoming() const;
    Frame maximumBlendProjectFrames() const noexcept {return maximum_;}
  private:
    friend CompProcessingPlan prepareCompProcessing(const Session &,const PrepareCompTransition &,
        const CompHandlePlanContext &,Frame,ResourceLedger,CompHandlePlanLimits);
    CompProcessingPlan(ResourceLease lease,CompTransitionHandlePlan handles,Frame maximum,
        std::unique_ptr<const CompFragmentProcessing> out,std::unique_ptr<const CompFragmentProcessing> in)
        :lease_(std::move(lease)),handles_(std::move(handles)),maximum_(maximum),outgoing_(std::move(out)),incoming_(std::move(in)){}
    ResourceLease lease_;
    CompTransitionHandlePlan handles_;
    const Frame maximum_;
    std::unique_ptr<const CompFragmentProcessing> outgoing_,incoming_;
};
// Positive maximum in project frames; actual width is limited by the requested
// transition's overlap with the existing fragment. ProjectAfterSrc is required
// when physical modifications could leak outside that window through the FIR.
CompProcessingPlan prepareCompProcessing(const Session &,const PrepareCompTransition &,
    const CompHandlePlanContext &,Frame maximumBlendProjectFrames,ResourceLedger,
    CompHandlePlanLimits={});
void verifyCompProcessingCurrent(const Session &,const CompHandlePlanContext &,const CompProcessingPlan &);
} // namespace soundcurrent::daw
