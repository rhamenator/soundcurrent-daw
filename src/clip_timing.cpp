// SPDX-License-Identifier: GPL-3.0-only
#include <soundcurrent/clip_timing.hpp>
#include <numeric>
#include <limits>

namespace soundcurrent::daw {
namespace {
void require(bool v, const char *m) {
    if (!v) throw ProjectError(ErrorCode::InvalidParameter,m);
}
SourcePosition scale(Frame frames, std::uint32_t numerator, std::uint32_t denominator) {
    require(frames >= 0,"Negative timing offset");
    // Quotient/remainder decomposition avoids the overflowing frames*n product
    // without non-portable __int128 or floating-point position accumulation.
    const auto whole = std::uint64_t(frames) / denominator;
    const auto partial = (std::uint64_t(frames) % denominator) * numerator;
    constexpr auto maximum = std::uint64_t(std::numeric_limits<Frame>::max());
    require(whole <= maximum / numerator,"Source/project timing overflow");
    const auto base = whole * numerator, add = partial / denominator;
    require(add <= maximum - base,"Source/project timing overflow");
    return {Frame(base + add),std::uint32_t(partial % denominator),denominator};
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
SourcePosition SourceFrameMap::at(Frame offset) const {
    auto p=scale(offset,numerator_,denominator_);
    const auto fraction=std::uint64_t(p.fraction)+origin_.fraction;
    const auto carry=fraction/denominator_;
    constexpr auto maximum=std::numeric_limits<Frame>::max();
    require(p.frame <= maximum-origin_.frame,"Source origin overflow");
    p.frame+=origin_.frame;
    require(carry <= std::uint64_t(maximum-p.frame),"Source fraction carry overflow");
    p.frame+=Frame(carry);p.fraction=std::uint32_t(fraction%denominator_);
    return p;
}
SourceFrameMap SourceFrameMap::advanced(Frame offset) const {
    auto next=*this;next.origin_=at(offset);return next;
}
Frame SourceFrameMap::projectFramesForSource(Frame frames) const {
    const auto p=scale(frames,denominator_,numerator_);
    require(!p.fraction || p.frame<std::numeric_limits<Frame>::max(),
            "Project duration rounding overflow");
    return p.frame + (p.fraction ? 1 : 0);
}
} // namespace soundcurrent::daw
