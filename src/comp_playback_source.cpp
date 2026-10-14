// SPDX-License-Identifier: GPL-3.0-only
#include <soundcurrent/comp_playback_source.hpp>
#include <soundcurrent/clip_timing.hpp>
#include <algorithm>
#include <limits>

namespace soundcurrent::daw {
namespace {
void require(bool v,const char *message) {
    if(!v)throw ProjectError(ErrorCode::InvalidState,message);
}
SourcePosition interpolate(SourcePosition position,const CompHandlePoint &a,const CompHandlePoint &b) {
    const auto ratio=sourceIntervalRatio(subtractSourcePosition(b.raw,a.raw),
                                         subtractSourcePosition(b.output,a.output));
    return addSourcePosition(a.raw,scaleSourcePosition(subtractSourcePosition(position,a.output),
                                                      ratio.numerator,ratio.denominator));
}
}
const CompPlaybackSourceData &CompPlaybackSource::data() const {
    require(bool(data_),"Comp playback source was moved from");return *data_;
}
SourcePosition CompPlaybackSource::rawPosition(SourcePosition position) const {
    const auto &d=data();position=scaleSourcePosition(position,1,1);
    require(!sourcePositionLess({d.rendered.frames,0,1},position),"Comp provenance position exceeds artifact");
    const auto expanded=addSourcePosition(d.expandedOrigin,
        scaleSourcePosition(position,d.expandedNumerator,d.expandedDenominator));
    const auto right=std::upper_bound(d.rawMap.begin(),d.rawMap.end(),expanded,
        [](SourcePosition x,const auto &point){return sourcePositionLess(x,point.output);});
    require(right!=d.rawMap.begin(),"Comp provenance precedes expanded source");
    if(right==d.rawMap.end())return interpolate(expanded,d.rawMap[d.rawMap.size()-2],d.rawMap.back());
    return interpolate(expanded,*(right-1),*right);
}
CompPlaybackSource prepareCompPlaybackSource(const Session &s,const CompHandlePlanContext &context,
    const CompProcessingPlan &plan,unsigned side,const std::optional<Asset> &asset,ResourceLedger ledger) {
    require(side<2,"Invalid comp playback side");verifyCompProcessingCurrent(s,context,plan);
    const auto &h=(side?plan.handles().incoming():plan.handles().outgoing()).data();
    const auto &processing=side?plan.incoming():plan.outgoing();
    const bool generated=processing.domain!=CompProcessingDomain::ExistingMedia;
    require(asset.has_value()==generated,"Comp playback artifact presence differs from plan");
    const auto &rendered=asset?*asset:h.base;
    if(generated) {
        require(std::none_of(s.assets.begin(),s.assets.end(),[&](const auto &a){return a.id==rendered.id;}),
                "Comp playback artifact identity already exists");
        require(rendered.sha256.size()==64 && std::all_of(rendered.sha256.begin(),rendered.sha256.end(),
            [](char c){return (c>='0' && c<='9') || (c>='a' && c<='f');}),"Invalid comp playback artifact hash");
        validateRelativeMediaPath(rendered.relativePath);
        require(rendered.nativeEvidence.empty(),"Comp playback artifact has unrelated capture evidence");
    }
    const bool post=processing.domain==CompProcessingDomain::ProjectAfterSrc;
    require(rendered.layout==h.base.layout && rendered.sampleRate==(post?h.projectRate:h.base.sampleRate) &&
        rendered.frames==(post?h.projectEnd-h.projectFirst:h.outputFrames),
        "Comp playback artifact geometry differs from processing domain");
    // Charge source snapshots, map storage and rendered declaration before copy.
    const auto payload=sessionPayloadBytes(s);
    const auto extra=sizeof(CompPlaybackSourceData)+h.map.size()*sizeof(CompHandlePoint)+
        rendered.relativePath.capacity()+rendered.sha256.capacity()+rendered.id.str().capacity()+256;
    require(payload<=(std::numeric_limits<std::size_t>::max()-extra)/3,"Comp playback payload overflow");
    auto credit=ledger.reserve(payload*3+extra);
    CompPlaybackSourceData d;
    d.original=h.expected;d.playback=h.expected;d.raw=h.raw;d.base=h.base;d.rendered=rendered;
    d.nominal=h.nominalAsset;d.domain=processing.domain;d.projectRate=h.projectRate;d.rawMap=h.map;
    d.playback.assetId=rendered.id;d.playback.startFrame=h.projectFirst;
    d.playback.lengthFrames=h.projectEnd-h.projectFirst;
    shiftClipProcessing(d.playback.processing,h.projectFirst-h.expected.startFrame);
    if(post) {
        d.playback.sourceFrame=0;d.playback.sourceTiming={};d.playback.playbackRate={};
        const SourceFrameMap map(h.base.sampleRate,h.projectRate,h.clipOutputOrigin,h.expected.playbackRate);
        d.expandedOrigin=map.at(0);d.expandedNumerator=map.numerator();d.expandedDenominator=map.denominator();
    } else {
        d.playback.sourceFrame=h.clipOutputOrigin.frame;
        d.playback.sourceTiming={h.clipOutputOrigin.fraction,h.clipOutputOrigin.denominator};
    }
    if(generated) {
        // These describe the old nominal artifact, not the new playback asset.
        // The complete original value above is retained for canonical provenance.
        d.playback.stretch.reset();d.playback.compHandles.reset();
    }
    require(d.rawMap.size()>=2,"Missing comp playback raw map");
    const auto audible=clipSourceMap(d.playback,rendered.sampleRate,h.projectRate);
    require(audible.at(d.playback.lengthFrames-1).frame<rendered.frames,"Comp playback exceeds output samples");
    auto result=CompPlaybackSource(std::move(credit),std::make_unique<const CompPlaybackSourceData>(std::move(d)));
    require(result.rawPosition(audible.at(0))==h.requestedRawFirst &&
            result.rawPosition(audible.at(h.projectEnd-h.projectFirst-1))==h.requestedRawLast,
            "Comp playback changed requested raw timing");
    return result;
}
} // namespace soundcurrent::daw
