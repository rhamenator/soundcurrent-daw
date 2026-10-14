// SPDX-License-Identifier: GPL-3.0-only
#include <soundcurrent/comp_handle_pcm.hpp>
#include <algorithm>
#include <cmath>
#include <limits>
#include <vector>
namespace soundcurrent::daw {
namespace {
void require(bool v,const char *why){if(!v)throw ProjectError(ErrorCode::InvalidParameter,why);}
void validateSpec(const CompHandlePcmSpec &s){
    require(s.channels && s.channels<=256 && s.blockFrames && s.blockFrames<=65536,
        "Invalid comp assembly channel/block grant");
    require(s.currentFrames>0 && s.currentFrames<=1000000000 && s.prefixFrames>=0 &&
        s.prefixFrames<=1000000000-s.currentFrames && s.suffixFrames>=0 &&
        s.suffixFrames<=1000000000-s.currentFrames-s.prefixFrames,"Invalid comp assembly extent");
}
}
CompHandlePcmReport assembleCompHandlePcm(const CompHandlePcmSpec &s,
    const CompHandlePcmRead &current,const CompHandlePcmRead &candidate,
    const CompHandlePcmWrite &write,ResourceLedger ledger,const std::function<void()> &poll){
    validateSpec(s);
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
CompHandlePcmReport blendCompHandlePcm(const CompHandlePcmSpec &s,
    const CompHandlePcmBlends &blends,const CompHandlePcmRead &current,
    const CompHandlePcmRead &candidate,const CompHandlePcmWrite &write,
    ResourceLedger ledger,const std::function<void()> &poll){
    validateSpec(s);
    require(bool(current) && bool(candidate) && bool(write) && bool(poll),"Missing comp blend I/O or cancellation poll");
    const auto validateBlend=[&](const std::optional<CompHandleInnerBlend> &b,bool prefix){
        if(!b)return;
        require((prefix?s.prefixFrames:s.suffixFrames)>0 && b->frames>=2 && b->frames<=s.currentFrames,
            "Comp blend needs a generated edge and valid inner width");
        require(b->permittedFirst>=0 && b->permittedEnd>b->permittedFirst && b->permittedEnd<=s.currentFrames,
            "Invalid permitted comp processing region");
        const auto first=prefix?0:s.currentFrames-b->frames,end=prefix?b->frames:s.currentFrames;
        require(first>=b->permittedFirst && end<=b->permittedEnd,"Comp blend exceeds permitted processing region");
    };
    validateBlend(blends.prefix,true);validateBlend(blends.suffix,false);
    const auto prefix=blends.prefix?blends.prefix->frames:0,suffix=blends.suffix?blends.suffix->frames:0;
    require(prefix<=s.currentFrames-suffix,"Comp blend inner regions overlap");
    if(!prefix && !suffix)return assembleCompHandlePcm(s,current,candidate,write,ledger,poll);
    poll();const auto scratchFrames=std::max<std::uint32_t>(2,s.blockFrames);
    const auto samples=std::size_t(scratchFrames)*s.channels;
    require(samples<=SIZE_MAX/sizeof(float),"Comp blend scratch overflow");
    auto lease=ledger.reserve(samples*sizeof(float));std::vector<float> scratch(samples);
    const CompHandlePcmRead blended=[&](Frame first,std::span<float> values){
        poll();current(first,values);poll();
        for(const auto x:values)require(std::isfinite(x),"Incomplete or nonfinite current blend input");
        const auto last=first+Frame(values.size()/s.channels);
        const auto apply=[&](Frame begin,Frame end,bool head){
            const auto low=std::max(first,begin),high=std::min(last,end);if(low>=high)return;
            const auto count=std::size_t(high-low)*s.channels;auto generated=std::span(scratch).first(count);
            std::fill(generated.begin(),generated.end(),std::numeric_limits<float>::quiet_NaN());
            poll();candidate(s.prefixFrames+low,generated);poll();
            for(const auto x:generated)require(std::isfinite(x),"Incomplete or nonfinite candidate blend input");
            for(Frame f=low;f<high;++f){
                const auto t=double(f-begin+1)/double(end-begin+1);
                const auto curve=std::clamp(t*t*t*(10+t*(-15+6*t)),0.,1.);
                const auto oldWeight=head?curve:1-curve;
                for(std::uint32_t ch=0;ch<s.channels;++ch){
                    auto &old=values[std::size_t(f-first)*s.channels+ch];const auto next=generated[std::size_t(f-low)*s.channels+ch];
                    if(old!=next)old=float(oldWeight*double(old)+(1-oldWeight)*double(next));
                    require(std::isfinite(old),"Nonfinite comp blend output");
                }
            }
        };
        if(prefix)apply(0,prefix,true);
        if(suffix)apply(s.currentFrames-suffix,s.currentFrames,false);
    };
    return assembleCompHandlePcm(s,blended,candidate,write,ledger,poll);
}
} // namespace soundcurrent::daw
