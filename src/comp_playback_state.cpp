// SPDX-License-Identifier: GPL-3.0-only
#include <soundcurrent/comp_playback_state.hpp>
#include <soundcurrent/comp_handle_plan.hpp>
#include <soundcurrent/clip_timing.hpp>
#include <soundcurrent/warp.hpp>
#include <algorithm>
#include <unordered_map>
#include <unordered_set>

namespace soundcurrent::daw {
namespace {
void require(bool v,const char *m){if(!v)throw ProjectError(ErrorCode::InvalidState,m);}
bool hash(const std::string &s){return s.size()==64 && std::all_of(s.begin(),s.end(),[](char c){return (c>='0' && c<='9') || (c>='a' && c<='f');});}
SourcePosition interpolate(SourcePosition p,const CompHandlePoint &a,const CompHandlePoint &b){
    const auto ratio=sourceIntervalRatio(subtractSourcePosition(b.raw,a.raw),subtractSourcePosition(b.output,a.output));
    return addSourcePosition(a.raw,scaleSourcePosition(subtractSourcePosition(p,a.output),ratio.numerator,ratio.denominator));
}
void geometry(const Clip &c,const Asset &a,std::uint32_t rate){
    require(c.startFrame>=0 && c.lengthFrames>0 && c.startFrame<=INT64_MAX-c.lengthFrames,"Invalid historical comp clip extent");
    validateClipProcessing(c.processing);
    require(clipSourceMap(c,a.sampleRate,rate).at(c.lengthFrames-1).frame<a.frames,"Historical comp clip exceeds its asset");
}
}
std::size_t compPlaybackSourcePayloadBytes(const CompPlaybackSourceState &state){
    PayloadCharge charge("Comp playback provenance",SIZE_MAX);
    charge.add(sizeof(state));
    const auto string=[&](const std::string &s){charge.add(s.capacity());};
    const auto id=[&](const Id &v){string(v.str());};
    id(state.assembly.operation);id(state.assembly.selectionOperation);string(state.assembly.requestSha256);
    string(state.selectionSha256);
    for(const auto &v:state.assembly.outputs)if(v)id(*v);
    const auto &d=state.source;
    for(const auto *c:{&d.original,&d.playback}){
        id(c->id);id(c->assetId);
        if(c->takeOrigin){id(c->takeOrigin->trackId);id(c->takeOrigin->laneId);id(c->takeOrigin->clipId);}
        if(c->stretch){const auto &p=*c->stretch;id(p.sourceAssetId);string(p.sourceSha256);string(p.renderKey);string(p.processor);if(p.warp)charge.add(warpPayloadBytes(*p.warp));}
        if(c->compHandles){id(c->compHandles->baseAssetId);string(c->compHandles->baseSha256);string(c->compHandles->renderKey);}
    }
    const auto asset=[&](const Asset &a){id(a.id);string(a.relativePath);string(a.sha256);charge.add(a.nativeEvidence.capacity(),sizeof(ProjectEvidenceFile));for(const auto &f:a.nativeEvidence){string(f.relativePath);string(f.sha256);}};
    asset(d.raw);asset(d.base);asset(d.rendered);if(d.nominal)asset(*d.nominal);
    charge.add(d.rawMap.capacity(),sizeof(CompHandlePoint));
    charge.add(d.rawMap.size()+4,sizeof(CompHandlePoint)*3); // Validation map staging.
    charge.add(1024); // Asset lineage/cycle indices.
    return charge.bytes();
}
const CompPlaybackSourceState *findCompPlaybackSource(const Session &s,const Id &asset){
    const auto it=std::find_if(s.compPlaybackSources.begin(),s.compPlaybackSources.end(),[&](const auto &p){return p.source.rendered.id==asset;});
    return it==s.compPlaybackSources.end()?nullptr:&*it;
}
SourcePosition compPlaybackRawPosition(const CompPlaybackSourceData &d,SourcePosition position){
    position=scaleSourcePosition(position,1,1);
    require(!sourcePositionLess({d.rendered.frames,0,1},position),"Comp provenance position exceeds artifact");
    require(d.rawMap.size()>=2,"Missing comp playback map");
    const auto expanded=addSourcePosition(d.expandedOrigin,scaleSourcePosition(position,d.expandedNumerator,d.expandedDenominator));
    const auto right=std::upper_bound(d.rawMap.begin(),d.rawMap.end(),expanded,[](auto p,const auto &k){return sourcePositionLess(p,k.output);});
    require(right!=d.rawMap.begin(),"Comp provenance precedes expanded source");
    return right==d.rawMap.end()?interpolate(expanded,d.rawMap[d.rawMap.size()-2],d.rawMap.back()):interpolate(expanded,*(right-1),*right);
}
void validateCompPlaybackSources(const Session &s){
    if(s.compPlaybackSources.empty())return;
    std::unordered_map<std::string_view,const Asset *> assets;
    for(const auto &a:s.assets)assets.emplace(a.id.str(),&a);
    std::unordered_map<std::string_view,const CompPlaybackSourceState *> records;
    std::unordered_map<std::string_view,const CompAssemblyReference *> intents;
    for(const auto &r:s.compPlaybackSources){
        const auto &d=r.source;
        require(records.emplace(d.rendered.id.str(),&r).second,"Duplicate comp playback source");
        require(r.side<2 && hash(r.assembly.requestSha256) && r.assembly.outputs[r.side]==d.rendered.id,"Invalid adopted comp request binding");
        require(r.assembly.outputs[0]!=r.assembly.outputs[1],"Comp request repeats its output");
        require(hash(r.selectionSha256) && std::any_of(s.compPairSelections.begin(),s.compPairSelections.end(),[&](const auto &p){return p.operation==r.assembly.selectionOperation && p.selectionSha256==r.selectionSha256;}),"Comp provenance lost its selection reference");
        const auto [it,inserted]=intents.emplace(r.assembly.operation.str(),&r.assembly);
        require(inserted || *it->second==r.assembly,"Paired comp provenance has inconsistent intent");
        require(std::none_of(s.compAssemblies.begin(),s.compAssemblies.end(),[&](const auto &v){return v.operation==r.assembly.operation;}),"Adopted comp still has output reservations");
        const auto match=[&](const Asset &a){const auto found=assets.find(a.id.str());require(found!=assets.end() && *found->second==a,"Comp provenance asset declaration changed");};
        match(d.raw);match(d.base);match(d.rendered);if(d.nominal)match(*d.nominal);
        require(d.raw.id!=d.rendered.id && d.base.id!=d.rendered.id && d.rendered.nativeEvidence.empty(),"Comp output reuses source/capture identity");
        require(d.rendered.relativePath=="media/comp-results/"+r.assembly.selectionOperation.str()+"/"+d.rendered.id.str()+".wav","Comp provenance points outside its owned assembly");
        require(d.projectRate>=8000 && d.projectRate<=384000 && d.original.assetId==d.base.id && d.playback.assetId==d.rendered.id &&
            d.original.id==d.playback.id && d.original.takeOrigin && d.original.takeOrigin==d.playback.takeOrigin &&
            !d.playback.stretch && !d.playback.compHandles,"Invalid comp provenance clip snapshots");
        require(d.raw.layout==d.base.layout && d.base.layout==d.rendered.layout,"Comp provenance changed channel layout");
        require(d.prefixFrames>=0 && d.suffixFrames>=0 && d.base.frames<=1000000000 &&
            d.prefixFrames<=1000000000-d.base.frames && d.suffixFrames<=1000000000-d.base.frames-d.prefixFrames,"Invalid comp expanded extent");
        const auto frames=d.prefixFrames+d.base.frames+d.suffixFrames;
        require(d.rawMap.size()>=2 && d.rawMap.size()<=16388 && d.rawMap.front().output==SourcePosition{} &&
            d.rawMap.back().output==SourcePosition{frames,0,1},"Invalid comp provenance map extent");
        for(std::size_t i=0;i<d.rawMap.size();++i){const auto &k=d.rawMap[i];
            require(scaleSourcePosition(k.raw,1,1)==k.raw && scaleSourcePosition(k.output,1,1)==k.output,"Noncanonical comp provenance coordinate");
            if(i)require(sourcePositionLess(d.rawMap[i-1].raw,k.raw) && sourcePositionLess(d.rawMap[i-1].output,k.output),"Nonmonotone comp provenance map");
        }
        geometry(d.original,d.base,d.projectRate);geometry(d.playback,d.rendered,d.projectRate);
        require(d.playback.startFrame<=d.original.startFrame && d.playback.startFrame+d.playback.lengthFrames>=d.original.startFrame+d.original.lengthFrames,"Comp playback no longer contains original fragment");
        auto processing=d.original.processing;shiftClipProcessing(processing,d.playback.startFrame-d.original.startFrame);
        require(processing==d.playback.processing,"Historical comp processing changed");
        const auto expanded=SourceFrameMap(d.base.sampleRate,d.projectRate,
            addSourcePosition({d.original.sourceFrame,d.original.sourceTiming.fraction,d.original.sourceTiming.denominator},{d.prefixFrames,0,1}),
            d.original.playbackRate).translated(d.playback.startFrame-d.original.startFrame);
        if(d.domain==CompProcessingDomain::ProjectAfterSrc){
            require(d.rendered.sampleRate==d.projectRate && d.rendered.frames==d.playback.lengthFrames &&
                d.playback.sourceFrame==0 && d.playback.sourceTiming==ClipSourceTiming{} && d.playback.playbackRate==ClipPlaybackRate{} &&
                d.expandedOrigin==expanded.at(0) && d.expandedNumerator==expanded.numerator() && d.expandedDenominator==expanded.denominator(),"Invalid post-SRC playback grid");
        }else{
            require(d.domain==CompProcessingDomain::PhysicalArtifact && d.rendered.sampleRate==d.base.sampleRate && d.rendered.frames==frames &&
                d.expandedOrigin==SourcePosition{} && d.expandedNumerator==1 && d.expandedDenominator==1 &&
                d.playback.playbackRate==d.original.playbackRate &&
                clipSourceMap(d.playback,d.rendered.sampleRate,d.projectRate).at(0)==expanded.at(0),"Invalid physical comp playback grid");
        }
        require(sourcePositionLess(compPlaybackRawPosition(d,{d.rendered.frames-1,0,1}),{d.raw.frames,0,1}),"Comp playback provenance exceeds raw samples");
    }
    std::unordered_map<std::string_view,unsigned> colors;
    for(const auto &r:s.compPlaybackSources){
        require(!records.contains(r.source.raw.id.str()) && (!r.source.nominal || !records.contains(r.source.nominal->id.str())),"Assembled output cannot be reinterpreted as a raw or nominal stretch source");
        for(const auto &output:r.assembly.outputs)if(output){const auto it=records.find(output->str());require(it!=records.end() && it->second->assembly==r.assembly,"Adopted comp pair is incomplete");}
        // Iterative traversal bounds stack use; record count bounds total depth.
        std::vector<std::string_view> path;
        const auto *current=&r;
        while(current){
            const auto id=std::string_view(current->source.rendered.id.str());auto &color=colors[id];
            require(color!=1,"Cyclic comp provenance");if(color==2)break;
            color=1;path.push_back(id);const auto it=records.find(current->source.base.id.str());current=it==records.end()?nullptr:it->second;
        }
        for(const auto id:path)colors[id]=2;
        const auto previous=records.find(r.source.base.id.str());
        validateCompPlaybackRawMap(r.source,previous==records.end()?nullptr:&previous->second->source);
    }
}
} // namespace soundcurrent::daw
