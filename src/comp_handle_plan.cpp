// SPDX-License-Identifier: GPL-3.0-only
#include <soundcurrent/comp_handle_plan.hpp>
#include <soundcurrent/clip_timing.hpp>
#include <soundcurrent/warp.hpp>
#include <algorithm>
#include <numeric>
#include <limits>
namespace soundcurrent::daw {
namespace {
void require(bool v,const char *s){if(!v)throw ProjectError(ErrorCode::InvalidState,s);}
template<class T> const T &find(const std::vector<T> &v,const Id &id){auto p=std::find_if(v.begin(),v.end(),[&](const auto &x){return x.id==id;});require(p!=v.end(),"Comp handle selection no longer exists");return *p;}
struct Signed {bool negative=false;SourcePosition magnitude;};
Signed coordinate(const SourceFrameMap &m,Frame delta){
    const auto origin=m.at(0);if(delta>=0)return {false,m.at(delta)};
    require(delta!=INT64_MIN,"Comp handle offset overflow");
    const auto amount=scaleSourcePosition({-delta,0,1},m.numerator(),m.denominator());
    return sourcePositionLess(origin,amount)?Signed{true,subtractSourcePosition(amount,origin)}:Signed{false,subtractSourcePosition(origin,amount)};
}
Frame ceil(SourcePosition p){require(p.frame<INT64_MAX || !p.fraction,"Comp handle frame rounding overflow");return p.frame+Frame(p.fraction!=0);}
SourcePosition intervalScale(SourcePosition p,SourcePosition n,SourcePosition d) {
    const auto ratio=sourceIntervalRatio(n,d);
    return scaleSourcePosition(p,ratio.numerator,ratio.denominator);
}
void validateContext(const CompHandlePlanContext &context) {
    require(context.projectEpoch && !context.rootIdentity.empty() &&
            context.rootIdentity.size()<=4096 && context.rootIdentity.find('\0')==std::string::npos,
            "Invalid comp handle project revision/workspace identity");
}
SourcePosition rawAt(Signed p,std::span<const CompHandlePoint> map){
    if(p.negative){auto amount=intervalScale(p.magnitude,subtractSourcePosition(map[1].raw,map[0].raw),subtractSourcePosition(map[1].output,map[0].output));return subtractSourcePosition(map.front().raw,amount);}
    if(sourcePositionLess(map.back().output,p.magnitude)) {
        const auto &a=map[map.size()-2],&b=map.back();return addSourcePosition(b.raw,intervalScale(subtractSourcePosition(p.magnitude,b.output),subtractSourcePosition(b.raw,a.raw),subtractSourcePosition(b.output,a.output)));
    }
    if(!sourcePositionLess(p.magnitude,map.back().output))return map.back().raw;
    const auto right=std::upper_bound(map.begin(),map.end(),p.magnitude,[](SourcePosition x,const auto &k){return sourcePositionLess(x,k.output);});
    require(right!=map.begin(),"Comp handle map precedes its origin");const auto &a=*(right-1),&b=*right;
    if(p.magnitude==a.output)return a.raw;
    return addSourcePosition(a.raw,intervalScale(subtractSourcePosition(p.magnitude,a.output),subtractSourcePosition(b.raw,a.raw),subtractSourcePosition(b.output,a.output)));
}
struct PreparedMap {
    ResourceLease lease;
    std::vector<CompHandlePoint> points;
    StretchGeometry geometry;
};
PreparedMap clipMap(const Clip &c,const Asset &raw,Frame artifactFrames,ResourceLedger ledger) {
    PreparedMap nominal;
    nominal.geometry=c.stretch?stretchGeometry(*c.stretch,raw.frames):
        StretchGeometry{{0,0,1},artifactFrames,artifactFrames,{0,0,1},{artifactFrames,0,1},1,1};
    const auto &g=nominal.geometry;
    if(c.stretch && c.stretch->warp) {
        const auto &p=*c.stretch;
        ProtectedWarpPlan warp({p.sourceOrigin,raw.sampleRate,raw.frames,p.sourceFrames,g.outputFrames,
            {0,0,1},{p.sourceFrames,0,1}},p.warp->markers,p.warp->protection,ledger);
        require(warp.map().points().size()<=16388,"Handle map exceeds point limit");
        nominal.lease=ledger.reserve(warp.map().points().size()*sizeof(CompHandlePoint));
        nominal.points.reserve(warp.map().points().size());
        for(const auto &k:warp.map().points())
            nominal.points.push_back({addSourcePosition(p.sourceOrigin,k.source),k.output});
    } else {
        nominal.lease=ledger.reserve(2*sizeof(CompHandlePoint));
        nominal.points={{g.inputOrigin,{0,0,1}},
            {addSourcePosition(g.inputOrigin,{g.inputFrames,0,1}),{g.outputFrames,0,1}}};
    }
    if(!c.compHandles)return nominal;
    const auto &w=*c.compHandles;
    require(c.stretch && w.prefixFrames>=0 && w.suffixFrames>=0 &&
        g.outputFrames<=1000000000 && w.prefixFrames<=1000000000-g.outputFrames &&
        w.suffixFrames<=1000000000-g.outputFrames-w.prefixFrames,"Invalid processed handle window extent");
    const auto count=nominal.points.size()+std::size_t(w.prefixFrames!=0)+std::size_t(w.suffixFrames!=0);
    require(count<=16388,"Processed handle map exceeds point limit");
    PreparedMap expanded;
    expanded.lease=ledger.reserve(count*sizeof(CompHandlePoint));expanded.points.reserve(count);
    expanded.geometry=g;
    expanded.geometry.outputFrames=g.outputFrames+w.prefixFrames+w.suffixFrames;
    expanded.geometry.visibleBegin={0,0,1};
    expanded.geometry.visibleEnd={expanded.geometry.outputFrames,0,1};
    if(w.prefixFrames)expanded.points.push_back({rawAt({true,{w.prefixFrames,0,1}},nominal.points),{0,0,1}});
    for(auto k:nominal.points) {
        k.output=addSourcePosition(k.output,{w.prefixFrames,0,1});expanded.points.push_back(k);
    }
    if(w.suffixFrames)expanded.points.push_back({rawAt({false,{g.outputFrames+w.suffixFrames,0,1}},nominal.points),
        {expanded.geometry.outputFrames,0,1}});
    return expanded;
}
}
CompHandleEnvelope prepareCompHandleEnvelope(const Session &s,const Id &trackId,const Id &clipId,
    Frame first,Frame end,const CompHandlePlanContext &context,ResourceLedger ledger,CompHandlePlanLimits limits){
    validateContext(context);
    require(limits.maximumOutputFrames>0 && limits.maximumOutputFrames<=1000000000 &&
            limits.maximumMapPoints>=2 && limits.maximumMapPoints<=16388 &&
            limits.maximumOutputBytes>8192 && limits.maximumOutputBytes<=8ULL*1024*1024*1024,
            "Invalid handle plan limits");
    const auto bytes=sessionPayloadBytes(s);
    const auto extra=sizeof(CompHandleEnvelopeData)+context.rootIdentity.size();
    require(bytes<=(SIZE_MAX-extra)/3,"Handle plan credit overflow");
    auto credit=ledger.reserve(bytes*3+extra);
    validate(s);const auto &c=find(find(s.tracks,trackId).clips,clipId);
    require(c.takeOrigin && first>=0 && first<=c.startFrame && end>=c.startFrame+c.lengthFrames && end>first,"Handle envelope must extend a retained rendered clip");
    const auto &origin=*c.takeOrigin;const auto &retained=find(find(find(s.tracks,origin.trackId).takeLanes,origin.laneId).clips,origin.clipId);
    const auto &raw=find(s.assets,c.stretch?c.stretch->sourceAssetId:c.assetId),&base=find(s.assets,c.assetId),&retainedAsset=find(s.assets,retained.assetId);
    require(raw.id==retained.assetId || (c.stretch && retained.stretch && raw.id==retained.stretch->sourceAssetId && c.stretch->sourceSha256==retained.stretch->sourceSha256),"Handle render source differs from retained take");
    auto preparedMap=clipMap(c,raw,base.frames,ledger);
    const auto &g=preparedMap.geometry;const auto &oldMap=preparedMap.points;
    require(oldMap.size()<=limits.maximumMapPoints,"Handle map exceeds point limit");
    const auto m=clipSourceMap(c,base.sampleRate,s.sampleRate);const auto outputFirst=coordinate(m,first-c.startFrame),outputLast=coordinate(m,end-1-c.startFrame),outputEnd=coordinate(m,end-c.startFrame);
    const auto rawFirst=rawAt(outputFirst,oldMap),rawLast=rawAt(outputLast,oldMap),rawEnd=rawAt(outputEnd,oldMap);
    const auto allowed=clipSourceMap(retained,retainedAsset.sampleRate,s.sampleRate);auto allowedFirst=allowed.at(0),allowedEnd=allowed.at(retained.lengthFrames);
    if(retained.assetId!=raw.id){require(bool(retained.stretch),"Missing retained raw map");auto rg=stretchGeometry(*retained.stretch,raw.frames);if(!retained.compHandles && sourcePositionLess(rg.visibleEnd,allowedEnd))allowedEnd=rg.visibleEnd;allowedFirst=processedClipOutputToRaw(retained,raw,allowedFirst);allowedEnd=processedClipOutputToRaw(retained,raw,allowedEnd);}
        if(c.stretch)require(!sourcePositionLess(rawFirst,allowedFirst) && sourcePositionLess(rawLast,allowedEnd),"Requested handles exceed retained take");
    else require(!sourcePositionLess(rawFirst,allowedFirst) && !sourcePositionLess(allowed.at(retained.lengthFrames-1),rawLast),"Requested raw handles exceed retained take");
    const auto prefix=outputFirst.negative?ceil(outputFirst.magnitude):0;
    require(!outputLast.negative && outputLast.magnitude.frame<INT64_MAX,"Comp handle end is negative or overflowing");const auto last=outputLast.magnitude.frame+1;const auto suffix=std::max<Frame>(0,last-base.frames);
    require(base.frames<=limits.maximumOutputFrames && prefix<=limits.maximumOutputFrames-base.frames && suffix<=limits.maximumOutputFrames-base.frames-prefix,"Handle output exceeds frame limit");
    const auto frames=prefix+base.frames+suffix;
    require(std::uint64_t(frames)<=(limits.maximumOutputBytes>8192?limits.maximumOutputBytes-8192:0)/(4ULL*base.layout.channels),"Handle output exceeds byte limit");
    const auto wholeFirst=rawAt({bool(prefix),{prefix,0,1}},oldMap),wholeEnd=rawAt({false,{base.frames+suffix,0,1}},oldMap);
    // An exclusive fractional endpoint is not an actual source sample.
    require(sourcePositionLess(rawAt({false,{base.frames+suffix-1,0,1}},oldMap),{raw.frames,0,1}),
        "Full handle input exceeds real source samples");
    const auto points=oldMap.size()+std::size_t(prefix!=0)+std::size_t(suffix!=0);
    require(points<=limits.maximumMapPoints,"Expanded handle map exceeds point limit");auto mapCredit=ledger.reserve(points*sizeof(CompHandlePoint));
    CompHandleEnvelopeData result;result.context=context;result.project=s.id;result.projectRate=s.sampleRate;result.track=trackId;result.trackLayout=find(s.tracks,trackId).layout;result.expected=c;result.retained=retained;result.raw=raw;result.base=base;result.retainedAsset=retainedAsset;
    if(c.compHandles)result.nominalAsset=find(s.assets,c.compHandles->baseAssetId);
    result.projectFirst=first;result.projectEnd=end;result.prefixFrames=prefix;result.suffixFrames=suffix;result.outputFrames=frames;
    result.clipOutputOrigin=outputFirst.negative?subtractSourcePosition({prefix,0,1},outputFirst.magnitude):addSourcePosition({prefix,0,1},outputFirst.magnitude);
    result.requestedRawFirst=rawFirst;result.requestedRawLast=rawLast;result.requestedRawEnd=rawEnd;
    result.map.reserve(points);if(prefix)result.map.push_back({wholeFirst,{0,0,1}});
    for(auto point:oldMap){point.output=addSourcePosition(point.output,{prefix,0,1});result.map.push_back(point);}
    if(suffix)result.map.push_back({wholeEnd,{frames,0,1}});
    result.needsNewSpan=prefix || suffix;result.needsNominalExposure=outputFirst.negative || sourcePositionLess(outputFirst.magnitude,g.visibleBegin) || !sourcePositionLess(outputLast.magnitude,g.visibleEnd);
    // The map's credit must live as long as the vector, not just preparation.
    mapCredit.transferTo(credit,mapCredit.bytes());
    auto data=std::make_unique<const CompHandleEnvelopeData>(std::move(result));
    return CompHandleEnvelope(std::move(credit),std::move(data));
}
const CompHandleEnvelopeData &CompHandleEnvelope::data() const {
    require(bool(data_),"Comp handle plan was moved from");
    return *data_;
}
void verifyCompHandleEnvelopeCurrent(const Session &s,const CompHandlePlanContext &context,
                                    const CompHandleEnvelope &plan) {
    validateContext(context);
    const auto &p=plan.data();
    require(context==p.context,"Comp handle project revision/workspace changed after planning");
    require(s.id==p.project && s.sampleRate==p.projectRate,"Handle plan belongs to a different project/rate");
    const auto &track=find(s.tracks,p.track);require(track.layout==p.trackLayout && find(track.clips,p.expected.id)==p.expected,"Handle clip/layout changed after planning");
    require(find(s.assets,p.raw.id)==p.raw && find(s.assets,p.base.id)==p.base && find(s.assets,p.retainedAsset.id)==p.retainedAsset,"Handle source declaration changed after planning");
    if(p.nominalAsset)require(find(s.assets,p.nominalAsset->id)==*p.nominalAsset,"Nominal handle artifact changed after planning");
    require(p.expected.takeOrigin.has_value(),"Handle plan has no retained source identity");
    const auto &origin=*p.expected.takeOrigin;
    require(find(find(find(s.tracks,origin.trackId).takeLanes,origin.laneId).clips,origin.clipId)==p.retained,"Retained take changed after planning");
}
CompTransitionHandlePlan prepareCompTransitionHandles(const Session &s,const PrepareCompTransition &e,
    const CompHandlePlanContext &context,ResourceLedger ledger,CompHandlePlanLimits limits) {
    validateContext(context);
    auto validationCredit=ledger.reserve(sessionPayloadBytes(s));
    validate(s); // Must precede signed end-coordinate arithmetic on untrusted state.
    require(e.outgoing!=e.incoming,"Handle transition needs distinct fragments");
    const auto &clips=find(s.tracks,e.track).clips;const auto &out=find(clips,e.outgoing),&in=find(clips,e.incoming);
    require(out.startFrame<=in.startFrame && out.startFrame+out.lengthFrames<=in.startFrame+in.lengthFrames && in.startFrame<=out.startFrame+out.lengthFrames,"Handle transition needs ordered adjacent or overlapping fragments");
    require(e.startFrame>=out.startFrame && e.startFrame<out.startFrame+out.lengthFrames && e.endFrame>in.startFrame && e.endFrame<=in.startFrame+in.lengthFrames && e.endFrame>e.startFrame,"Handle transition range exceeds fragment pair");
    validateClipProcessing({0,false,false,{}, {0,1,e.curve,1}});
    auto outgoing=prepareCompHandleEnvelope(s,e.track,e.outgoing,out.startFrame,std::max(out.startFrame+out.lengthFrames,e.endFrame),context,ledger,limits);
    auto incoming=prepareCompHandleEnvelope(s,e.track,e.incoming,std::min(in.startFrame,e.startFrame),in.startFrame+in.lengthFrames,context,ledger,limits);
    return CompTransitionHandlePlan(e,std::move(outgoing),std::move(incoming));
}
void verifyCompTransitionHandlesCurrent(const Session &s,const CompHandlePlanContext &context,
                                        const CompTransitionHandlePlan &p) {
    verifyCompHandleEnvelopeCurrent(s,context,p.outgoing());
    verifyCompHandleEnvelopeCurrent(s,context,p.incoming());
}

SourcePosition processedClipOutputToRaw(const Clip &c,const Asset &raw,SourcePosition output) {
    output=scaleSourcePosition(output,1,1);
    if(!c.compHandles)return c.stretch?stretchOutputToRaw(*c.stretch,output):output;
    auto map=clipMap(c,raw,0,ResourceLedger(8*1024*1024,"Processed handle lookup"));
    require(!sourcePositionLess({map.geometry.outputFrames,0,1},output),"Processed handle lookup exceeds artifact");
    return rawAt({false,output},map.points);
}
void validateCompHandleWindow(const Clip &c,const Asset &raw,const Asset &nominal,const Asset &rendered) {
    require(c.compHandles && c.stretch && c.takeOrigin,"Processed handles need stretch and retained take provenance");
    const auto &w=*c.compHandles;
    const auto hash=[](const std::string &s) {
        return s.size()==64 && std::all_of(s.begin(),s.end(),[](char ch){
            return (ch>='0' && ch<='9') || (ch>='a' && ch<='f');});
    };
    require(w.baseAssetId==nominal.id && w.baseSha256==nominal.sha256 && hash(w.baseSha256) && hash(w.renderKey),
        "Processed handle nominal artifact identity mismatch");
    validateClipStretch(*c.stretch,raw,nominal);
    require(rendered.id!=raw.id && rendered.sampleRate==nominal.sampleRate && rendered.layout==nominal.layout,
        "Processed handles changed source rate/layout");
    if(rendered.id==nominal.id)require(!w.prefixFrames && !w.suffixFrames && w.renderKey==c.stretch->renderKey,
        "Reused nominal artifact has new processed samples");
    auto map=clipMap(c,raw,nominal.frames,ResourceLedger(8*1024*1024,"Processed handle validation"));
    require(rendered.frames==map.geometry.outputFrames,"Processed handle artifact extent mismatch");
    require(sourcePositionLess(rawAt({false,{rendered.frames-1,0,1}},map.points),{raw.frames,0,1}),
        "Processed handles exceed real source samples");
}

}
