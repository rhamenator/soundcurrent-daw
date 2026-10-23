// SPDX-License-Identifier: GPL-3.0-only
#include <soundcurrent/comp_transition_preparation.hpp>
#include <soundcurrent/comp_regeneration_source.hpp>
#include <algorithm>
namespace soundcurrent::daw {
PreparedCompTransition prepareOwnedCompTransition(const std::filesystem::path &root,
    const Session &s,const CompHandlePlanContext &context,const PrepareCompTransition &transition,
    Frame maximum,std::string worker,CompSpanWorkerPolicy spanPolicy,CompAssemblyJobPolicy assemblyPolicy,
    ResourceLedger ledger,std::stop_token stop){
    const auto poll=[&]{if(stop.stop_requested())throw ProjectError(ErrorCode::Canceled,"Comp preparation canceled");};
    poll();PayloadCharge charge("Comp transition preparation",SIZE_MAX);
    charge.add(sessionPayloadBytes(s),3);charge.add(128*1024);auto lease=ledger.reserve(charge.bytes());
    auto plan=prepareCompProcessing(s,transition,context,maximum,ledger);poll();
    if(plan.outgoing().domain==CompProcessingDomain::ExistingMedia && plan.incoming().domain==CompProcessingDomain::ExistingMedia)
        throw ProjectError(ErrorCode::InvalidParameter,"This transition can use existing source handles without rendering");
    const auto image=compAssemblyExecutableSha256();poll();
    const CompRecipeExecutor executor{"soundcurrent.comp-join-quintic-v1",image};
    auto recipe=encodeCompProcessingRecipe(s,context,plan,Id::generate(),executor,ledger,{},stop);
    auto pairPolicy=CompPairSpanPolicy::UniformOnly;bool hasHistory=false,uniformProcessing=false;
    for(const auto *fragment:{&recipe.plan().handles().outgoing(),&recipe.plan().handles().incoming()}){
        const auto &data=fragment->data();
        const auto &original=data.regeneration?data.regeneration->data().originalNominalClip:data.expected;
        const auto domain=fragment==&recipe.plan().handles().outgoing()?recipe.plan().outgoing().domain:recipe.plan().incoming().domain;
        // Regenerated uniform clips use their original analysis domain so a crop
        // cannot accidentally choose a shorter backend window or erase baked pitch.
        hasHistory=hasHistory || bool(data.regeneration);
        if(domain!=CompProcessingDomain::ExistingMedia && original.stretch && !original.stretch->warp)
            uniformProcessing=true;
        // Select modern timing explicitly from source metadata, never by trying
        // a failed worker or dropping protection. Preserve the legacy identity
        // for same-clock integer requests and all retained legacy manifests.
        if(original.stretch && original.stretch->warp){
            const bool clocked=data.raw.sampleRate!=data.base.sampleRate || data.projectRate!=data.base.sampleRate ||
                data.expected.sourceTiming.fraction || data.expected.playbackRate.numerator!=data.expected.playbackRate.denominator ||
                (data.regeneration && (data.regeneration->data().bakedPitchScale!=SourcePosition{1,0,1} ||
                    data.regeneration->data().backendClockScale!=SourcePosition{1,0,1})) ||
                std::any_of(data.map.begin(),data.map.end(),[](const auto &point){return point.raw.fraction || point.output.fraction;});
            if(clocked)pairPolicy=CompPairSpanPolicy::ClockedProtected;
            else if(pairPolicy==CompPairSpanPolicy::UniformOnly)pairPolicy=CompPairSpanPolicy::ProtectedOriginal;
        }
    }
    if(hasHistory && uniformProcessing)pairPolicy=CompPairSpanPolicy::OriginalContext;
    auto selection=std::make_shared<const CompPairSelection>(prepareCompPairSelection(std::move(recipe),executor,
        std::move(worker),{Id::generate(),Id::generate()},spanPolicy,ledger,pairPolicy));
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
