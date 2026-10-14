// SPDX-License-Identifier: GPL-3.0-only
#include <soundcurrent/comp_handle_pcm.hpp>
#include <algorithm>
#include <cmath>
#include <limits>
#include <vector>
namespace soundcurrent::daw {
namespace {
void require(bool v,const char *why){if(!v)throw ProjectError(ErrorCode::InvalidParameter,why);}
}
CompHandlePcmReport assembleCompHandlePcm(const CompHandlePcmSpec &s,
    const CompHandlePcmRead &current,const CompHandlePcmRead &candidate,
    const CompHandlePcmWrite &write,ResourceLedger ledger,const std::function<void()> &poll){
    require(s.channels && s.channels<=256 && s.blockFrames && s.blockFrames<=65536,
        "Invalid comp assembly channel/block grant");
    require(s.currentFrames>0 && s.currentFrames<=1000000000 && s.prefixFrames>=0 &&
        s.prefixFrames<=1000000000-s.currentFrames && s.suffixFrames>=0 &&
        s.suffixFrames<=1000000000-s.currentFrames-s.prefixFrames,"Invalid comp assembly extent");
    require(bool(current) && bool(candidate) && bool(write) && bool(poll),"Missing comp assembly I/O or cancellation poll");
    poll();const auto slabFrames=std::max<std::uint32_t>(s.blockFrames,2);
    const auto samples=std::size_t(slabFrames)*s.channels;
    require(samples<=SIZE_MAX/(2*sizeof(float)),"Comp assembly scratch overflow");
    auto lease=ledger.reserve(samples*2*sizeof(float));
    std::vector<float> a(samples),b(samples);CompHandlePcmReport report;
    const auto read=[&](const CompHandlePcmRead &reader,Frame first,std::span<float> values){
        poll();std::fill(values.begin(),values.end(),std::numeric_limits<float>::quiet_NaN());reader(first,values);poll();
        for(const auto x:values)require(std::isfinite(x),"Incomplete or nonfinite comp assembly input");
    };
    const auto inspect=[&](bool prefix)->CompHandleJoinMetrics {
        const auto edge=prefix?s.prefixFrames:s.suffixFrames;
        const auto edgeCount=std::min<Frame>(2,edge),innerCount=std::min<Frame>(2,s.currentFrames);
        auto old=std::span(a).first(std::size_t(innerCount)*s.channels);
        auto generated=std::span(b).first(std::size_t(edgeCount)*s.channels);
        read(current,prefix?0:s.currentFrames-innerCount,old);
        read(candidate,prefix?s.prefixFrames-edgeCount:s.prefixFrames+s.currentFrames,generated);
        CompHandleJoinMetrics m;if(edgeCount==2 && innerCount==2)m.slopeDeviationPeak=0;
        for(std::uint32_t ch=0;ch<s.channels;++ch){
            const double left=prefix?generated[std::size_t(edgeCount-1)*s.channels+ch]:old[std::size_t(innerCount-1)*s.channels+ch];
            const double right=prefix?old[ch]:generated[ch];const auto step=right-left;
            m.stepPeak=std::max(m.stepPeak,std::abs(step));
            if(m.slopeDeviationPeak){
                const auto before=prefix?left-generated[ch]:left-old[ch];
                const auto after=prefix?old[s.channels+ch]-right:generated[s.channels+ch]-right;
                *m.slopeDeviationPeak=std::max({*m.slopeDeviationPeak,std::abs(step-before),std::abs(step-after)});
            }
        }return m;
    };
    // Inspect each declared join before emitting data. Metrics survive as a
    // report only after successful assembly; the caller keeps failure evidence.
    if(s.prefixFrames)report.prefixJoin=inspect(true);
    if(s.suffixFrames)report.suffixJoin=inspect(false);
    const auto emit=[&](const CompHandlePcmRead &reader,Frame first,Frame frames){
        for(Frame at=0;at<frames;){const auto count=std::size_t(std::min<Frame>(s.blockFrames,frames-at));
            auto values=std::span(a).first(count*s.channels);read(reader,first+at,values);
            for(const auto x:values)report.floatPeak=std::max(report.floatPeak,std::abs(double(x)));
            poll();write(values);report.writtenFrames+=Frame(count);poll();at+=Frame(count);
        }
    };
    emit(candidate,0,s.prefixFrames);emit(current,0,s.currentFrames);
    emit(candidate,s.prefixFrames+s.currentFrames,s.suffixFrames);
    require(report.writtenFrames==s.prefixFrames+s.currentFrames+s.suffixFrames,"Comp assembly incomplete");
    return report;
}
} // namespace soundcurrent::daw
