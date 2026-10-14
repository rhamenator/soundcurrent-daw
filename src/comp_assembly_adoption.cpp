// SPDX-License-Identifier: GPL-3.0-only
#include <soundcurrent/comp_assembly_adoption.hpp>
#include <soundcurrent/comp_playback_source.hpp>
#include <soundcurrent/comp_regeneration_history.hpp>
#include <algorithm>

namespace soundcurrent::daw {
VerifiedCompAssemblyAdoption verifyCompAssemblyAdoption(const std::filesystem::path &root,
    const Session &s,const CompHandlePlanContext &context,const CompPairSelection &pair,
    const CompAssemblyRequest &request,ResourceLedger ledger,std::stop_token stop){
    const auto check=[&](bool v,const char *m){if(!v)throw ProjectError(ErrorCode::InvalidState,m);};
    const auto poll=[&]{if(stop.stop_requested())throw ProjectError(ErrorCode::Canceled,"Comp adoption canceled");};
    poll();verifyCompProcessingCurrent(s,context,pair.recipe().plan());
    const auto parent=referenceCompPairSelection(s,context,pair);
    check(std::find(s.compPairSelections.begin(),s.compPairSelections.end(),parent)!=s.compPairSelections.end(),"Comp adoption lost its parent selection");
    const CompAssemblyReference reference{request.operation(),parent.operation,request.digest(),request.outputs()};
    check(std::find(s.compAssemblies.begin(),s.compAssemblies.end(),reference)!=s.compAssemblies.end(),"Comp adoption lost its independent request");
    PayloadCharge charge("Comp adoption staging",SIZE_MAX);
    charge.add(sessionPayloadBytes(s),4);charge.add(8*16388,sizeof(CompHandlePoint));
    charge.add(128*1024+root.native().size()*sizeof(std::filesystem::path::value_type)+context.rootIdentity.size());
    auto lease=ledger.reserve(charge.bytes());
    const auto verified=verifyCompAssembly(root,s,context,pair,request,ledger,stop);poll();
    check(verified.requestDigest()==reference.requestSha256,"Comp adoption request changed");
    const auto &plan=pair.recipe().plan();
    std::vector<ReviewedCompRegenerationHistory> reviews;
    for(const auto *envelope:{&plan.handles().outgoing(),&plan.handles().incoming()})if(envelope->data().regeneration){
        poll();CompHistoryReviewLimits limits;limits.maximumInputFileBytes=request.policy().maximumFileBytes;
        reviews.push_back(reviewCompRegenerationHistory(root,s,context,envelope->data().regeneration,ledger,limits,stop));
    }
    std::vector<CompAssemblyDependencyView> dependencies;
    for(const auto &review:reviews){const auto &d=review.data();
        verifyCompRegenerationHistoryCurrent(s,context,review);
        dependencies.push_back({d.assets,d.history,d.selections,d.retained,d.tracks});
    }
    auto guard=std::make_shared<const CompAssemblyDependencies>(captureCompAssemblyDependencies(s,pair.recipe().selection(),dependencies,ledger));
    const auto identity=[](const CompHandleEnvelopeData &h){return CompAssemblySourceIdentity{h.expected,h.retained,h.raw,h.base,h.retainedAsset,h.nominalAsset};};
    ApplyCompAssembly edit{plan.handles().transition(),reference,parent,
        {identity(plan.handles().outgoing().data()),identity(plan.handles().incoming().data())},{},std::move(guard)};
    for(unsigned side=0;side<2;++side){poll();if(verified.outputs()[side])
        edit.sources[side]=prepareCompPlaybackSource(s,context,plan,side,verified.outputs()[side],ledger).data();}
    auto proposed=s;applySessionEdits(proposed,{edit});poll();
    return {std::move(lease),s.id,root,context,std::move(edit)};
}
} // namespace soundcurrent::daw
