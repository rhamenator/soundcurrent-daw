// SPDX-License-Identifier: GPL-3.0-only
#include <soundcurrent/clip_timing.hpp>
#include <numeric>
#include <limits>

namespace soundcurrent::daw {
namespace {
void require(bool v, const char *m) {
    if (!v) throw ProjectError(ErrorCode::InvalidParameter,m);
}
SourcePosition scale(std::uint64_t frames, std::uint32_t numerator, std::uint32_t denominator) {
    // Quotient/remainder decomposition avoids the overflowing frames*n product
    // without non-portable __int128 or floating-point position accumulation.
    const auto whole = frames / denominator;
    const auto partial = (frames % denominator) * numerator;
    constexpr auto maximum = std::uint64_t(std::numeric_limits<Frame>::max());
    require(whole <= maximum / numerator,"Source/project timing overflow");
    const auto base = whole * numerator, add = partial / denominator;
    require(add <= maximum - base,"Source/project timing overflow");
    return {Frame(base + add),partial % denominator,denominator};
}
}
SourceFrameMap::SourceFrameMap(std::uint32_t source, std::uint32_t project, Frame origin) {
    require(source >= 8000 && source <= 384000 && project >= 8000 && project <= 384000,
            "Invalid source/project sample rate");
    require(origin >= 0,"Negative source origin");
    const auto divisor=std::gcd(source,project);
    numerator_=source/divisor;denominator_=project/divisor;
    origin_={origin,0,denominator_};
}
SourceFrameMap::SourceFrameMap(std::uint32_t source, std::uint32_t project, SourcePosition origin)
    :SourceFrameMap(source,project,origin.frame) {
    require(origin.denominator && origin.fraction<origin.denominator,"Invalid source fraction");
    const auto divisor=std::gcd(origin.fraction,origin.denominator);
    origin.fraction/=divisor;origin.denominator/=divisor;
    const auto common=std::gcd(origin.denominator,std::uint64_t(denominator_));
    require(origin.denominator/common<=UINT64_MAX/denominator_,"Source denominator overflow");
    const auto denominator=origin.denominator/common*denominator_;
    origin_={origin.frame,origin.fraction*(denominator/origin.denominator),denominator};
}
SourcePosition SourceFrameMap::at(Frame offset) const {
    require(offset>=0,"Negative timing offset");
    auto p=scale(std::uint64_t(offset),numerator_,denominator_);
    const auto fraction=p.fraction*(origin_.denominator/denominator_);
    const bool carry=fraction>=origin_.denominator-origin_.fraction;
    p.fraction=carry ? fraction-(origin_.denominator-origin_.fraction) : fraction+origin_.fraction;
    constexpr auto maximum=std::numeric_limits<Frame>::max();
    require(p.frame <= maximum-origin_.frame,"Source origin overflow");
    p.frame+=origin_.frame;
    require(carry <= std::uint64_t(maximum-p.frame),"Source fraction carry overflow");
    p.frame+=Frame(carry);p.denominator=origin_.denominator;
    return p;
}
SourceFrameMap SourceFrameMap::advanced(Frame offset) const {
    auto next=*this;next.origin_=at(offset);return next;
}
SourceFrameMap SourceFrameMap::translated(Frame offset) const {
    if(offset>=0) return advanced(offset);
    const auto magnitude=std::uint64_t(-(offset+1))+1;
    auto p=scale(magnitude,numerator_,denominator_);
    const auto fraction=p.fraction*(origin_.denominator/denominator_);
    const bool borrow=origin_.fraction<fraction;
    require(origin_.frame>=p.frame && (!borrow || origin_.frame>p.frame),"Source origin underflow");
    auto next=*this;
    next.origin_.frame=origin_.frame-p.frame-Frame(borrow);
    next.origin_.fraction=borrow ? origin_.denominator-(fraction-origin_.fraction) : origin_.fraction-fraction;
    return next;
}
Frame SourceFrameMap::projectFramesForSource(Frame frames) const {
    require(frames>=0,"Negative source duration");
    const auto p=scale(std::uint64_t(frames),denominator_,numerator_);
    require(!p.fraction || p.frame<std::numeric_limits<Frame>::max(),
            "Project duration rounding overflow");
    return p.frame + (p.fraction ? 1 : 0);
}
SourceFrameMap clipSourceMap(const Clip &c,std::uint32_t source,std::uint32_t project) {
    return SourceFrameMap(source,project,SourcePosition{c.sourceFrame,c.sourceTiming.fraction,c.sourceTiming.denominator});
}
} // namespace soundcurrent::daw
