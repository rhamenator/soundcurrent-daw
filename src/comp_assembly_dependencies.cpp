// SPDX-License-Identifier: GPL-3.0-only
#include <soundcurrent/comp_assembly_dependencies.hpp>
#include <soundcurrent/comp_playback_state.hpp>
#include <algorithm>
namespace soundcurrent::daw {
namespace {
void check(bool good,const char *message){if(!good)throw ProjectError(ErrorCode::InvalidState,message);}
template<class T>const T &find(const std::vector<T> &v,const Id &id){const auto p=std::find_if(v.begin(),v.end(),[&](const auto &x){return x.id==id;});check(p!=v.end(),"Comp adoption dependency missing");return *p;}
template<class T>bool has(const std::vector<T> &v,const Id &id){return std::any_of(v.begin(),v.end(),[&](const auto &x){return x.id==id;});}
}
const CompAssemblyDependencyData &CompAssemblyDependencies::data() const {check(bool(data_),"Comp dependency guard was moved from");return *data_;}
CompAssemblyDependencies captureCompAssemblyDependencies(const Session &s,const Session &selection,
    std::span<const CompAssemblyDependencyView> additional,ResourceLedger ledger){
    check(selection.id==s.id && selection.sampleRate==s.sampleRate,"Comp dependency selection belongs to another project");
    PayloadCharge charge("Queued comp dependencies",SIZE_MAX);charge.add(sessionPayloadBytes(s),4);charge.add(65536);
    auto lease=ledger.reserve(charge.bytes());validate(s);
    CompAssemblyDependencyData d{s.id,s.sampleRate,{},{},{},{},{}};
    const auto asset=[&](const Asset &a){check(find(s.assets,a.id)==a,"Selected comp asset changed");if(!has(d.assets,a.id))d.assets.push_back(a);};
    const auto track=[&](const Id &id,const ChannelLayout &layout){check(find(s.tracks,id).layout==layout,"Selected comp track layout changed");
        if(std::none_of(d.tracks.begin(),d.tracks.end(),[&](const auto &t){return t.track==id;}))d.tracks.push_back({id,layout});};
    const auto reference=[&](const CompPairSelectionReference &r){check(std::find(s.compPairSelections.begin(),s.compPairSelections.end(),r)!=s.compPairSelections.end(),"Selected comp history reference changed");
        if(std::find(d.selections.begin(),d.selections.end(),r)==d.selections.end())d.selections.push_back(r);};
    const auto retained=[&](const ClipTakeOrigin &o,const Clip &clip,const Asset &a){const auto &t=find(s.tracks,o.trackId);track(t.id,t.layout);
        check(find(find(t.takeLanes,o.laneId).clips,o.clipId)==clip && clip.assetId==a.id,"Selected comp retained source changed");asset(a);
        if(std::none_of(d.retained.begin(),d.retained.end(),[&](const auto &r){return r.origin==o;}))d.retained.push_back({o,clip});
        if(clip.stretch)asset(find(s.assets,clip.stretch->sourceAssetId));
        if(clip.compHandles)asset(find(s.assets,clip.compHandles->baseAssetId));};
    const auto origin=[&](const Clip &c){if(c.takeOrigin){const auto &o=*c.takeOrigin;const auto &clip=find(find(find(s.tracks,o.trackId).takeLanes,o.laneId).clips,o.clipId);retained(o,clip,find(s.assets,clip.assetId));}};
    const auto historical=[&](const CompPlaybackSourceState &h){const auto *now=findCompPlaybackSource(s,h.source.rendered.id);check(now && *now==h,"Selected comp history changed");asset(h.source.rendered);};
    for(const auto &a:selection.assets)asset(a);
    for(const auto &h:selection.compPlaybackSources)historical(h);
    for(const auto &r:selection.compPairSelections)reference(r);
    for(const auto &t:selection.tracks){track(t.id,t.layout);for(const auto &c:t.clips){asset(find(s.assets,c.assetId));origin(c);}
        for(const auto &lane:t.takeLanes)for(const auto &c:lane.clips)retained({t.id,lane.id,c.id},c,find(s.assets,c.assetId));}
    for(const auto &v:additional){for(const auto &a:v.assets)asset(a);for(const auto &h:v.history)historical(h);for(const auto &r:v.selections)reference(r);
        for(const auto &r:v.retained)retained(r.origin,r.clip,r.asset);
        for(const auto &t:v.tracks)track(t.track,t.layout);}
    // Every newly reached asset is visited once; paired siblings and their own
    // ancestors cannot be omitted by choosing only one side of an assembly.
    for(std::size_t index=0;index<d.assets.size();++index){const auto id=d.assets[index].id;
        if(const auto *h=findCompPlaybackSource(s,id)){d.history.push_back(*h);asset(h->source.raw);asset(h->source.base);if(h->source.nominal)asset(*h->source.nominal);
            for(const auto &out:h->assembly.outputs)if(out)asset(find(s.assets,*out));
            reference({h->assembly.selectionOperation,h->selectionSha256});origin(h->source.original);origin(h->source.playback);}
    }
    auto result=CompAssemblyDependencies(std::move(lease),std::make_unique<const CompAssemblyDependencyData>(std::move(d)));
    verifyCompAssemblyDependencies(s,result);return result;
}
void verifyCompAssemblyDependencies(const Session &s,const CompAssemblyDependencies &guard){const auto &d=guard.data();
    check(s.id==d.project && s.sampleRate==d.sampleRate,"Queued comp project identity or clock changed");
    for(const auto &a:d.assets)check(find(s.assets,a.id)==a,"Queued comp asset changed");
    for(const auto &h:d.history){const auto *p=findCompPlaybackSource(s,h.source.rendered.id);check(p && *p==h,"Queued comp history changed");}
    for(const auto &r:d.selections)check(std::find(s.compPairSelections.begin(),s.compPairSelections.end(),r)!=s.compPairSelections.end(),"Queued comp history reference changed");
    for(const auto &t:d.tracks)check(find(s.tracks,t.track).layout==t.layout,"Queued comp track layout changed");
    for(const auto &r:d.retained)check(find(find(find(s.tracks,r.origin.trackId).takeLanes,r.origin.laneId).clips,r.origin.clipId)==r.clip,"Queued comp retained source changed");
}
void verifyCompAssemblyDependencyCoverage(const ApplyCompAssembly &edit){check(bool(edit.dependencies),"Comp adoption requires a dependency guard");const auto &d=edit.dependencies->data();
    check(std::any_of(d.tracks.begin(),d.tracks.end(),[&](const auto &t){return t.track==edit.transition.track;}),"Comp guard lacks transition track");
    for(const auto &e:edit.expected){for(const auto *a:{&e.raw,&e.base,&e.retainedAsset})check(find(d.assets,a->id)==*a,"Comp guard lacks an immediate source");
        if(e.nominal)check(find(d.assets,e.nominal->id)==*e.nominal,"Comp guard lacks nominal source");
        check(e.clip.takeOrigin.has_value(),"Comp guard needs retained origin");
        check(std::any_of(d.retained.begin(),d.retained.end(),[&](const auto &r){return r.origin==*e.clip.takeOrigin && r.clip==e.retained;}),"Comp guard lacks retained source");}
}
} // namespace soundcurrent::daw
