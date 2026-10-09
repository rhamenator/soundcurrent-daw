// SPDX-License-Identifier: BSD-2-Clause
// Copyright (c) 2002-2021, Erik de Castro Lopo <erikd@mega-nerd.com>
// All rights reserved. Original notice and license: third_party/libsamplerate/COPYING.
// Modified 2026-10-09: original SoundCurrent immutable absolute-position adapter;
// FIR halves/interpolation adapted from pinned 0.2.2 src_sinc.c. No ring/state ABI.
#include <soundcurrent/positioned_resampling.hpp>
#include <algorithm>
#include <array>
#include <cmath>
#include <functional>
#include <limits>
namespace {
using coeff_t=float;
#include <high_qual_coeffs.h>
constexpr std::int64_t fixedOne=4096;
constexpr std::int64_t maximumIndex=(std::size(slow_high_qual_coeffs.coeffs)-2)*fixedOne;
std::int64_t nearestEven(double x) {
    const auto base=std::int64_t(std::floor(x));const auto remainder=x-double(base);
    return base+((remainder>.5 || (remainder==.5 && base%2)) ? 1 : 0);
}
double coefficient(std::int64_t index) {
    const auto i=std::size_t(index/fixedOne);
    const double fraction=double(index%fixedOne)/fixedOne;
    return slow_high_qual_coeffs.coeffs[i]+fraction*
        (slow_high_qual_coeffs.coeffs[i+1]-slow_high_qual_coeffs.coeffs[i]);
}
void require(bool value,const char *message) {
    if(!value) throw soundcurrent::daw::ProjectError(soundcurrent::daw::ErrorCode::InvalidParameter,message);
}
}
namespace soundcurrent::daw {
PreparedPositionedResampling::PreparedPositionedResampling(std::uint32_t source,
        std::uint32_t project,std::uint32_t channels):sourceRate_(source),projectRate_(project),channels_(channels) {
    (void)SourceFrameMap(source,project);require(channels && channels<=256,"Invalid positioned channel layout");
    scale_=std::min(1.,double(project)/source);
    floatIncrement_=slow_high_qual_coeffs.increment*scale_;
    increment_=nearestEven(floatIncrement_*fixedOne);
}
std::uint32_t PreparedPositionedResampling::sourceContextFrames() const noexcept {
    return std::uint32_t(maximumIndex/increment_+3);
}
std::size_t PreparedPositionedResampling::maximumSourceWindowFrames(std::uint32_t frames) const {
    require(frames && frames<=65536,"Invalid positioned output block");
    const auto extent=(std::uint64_t(frames-1)*sourceRate_+projectRate_-1)/projectRate_;
    return std::size_t(extent+2*sourceContextFrames()+2);
}
bool PreparedPositionedResampling::exactCopy(const SourceFrameMap &map) const {
    require(map.numerator()*std::uint64_t(projectRate_)==map.denominator()*std::uint64_t(sourceRate_),
            "Positioned source map differs from prepared ratio");
    return sourceRate_==projectRate_ && map.at(0).fraction==0;
}
SourceReadRange PreparedPositionedResampling::sourceRange(const SourceFrameMap &map,
        Frame first,std::uint32_t frames,Frame total) const {
    require(first>=0 && frames && frames<=65536 && total>0 && first<=INT64_MAX-Frame(frames-1),
            "Invalid positioned source range");
    const bool copy=exactCopy(map);const auto begin=map.at(first),end=map.at(first+frames-1);
    require(begin.frame<total && end.frame<total,"Positioned range exceeds owned source extent");
    const Frame context=copy ? 0 : sourceContextFrames();
    const Frame low=begin.frame>=context ? begin.frame-context : 0;
    const Frame high=end.frame>=total-1-context ? total : end.frame+context+1;
    return {low,std::size_t(high-low)};
}
void PreparedPositionedResampling::process(const SourceFrameMap &map,Frame first,Frame total,
        Frame sourceFirst,std::span<const float> input,std::span<float> output) const {
    require(!output.empty() && output.size()%channels_==0 && output.size()/channels_<=65536 &&
            input.size()%channels_==0 && sourceFirst>=0 && sourceFirst<=total &&
            input.size()/channels_<=std::uint64_t(total-sourceFirst),"Invalid positioned spans");
    const auto count=std::uint32_t(output.size()/channels_);const auto range=sourceRange(map,first,count,total);
    require(sourceFirst<=range.first && input.size()/channels_>=range.frames+std::size_t(range.first-sourceFirst),
            "Positioned source window lacks filter context");
    // std::less supplies a total pointer order even for distinct allocations.
    // Reject aliasing before any output write, including the neutral copy path.
    const std::less<const float *> before;
    require(!before(input.data(),output.data()+output.size()) ||
            !before(output.data(),input.data()+input.size()),
            "Positioned source/output storage overlaps");
    for(const auto x:input) require(std::isfinite(x),"Non-finite positioned source sample");
    if(exactCopy(map)) {
        const auto at=std::size_t(map.at(first).frame-sourceFirst)*channels_;
        std::copy_n(input.data()+at,output.size(),output.data());return;
    }
    std::array<double,256> left{},right{};
    // Reuse validated raw views in the hot tap/channel loop. The complete window
    // and channel bound were checked above; each tap still checks its extent.
    const auto *inputData=input.data();const auto inputFrames=input.size()/channels_;
    auto *leftSum=left.data();auto *rightSum=right.data();
    for(std::uint32_t frame=0;frame<count;++frame) {
        auto position=map.at(first+frame);
        auto phase=nearestEven(double(position.fraction)/double(position.denominator)*floatIncrement_*fixedOne);
        // A fraction arbitrarily close to one rounds to the next fixed-point
        // phase. Carry it rather than dropping the right half's zero-index tap.
        if(phase>=increment_) {++position.frame;phase=0;}
        std::fill_n(left.data(),channels_,0.);std::fill_n(right.data(),channels_,0.);
        const auto accumulate=[&](std::int64_t index,Frame delta,double *sum) {
            // Delta is bounded by declared filter context; avoid Frame overflow
            // even for assets and positions at the signed frame boundary.
            if((delta<0 && position.frame<-delta) ||
               (delta>=0 && position.frame>INT64_MAX-delta)) return;
            const auto at=position.frame+delta;
            if(at<0 || at>=total) return; // Explicit asset boundary zero extension.
            require(at>=sourceFirst && std::uint64_t(at-sourceFirst)<inputFrames,
                    "Positioned tap outside admitted source window");
            const auto *p=inputData+std::size_t(at-sourceFirst)*channels_;const auto weight=coefficient(index);
            for(std::uint32_t c=0;c<channels_;++c) sum[c]+=weight*p[c];
        };
        auto taps=(maximumIndex-phase)/increment_;auto index=phase+taps*increment_;Frame delta=-taps;
        for(;index>=0;index-=increment_,++delta) accumulate(index,delta,leftSum);
        index=increment_-phase;taps=(maximumIndex-index)/increment_;index+=taps*increment_;delta=1+taps;
        for(;index>0;index-=increment_,--delta) accumulate(index,delta,rightSum);
        for(std::uint32_t c=0;c<channels_;++c) {
            const auto value=float(scale_*(leftSum[c]+rightSum[c]));
            require(std::isfinite(value),"Non-finite positioned output sample");
            output[std::size_t(frame)*channels_+c]=value;
        }
    }
}
} // namespace soundcurrent::daw
