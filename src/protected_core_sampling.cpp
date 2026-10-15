// SPDX-License-Identifier: GPL-3.0-only
#include <soundcurrent/protected_core_sampling.hpp>
#include <soundcurrent/stretch.hpp>
#include <algorithm>
namespace soundcurrent::daw {
namespace {
void require(bool value,const char *message){if(!value)throw ProjectError(ErrorCode::InvalidParameter,message);}
// This ceiling selects discrete samples; it never quantizes a semantic anchor.
// Binary search avoids floating point, denominator LCM growth and endpoint
// overflow. count is a sentinel; only actual sample positions are evaluated.
Frame firstAtOrAfter(const SourceFrameMap &clock,Frame count,SourcePosition boundary,
                     const std::function<void()> &poll){
    Frame low=0,high=count;
    while(low<high){poll();const auto mid=low+(high-low)/2;
        if(sourcePositionLess(clock.at(mid),boundary))low=mid+1;else high=mid;}
    return low;
}
}
PreparedProtectedCoreSampling::PreparedProtectedCoreSampling(const ProtectedWarpPlan &plan,
    SourceFrameMap clock,Frame count,std::uint32_t channels,ResourceLedger ledger,
    const std::function<void()> &poll,ClipWarpLimits limits)
    :kernel_(clock,channels),sourceFrames_(plan.map().region().availableSourceFrames),channels_(channels){
    require(bool(poll),"Missing protected sampling cancellation poll");poll();
    require(count>0 && count<=1000000000,"Protected sampling output extent exceeds admission");
    require(sourcePositionLess(clock.at(count-1),{plan.map().region().outputFrames,0,1}),
            "Protected sampling clock exceeds original geometry");
    require(plan.spans().size()<=limits.maximumMarkers,"Protected sampling span count exceeds admission");
    PayloadCharge charge("Protected core sampling plan",limits.maximumPayloadBytes);
    charge.add(sizeof(*this)+1024);charge.add(plan.spans().size(),sizeof(ProtectedCoreWindow)+256);
    for(const auto &span:plan.spans()){poll();charge.add(span.owner.str().size()+1);}
    lease_=ledger.reserve(charge.bytes());windows_.reserve(plan.spans().size());
    const SourcePosition halo{plan.policy().halo,0,1};
    for(const auto &span:plan.spans()){
        poll();const auto begin=addSourcePosition(span.outputBegin,halo),end=subtractSourcePosition(span.outputEnd,halo);
        const auto sourceBegin=addSourcePosition(span.sourceBegin,halo),sourceEnd=subtractSourcePosition(span.sourceEnd,halo);
        require(subtractSourcePosition(end,begin)==subtractSourcePosition(sourceEnd,sourceBegin),
                "Protected core has no unity acoustic slope");
        const auto first=firstAtOrAfter(clock,count,begin,poll),last=firstAtOrAfter(clock,count,end,poll);
        if(first==last)continue;
        const auto rawBegin=plan.map().rawAt(sourceBegin),rawEnd=plan.map().rawAt(sourceEnd);
        const auto rawFirst=addSourcePosition(rawBegin,subtractSourcePosition(clock.at(first),begin));
        auto map=SourceFrameMap::processingGrid(rawFirst,
            scaleSourcePosition({last-first,0,1},clock.numerator(),clock.denominator()),last-first);
        require(sourcePositionLess(map.at(last-first-1),{sourceFrames_,0,1}),
                "Protected sample exceeds original raw asset");
        require(windows_.empty() || windows_.back().endOutput<=first,"Protected sample windows overlap");
        windows_.push_back({span.owner,rawBegin,rawEnd,begin,end,first,last,std::move(map)});
    }
    poll();
}
const ProtectedCoreWindow &PreparedProtectedCoreSampling::checked(std::size_t index,Frame offset,std::uint32_t frames) const {
    require(index<windows_.size() && offset>=0 && frames>0 && frames<=512,"Invalid protected sampling block");
    const auto &window=windows_[index];const auto count=window.endOutput-window.firstOutput;
    require(Frame(frames)<=count && offset<=count-frames,"Protected sampling block crosses a core boundary");
    return window;
}
std::size_t PreparedProtectedCoreSampling::maximumSourceWindowFrames() const {return kernel_.maximumSourceWindowFrames(512);}
SourceReadRange PreparedProtectedCoreSampling::sourceRange(std::size_t index,Frame offset,std::uint32_t frames) const {
    const auto &window=checked(index,offset,frames);return kernel_.sourceRange(window.rawSamples,offset,frames,sourceFrames_);
}
void PreparedProtectedCoreSampling::process(std::size_t index,Frame offset,Frame firstSource,
    std::span<const float> source,std::span<float> output) const {
    require(!output.empty() && output.size()%channels_==0 && output.size()/channels_<=512,"Invalid protected output layout");
    const auto &window=checked(index,offset,std::uint32_t(output.size()/channels_));
    kernel_.process(window.rawSamples,offset,sourceFrames_,firstSource,source,output);
}
} // namespace soundcurrent::daw
