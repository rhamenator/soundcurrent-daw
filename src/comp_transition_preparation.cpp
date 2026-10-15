// SPDX-License-Identifier: GPL-3.0-only
#include <soundcurrent/comp_transition_preparation.hpp>
namespace soundcurrent::daw {
PreparedCompTransition prepareOwnedCompTransition(const std::filesystem::path &root,
    const Session &s,const CompHandlePlanContext &context,const PrepareCompTransition &transition,
    Frame maximum,std::string worker,CompSpanWorkerPolicy spanPolicy,CompAssemblyJobPolicy assemblyPolicy,
    ResourceLedger ledger,std::stop_token stop){
    const auto poll=[&]{if(stop.stop_requested())throw ProjectError(ErrorCode::Canceled,"Comp preparation canceled");};
    poll();PayloadCharge charge("Comp transition preparation",SIZE_MAX);
    charge.add(sessionPayloadBytes(s),3);charge.add(128*1024);auto lease=ledger.reserve(charge.bytes());
    auto plan=prepareCompProcessing(s,transition,context,maximum,ledger);poll();
    if(plan.handles().outgoing().data().regeneration || plan.handles().incoming().data().regeneration)
        throw ProjectError(ErrorCode::UnsupportedSchema,"Repeated comp pairs require verified queued adoption");
    if(plan.outgoing().domain==CompProcessingDomain::ExistingMedia && plan.incoming().domain==CompProcessingDomain::ExistingMedia)
        throw ProjectError(ErrorCode::InvalidParameter,"This transition can use existing source handles without rendering");
    const auto image=compAssemblyExecutableSha256();poll();
    const CompRecipeExecutor executor{"soundcurrent.comp-join-quintic-v1",image};
    auto recipe=encodeCompProcessingRecipe(s,context,plan,Id::generate(),executor,ledger,{},stop);
    auto selection=std::make_shared<const CompPairSelection>(prepareCompPairSelection(std::move(recipe),executor,
        std::move(worker),{Id::generate(),Id::generate()},spanPolicy,ledger));
    const auto pairRef=referenceCompPairSelection(s,context,*selection);
    auto proposed=s;applySessionEdits(proposed,{AddCompPairSelection{pairRef}});poll();
    std::array<std::optional<Id>,2> outputs;
    for(unsigned side=0;side<2;++side)if(selection->children()[side])outputs[side]=Id::generate();
    auto request=std::make_shared<const CompAssemblyRequest>(prepareCompAssemblyRequest(proposed,context,*selection,
        Id::generate(),outputs,image,assemblyPolicy,ledger));
    const auto assemblyRef=referenceCompAssembly(proposed,context,*selection,*request,ledger);
    auto complete=proposed;applySessionEdits(complete,{AddCompAssembly{assemblyRef}});poll();
    const auto digest=persistCompPairSelection(root,s,context,*selection,ledger,stop);
    if(digest!=pairRef.selectionSha256)throw ProjectError(ErrorCode::MediaMismatch,"Published comp selection changed");
    poll();const auto published=persistCompAssemblyRequest(root,proposed,context,*selection,*request,ledger,stop);
    if(published!=assemblyRef)throw ProjectError(ErrorCode::MediaMismatch,"Published comp request changed");
    poll();return {std::move(lease),root,s.id,context,std::move(selection),std::move(request),pairRef,assemblyRef};
}
} // namespace soundcurrent::daw
