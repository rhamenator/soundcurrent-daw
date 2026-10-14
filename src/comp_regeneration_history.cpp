// SPDX-License-Identifier: GPL-3.0-only
#include <soundcurrent/comp_regeneration_history.hpp>
#include <soundcurrent/comp_assembly_job.hpp>
#include <soundcurrent/comp_playback_source.hpp>
#include <soundcurrent/comp_playback_state.hpp>
#include <soundcurrent/approved_media.hpp>
#include <algorithm>
#include <set>
namespace soundcurrent::daw {
namespace {
void check(bool v,const char *m,ErrorCode e=ErrorCode::InvalidState){if(!v)throw ProjectError(e,m);}
void poll(std::stop_token t){check(!t.stop_requested(),"Comp history review canceled",ErrorCode::Canceled);}
template<class T>const T &find(const std::vector<T> &v,const Id &id){const auto p=std::find_if(v.begin(),v.end(),[&](const auto &x){return x.id==id;});check(p!=v.end(),"Historical comp dependency missing");return *p;}
template<class T>void add(std::vector<T> &v,const T &item){if(std::find(v.begin(),v.end(),item)==v.end())v.push_back(item);}
void capture(const Session &selected,const Session &current,CompHistoryReviewData &d){
    check(selected.id==current.id && selected.sampleRate==current.sampleRate,"Historical project identity changed");
    for(const auto &a:selected.assets){check(find(current.assets,a.id)==a,"Historical asset declaration changed");add(d.assets,a);}
    for(const auto &h:selected.compPlaybackSources){const auto *p=findCompPlaybackSource(current,h.source.rendered.id);check(p && *p==h,"Historical source declaration changed");add(d.history,h);}
    for(const auto &r:selected.compPairSelections){check(std::find(current.compPairSelections.begin(),current.compPairSelections.end(),r)!=current.compPairSelections.end(),"Historical selection reference changed");add(d.selections,r);}
    for(const auto &t:selected.tracks){const auto &now=find(current.tracks,t.id);check(now.layout==t.layout,"Historical track layout changed");
        if(std::none_of(d.tracks.begin(),d.tracks.end(),[&](const auto &x){return x.track==t.id;}))d.tracks.push_back({t.id,t.layout});
        for(const auto &lane:t.takeLanes)for(const auto &c:lane.clips){const auto &retained=find(find(now.takeLanes,lane.id).clips,c.id);check(retained==c,"Historical retained take changed");
            const ClipTakeOrigin origin{t.id,lane.id,c.id};
            if(std::none_of(d.retained.begin(),d.retained.end(),[&](const auto &x){return x.origin==origin;}))d.retained.push_back({origin,c,find(selected.assets,c.assetId)});
        }
    }
}
}
const CompHistoryReviewData &ReviewedCompRegenerationHistory::data() const {check(bool(data_),"Comp history review was moved from");return *data_;}
void verifyCompRegenerationHistoryCurrent(const Session &s,const CompHandlePlanContext &context,const ReviewedCompRegenerationHistory &review){
    const auto &d=review.data();check(bool(d.source),"Comp history review lost source");verifyCompRegenerationSourceCurrent(s,context,*d.source);
    for(const auto &a:d.assets)check(find(s.assets,a.id)==a,"Reviewed historical asset changed");
    for(const auto &h:d.history){const auto *p=findCompPlaybackSource(s,h.source.rendered.id);check(p && *p==h,"Reviewed historical source changed");}
    for(const auto &r:d.selections)check(std::find(s.compPairSelections.begin(),s.compPairSelections.end(),r)!=s.compPairSelections.end(),"Reviewed historical selection changed");
    for(const auto &t:d.tracks)check(find(s.tracks,t.track).layout==t.layout,"Reviewed historical track layout changed");
    for(const auto &r:d.retained)check(find(find(find(s.tracks,r.origin.trackId).takeLanes,r.origin.laneId).clips,r.origin.clipId)==r.clip,"Reviewed historical retained take changed");
}
ReviewedCompRegenerationHistory reviewCompRegenerationHistory(const std::filesystem::path &root,
    const Session &s,const CompHandlePlanContext &context,std::shared_ptr<const CompRegenerationSource> source,
    ResourceLedger ledger,CompHistoryReviewLimits limits,std::stop_token stop){
    poll(stop);check(bool(source),"Missing comp history source");
    check(limits.maximumAssemblies>0 && limits.maximumAssemblies<=1024 && limits.maximumComparedSamples>0 && limits.maximumComparedSamples<=1024ULL*1024*1024 && limits.maximumInputFileBytes>0 && limits.maximumInputFileBytes<=8ULL*1024*1024*1024,"Invalid comp history review bounds");
    verifyCompRegenerationSourceCurrent(s,context,*source);
    PayloadCharge charge("Comp history dependency review",SIZE_MAX);charge.add(sessionPayloadBytes(s),8);charge.add(65536);charge.add(limits.maximumAssemblies,sizeof(CompHistoryReviewEntry)+512);
    auto lease=ledger.reserve(charge.bytes());validate(s);
    CompHistoryReviewData d;d.source=std::move(source);d.history=d.source->data().history;
    std::set<std::string> operations,pending,counted;std::uint64_t total=0;
    const auto admit=[&]{for(const auto &h:d.history){poll(stop);
        if(operations.insert(h.assembly.operation.str()).second)pending.insert(h.assembly.operation.str());
        check(operations.size()<=limits.maximumAssemblies,"Comp history exceeds assembly count",ErrorCode::ResourceLimit);
        if(!counted.insert(h.source.rendered.id.str()).second)continue;
        const auto &a=h.source.rendered;check(a.frames>0 && a.layout.channels>0,"Invalid historical output geometry");
        check(std::uint64_t(a.frames)<=(limits.maximumComparedSamples-total)/a.layout.channels,"Comp history exceeds comparison budget",ErrorCode::ResourceLimit);total+=std::uint64_t(a.frames)*a.layout.channels;
    }};
    admit();
    check(!operations.empty(),"Empty comp assembly history");
    // Iterate once per unique owned assembly. Never recursively rerender history.
    while(!pending.empty()){poll(stop);const auto op=*pending.begin();pending.erase(pending.begin());
        const auto h=*std::find_if(d.history.begin(),d.history.end(),[&](const auto &x){return x.assembly.operation.str()==op;});
        auto pair=loadCompPairSelectionSnapshot(root,context,h.assembly.selectionOperation,h.selectionSha256,ledger,stop);
        capture(pair.recipe().selection(),s,d);admit();
        // Preflight extents against the current grant before historical reads.
        // This relies on the same immutable-owned-file contract as the original
        // verifier; it is not a cumulative or OS-enforced I/O budget.
        {ApprovedMediaRoot approved(root,ledger,{1});
            const auto extent=[&](const std::string &path){poll(stop);auto file=approved.open(path,limits.maximumInputFileBytes,stop);file.verifyUnchanged();};
            for(const auto &a:pair.recipe().selection().assets)extent(a.relativePath);
            for(const auto &a:d.history)if(a.assembly.operation==h.assembly.operation)extent(a.source.rendered.relativePath);
            for(const auto &child:pair.children())if(child)extent("media/comp-spans/"+child->operation().str()+"/audio.wav");
        }
        PayloadCharge temporary("Historical comp selection staging",SIZE_MAX);temporary.add(sessionPayloadBytes(pair.recipe().selection()),4);temporary.add(65536);
        auto temporaryLease=ledger.reserve(temporary.bytes());auto selected=pair.recipe().selection();
        const CompPairSelectionReference ref{h.assembly.selectionOperation,h.selectionSha256};
        add(selected.compPairSelections,ref);selected.compAssemblies.push_back(h.assembly);validate(selected);
        auto request=loadReferencedCompAssemblyRequest(root,selected,context,pair,h.assembly.operation,ledger,stop);
        std::uint64_t expectedSamples=0;
        for(unsigned side=0;side<2;++side)if(h.assembly.outputs[side]){
            const auto p=std::find_if(d.history.begin(),d.history.end(),[&](const auto &x){return x.source.rendered.id==*h.assembly.outputs[side];});
            check(p!=d.history.end() && p->assembly==h.assembly && p->side==side && p->selectionSha256==h.selectionSha256,"Historical paired output closure incomplete");
            expectedSamples+=std::uint64_t(p->source.rendered.frames)*p->source.rendered.layout.channels;
        }
        const auto verified=verifyCompAssembly(root,selected,context,pair,request,ledger,stop);poll(stop);
        check(verified.requestDigest()==h.assembly.requestSha256 && verified.comparedSamples()==expectedSamples,"Historical assembly verification identity changed");
        for(unsigned side=0;side<2;++side)if(verified.outputs()[side]){
            const auto *p=findCompPlaybackSource(s,verified.outputs()[side]->id);check(p && p->assembly==h.assembly && p->side==side && p->selectionSha256==h.selectionSha256,"Historical completion differs from source identity");
            auto playback=prepareCompPlaybackSource(selected,context,pair.recipe().plan(),side,verified.outputs()[side],ledger);
            check(playback.data()==p->source,"Historical playback provenance differs from original recipe",ErrorCode::MediaMismatch);
        }
        d.entries.push_back({h.assembly,h.selectionSha256,verified.comparedSamples()});
    }
    auto result=ReviewedCompRegenerationHistory(std::move(lease),std::make_unique<const CompHistoryReviewData>(std::move(d)));
    verifyCompRegenerationHistoryCurrent(s,context,result);poll(stop);return result;
}
} // namespace soundcurrent::daw
