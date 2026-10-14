// SPDX-License-Identifier: GPL-3.0-only
#include <soundcurrent/comp_processing_plan.hpp>
#include <soundcurrent/positioned_resampling.hpp>
#include <algorithm>
namespace soundcurrent::daw {
namespace {
void require(bool v,const char *m){if(!v)throw ProjectError(ErrorCode::InvalidParameter,m);}
Frame ceiling(SourcePosition p){require(p.frame>=0 && (!p.fraction || p.frame<INT64_MAX),"Permission coordinate overflow");return p.frame+Frame(p.fraction!=0);}
CompFragmentProcessing prepare(const CompHandleEnvelope &envelope,const PrepareCompTransition &e,Frame maximum){
    const auto &p=envelope.data();CompFragmentProcessing result;
    if(!p.needsNewSpan)return result;
    const auto &c=p.expected;const auto oldEnd=c.startFrame+c.lengthFrames;
    const auto begin=std::max(e.startFrame,c.startFrame),end=std::min(e.endFrame,oldEnd);
    require(begin<end,"Generated join lacks a requested inner processing region");
    const auto projectWidth=std::min(maximum,end-begin);
    SourceFrameMap expanded(p.base.sampleRate,p.projectRate,p.clipOutputOrigin,c.playbackRate);
    PreparedPositionedResampling kernel(expanded,p.base.layout.channels);
    const auto first=expanded.at(e.startFrame-p.projectFirst),last=expanded.at(e.endFrame-p.projectFirst);
    const auto currentCoordinate=[&](Frame f){return std::clamp<Frame>(f-p.prefixFrames,0,p.base.frames);};
    Frame allowedFirst=currentCoordinate(ceiling(first)),allowedEnd=currentCoordinate(ceiling(last));
    // A declared source-center range is insufficient: include the full prepared
    // FIR read footprint of the closest audible sample outside the edit.
    if(e.startFrame>c.startFrame){const auto r=kernel.sourceRange(expanded,e.startFrame-1-p.projectFirst,1,p.outputFrames);
        allowedFirst=std::max(allowedFirst,currentCoordinate(r.first+Frame(r.frames)));}
    if(e.endFrame<oldEnd){const auto r=kernel.sourceRange(expanded,e.endFrame-p.projectFirst,1,p.outputFrames);
        allowedEnd=std::min(allowedEnd,currentCoordinate(r.first));}
    const auto delta=scaleSourcePosition({projectWidth,0,1},expanded.numerator(),expanded.denominator());
    const auto physicalWidth=ceiling(delta);
    bool physical=physicalWidth>0 && physicalWidth<=p.base.frames;
    if(p.prefixFrames)physical=physical && allowedFirst==0 && physicalWidth<=allowedEnd;
    if(p.suffixFrames)physical=physical && allowedEnd==p.base.frames && p.base.frames-physicalWidth>=allowedFirst;
    if(p.prefixFrames && p.suffixFrames)physical=physical && physicalWidth<=p.base.frames-physicalWidth;
    if(physical){
        result.domain=CompProcessingDomain::PhysicalArtifact;
        result.pcm={p.base.layout.channels,512,p.base.frames,p.prefixFrames,p.suffixFrames};
        if(p.prefixFrames)result.blends.prefix=CompHandleInnerBlend{physicalWidth,allowedFirst,allowedEnd};
        if(p.suffixFrames)result.blends.suffix=CompHandleInnerBlend{physicalWidth,allowedFirst,allowedEnd};
    }else{
        // Blend after both artifact streams have been independently resampled.
        // The requested project region then bounds every changed output sample.
        result.domain=CompProcessingDomain::ProjectAfterSrc;
        result.pcm={p.base.layout.channels,512,c.lengthFrames,c.startFrame-p.projectFirst,p.projectEnd-oldEnd};
        require(projectWidth<=c.lengthFrames,"Project blend width exceeds fragment");
        if(p.prefixFrames)result.blends.prefix=CompHandleInnerBlend{projectWidth,begin-c.startFrame,end-c.startFrame};
        if(p.suffixFrames)result.blends.suffix=CompHandleInnerBlend{projectWidth,begin-c.startFrame,end-c.startFrame};
        require(!result.blends.prefix || result.pcm.prefixFrames>0,"Project prefix missing");
        require(!result.blends.suffix || result.pcm.suffixFrames>0,"Project suffix missing");
    }
    return result;
}
}
CompProcessingPlan prepareCompProcessing(const Session &s,const PrepareCompTransition &e,
    const CompHandlePlanContext &context,Frame maximum,ResourceLedger ledger,CompHandlePlanLimits limits){
    require(maximum>0 && maximum<=1000000000,"Invalid maximum project blend width");
    auto credit=ledger.reserve(2*sizeof(CompFragmentProcessing)+256);
    auto handles=prepareCompTransitionHandles(s,e,context,ledger,limits);
    auto out=std::make_unique<const CompFragmentProcessing>(prepare(handles.outgoing(),e,maximum));
    auto in=std::make_unique<const CompFragmentProcessing>(prepare(handles.incoming(),e,maximum));
    return {std::move(credit),std::move(handles),std::move(out),std::move(in)};
}
void verifyCompProcessingCurrent(const Session &s,const CompHandlePlanContext &c,const CompProcessingPlan &p){
    verifyCompTransitionHandlesCurrent(s,c,p.handles());
}
const CompFragmentProcessing &CompProcessingPlan::outgoing() const {
    require(bool(outgoing_),"Comp processing plan was moved from");return *outgoing_;
}
const CompFragmentProcessing &CompProcessingPlan::incoming() const {
    require(bool(incoming_),"Comp processing plan was moved from");return *incoming_;
}
} // namespace soundcurrent::daw
