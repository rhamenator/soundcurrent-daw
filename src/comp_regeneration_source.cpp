// SPDX-License-Identifier: GPL-3.0-only
#include <soundcurrent/comp_regeneration_source.hpp>
#include <soundcurrent/comp_playback_state.hpp>
#include <soundcurrent/warp.hpp>
#include <algorithm>
#include <set>
namespace soundcurrent::daw {
namespace {
void check(bool v,const char *m){if(!v)throw ProjectError(ErrorCode::InvalidState,m);}
template<class T>const T &find(const std::vector<T> &v,const Id &id){const auto p=std::find_if(v.begin(),v.end(),[&](const auto &x){return x.id==id;});check(p!=v.end(),"Comp regeneration dependency missing");return *p;}
void contextValid(const CompHandlePlanContext &c){check(c.projectEpoch && !c.rootIdentity.empty() && c.rootIdentity.size()<=4096 && c.rootIdentity.find('\0')==std::string::npos,"Invalid comp regeneration owner");}
SourcePosition interpolate(SourcePosition p,const CompHandlePoint &a,const CompHandlePoint &b){const auto ratio=sourceIntervalRatio(subtractSourcePosition(b.raw,a.raw),subtractSourcePosition(b.output,a.output));return addSourcePosition(a.raw,scaleSourcePosition(subtractSourcePosition(p,a.output),ratio.numerator,ratio.denominator));}
}
const CompRegenerationSourceData &CompRegenerationSource::data() const {check(bool(data_),"Comp regeneration source was moved from");return *data_;}
SourcePosition compRegenerationRawPosition(const CompRegenerationSourceData &d,SourcePosition at){
    at=scaleSourcePosition(at,1,1);check(d.rawMap.size()>=2 && !sourcePositionLess({d.current.frames,0,1},at),"Regeneration position exceeds current artifact");
    const auto p=std::upper_bound(d.rawMap.begin(),d.rawMap.end(),at,[](auto x,const auto &k){return sourcePositionLess(x,k.output);});
    check(p!=d.rawMap.begin(),"Regeneration position precedes current artifact");
    return p==d.rawMap.end()?interpolate(at,d.rawMap[d.rawMap.size()-2],d.rawMap.back()):interpolate(at,*(p-1),*p);
}
CompRegenerationSource resolveCompRegenerationSource(const Session &s,const Id &trackId,const Id &clipId,
    const CompHandlePlanContext &context,ResourceLedger ledger,std::size_t maximumMapPoints){
    contextValid(context);check(maximumMapPoints>=2 && maximumMapPoints<=16388,"Invalid regeneration map limit");
    PayloadCharge payload("Comp regeneration source closure",SIZE_MAX);payload.add(sessionPayloadBytes(s),8);
    payload.add(maximumMapPoints,sizeof(CompHandlePoint));payload.add(context.rootIdentity.size()+65536);
    auto lease=ledger.reserve(payload.bytes());validate(s);
    const auto &track=find(s.tracks,trackId);const auto &clip=find(track.clips,clipId);
    const auto *leaf=findCompPlaybackSource(s,clip.assetId);check(leaf,"Comp regeneration needs an assembled source");
    CompRegenerationSourceData d;d.context=context;d.project=s.id;d.track=trackId;d.projectRate=s.sampleRate;d.trackLayout=track.layout;
    d.expected=clip;d.current=find(s.assets,clip.assetId);d.raw=leaf->source.raw;
    // Source-map normalization is exact even for fractional crops, mixed rates,
    // varispeed and piecewise raw timing. Do not interpret current PCM as raw.
    const auto &a=leaf->source;const auto first=a.expandedOrigin;
    const auto end=addSourcePosition(first,scaleSourcePosition({a.rendered.frames,0,1},a.expandedNumerator,a.expandedDenominator));
    d.rawMap.reserve(std::min(maximumMapPoints,a.rawMap.size()+2));
    const auto point=[&](CompHandlePoint p){check(d.rawMap.size()<maximumMapPoints,"Regeneration map exceeds admitted points");d.rawMap.push_back(p);};
    point({compPlaybackRawPosition(a,{}),{}});
    for(const auto &p:a.rawMap)if(sourcePositionLess(first,p.output) && sourcePositionLess(p.output,end))
        point({p.raw,scaleSourcePosition(subtractSourcePosition(p.output,first),a.expandedDenominator,a.expandedNumerator)});
    point({compPlaybackRawPosition(a,{a.rendered.frames,0,1}),{a.rendered.frames,0,1}});
    const auto *ancestor=leaf;
    for(std::size_t depth=0;;++depth){
        check(depth<s.compPlaybackSources.size(),"Cyclic comp regeneration ancestry");
        if(ancestor->source.domain==CompProcessingDomain::ProjectAfterSrc){const auto rate=ancestor->source.original.playbackRate;
            d.bakedPitchScale=scaleSourcePosition(d.bakedPitchScale,rate.numerator,rate.denominator);}
        const auto *previous=findCompPlaybackSource(s,ancestor->source.base.id);
        if(!previous){d.originalNominalClip=ancestor->source.original;
            if(d.originalNominalClip.stretch)d.originalNominal=ancestor->source.nominal?ancestor->source.nominal:std::optional<Asset>(ancestor->source.base);
            break;
        }
        ancestor=previous;
    }
    check(!d.originalNominalClip.stretch || (d.originalNominalClip.stretch->sourceAssetId==d.raw.id && d.originalNominalClip.stretch->sourceSha256==d.raw.sha256),"Regeneration nominal lost original raw identity");
    d.backendClockScale=scaleSourcePosition(d.bakedPitchScale,d.raw.sampleRate,d.current.sampleRate);
    // A paired completion and its ancestors are dependencies even if only one
    // side is being resolved. Include historical retained origins as well.
    const auto less=[](const Id &a,const Id &b){return a.str()<b.str();};
    std::set<Id,decltype(less)> pending,seen;std::set<std::string> origins,selections;
    pending.insert(clip.assetId);
    const auto retain=[&](const Clip &c){if(!c.takeOrigin)return;const auto &o=*c.takeOrigin;
        if(!origins.insert(o.clipId.str()).second)return;
        const auto &t=find(s.tracks,o.trackId);const auto &lane=find(t.takeLanes,o.laneId);const auto &retained=find(lane.clips,o.clipId);
        const auto &asset=find(s.assets,retained.assetId);d.retained.push_back({o,retained,asset});pending.insert(asset.id);
        if(retained.stretch)pending.insert(retained.stretch->sourceAssetId);
        if(retained.compHandles)pending.insert(retained.compHandles->baseAssetId);
    };
    retain(clip);
    while(!pending.empty()){
        const auto id=*pending.begin();pending.erase(pending.begin());if(!seen.insert(id).second)continue;
        const auto &asset=find(s.assets,id);d.assets.push_back(asset);
        if(const auto *r=findCompPlaybackSource(s,id)){
            d.history.push_back(*r);pending.insert(r->source.raw.id);pending.insert(r->source.base.id);
            if(r->source.nominal)pending.insert(r->source.nominal->id);
            for(const auto &output:r->assembly.outputs)if(output)pending.insert(*output);
            retain(r->source.original);retain(r->source.playback);
            if(selections.insert(r->assembly.selectionOperation.str()).second){
                const CompPairSelectionReference ref{r->assembly.selectionOperation,r->selectionSha256};
                check(std::find(s.compPairSelections.begin(),s.compPairSelections.end(),ref)!=s.compPairSelections.end(),"Regeneration selection reference changed");d.selections.push_back(ref);
            }
        }
    }
    auto result=CompRegenerationSource(std::move(lease),std::make_unique<const CompRegenerationSourceData>(std::move(d)));
    verifyCompRegenerationSourceCurrent(s,context,result);return result;
}
void verifyCompRegenerationSourceCurrent(const Session &s,const CompHandlePlanContext &context,const CompRegenerationSource &source){
    contextValid(context);const auto &d=source.data();check(d.context==context && d.project==s.id && d.projectRate==s.sampleRate,"Comp regeneration owner changed");
    const auto &track=find(s.tracks,d.track);check(track.layout==d.trackLayout && find(track.clips,d.expected.id)==d.expected,"Comp regeneration clip changed");
    for(const auto &a:d.assets)check(find(s.assets,a.id)==a,"Comp regeneration source declaration changed");
    for(const auto &h:d.history){const auto *r=findCompPlaybackSource(s,h.source.rendered.id);check(r && *r==h,"Comp regeneration history changed");}
    for(const auto &r:d.selections)check(std::find(s.compPairSelections.begin(),s.compPairSelections.end(),r)!=s.compPairSelections.end(),"Comp regeneration parent reference changed");
    for(const auto &r:d.retained){const auto &t=find(s.tracks,r.origin.trackId);const auto &lane=find(t.takeLanes,r.origin.laneId);
        check(find(lane.clips,r.origin.clipId)==r.clip && find(s.assets,r.asset.id)==r.asset,"Comp regeneration retained take changed");}
}
} // namespace soundcurrent::daw
