// SPDX-License-Identifier: GPL-3.0-only
#include <soundcurrent/stretch.hpp>
#include <soundcurrent/clip_timing.hpp>
#include <algorithm>
#include <numeric>
#include <limits>
namespace soundcurrent::daw {
namespace {
void require(bool v, const char *message, ErrorCode code = ErrorCode::InvalidParameter) {
    if (!v) throw ProjectError(code, message);
}
struct Wide { std::uint64_t high, low; };
Wide multiply(std::uint64_t a, std::uint64_t b) {
    // Portable exact 64x64 product; MSVC does not provide unsigned __int128.
    constexpr std::uint64_t mask = 0xffffffff;
    const auto low = (a & mask) * (b & mask);
    const auto left = (a >> 32) * (b & mask), right = (b >> 32) * (a & mask);
    const auto middle = (low >> 32) + (left & mask) + (right & mask);
    return {(a >> 32) * (b >> 32) + (left >> 32) + (right >> 32) + (middle >> 32),
            (middle << 32) | (low & mask)};
}
struct Division { std::uint64_t quotient, remainder; };
Division divide(Wide p, std::uint64_t d) {
    require(d && p.high < d, "Stretch coordinate quotient overflow");
    std::uint64_t q = 0, r = p.high;
    for (int bit = 63; bit >= 0; --bit) {
        const bool carry = (r >> 63) != 0;
        r = (r << 1) | ((p.low >> bit) & 1);
        if (carry || r >= d) { r -= d; q |= std::uint64_t(1) << bit; }
    }
    return {q,r};
}
SourcePosition canonical(SourcePosition p) {
    require(p.frame >= 0 && p.denominator && p.fraction < p.denominator,
            "Invalid exact stretch coordinate");
    const auto g = std::gcd(p.fraction,p.denominator);
    p.fraction /= g; p.denominator /= g; return p;
}
bool digest(const std::string &s) {
    return s.size()==64 && std::all_of(s.begin(),s.end(),[](char c) {
        return (c>='0' && c<='9') || (c>='a' && c<='f');
    });
}
template<class T> const T &find(const std::vector<T> &v, const Id &id) {
    const auto p=std::find_if(v.begin(),v.end(),[&](const auto &x){return x.id==id;});
    require(p!=v.end(),"Stretch source/clip no longer exists",ErrorCode::InvalidId);return *p;
}
template<class T> T &find(std::vector<T> &v, const Id &id) {
    const auto p=std::find_if(v.begin(),v.end(),[&](const auto &x){return x.id==id;});
    require(p!=v.end(),"Stretch source/clip no longer exists",ErrorCode::InvalidId);return *p;
}
}
void validateStretchSettings(const StretchSettings &p) {
    require(p.timeNumerator && p.timeDenominator && p.timeNumerator<=1000000 &&
            p.timeDenominator<=1000000 && std::uint64_t(p.timeNumerator)*4>=p.timeDenominator &&
            std::uint64_t(p.timeDenominator)*4>=p.timeNumerator,
            "Stretch duration must be between 0.25 and 4 with bounded rational components");
    require(p.pitchMilliCents>=-2400000 && p.pitchMilliCents<=2400000,
            "Stretch pitch must be between -24 and 24 semitones");
}
StretchSettings canonicalStretchSettings(StretchSettings p) {
    validateStretchSettings(p);const auto g=std::gcd(p.timeNumerator,p.timeDenominator);
    p.timeNumerator/=g;p.timeDenominator/=g;return p;
}
std::string_view stretchProcessorFor(const StretchSettings &p) {
    validateStretchSettings(p);
    return p.timeNumerator==p.timeDenominator && p.pitchMilliCents==0 ? unityStretchProcessorId : stretchProcessorId;
}
void validateStretchProcessor(std::string_view id,const StretchSettings &p) {
    validateStretchSettings(p);
    require(id==stretchProcessorId || (id==unityStretchProcessorId && stretchProcessorFor(p)==unityStretchProcessorId),
            "Unsupported stretch processor/settings",ErrorCode::UnsupportedSchema);
}
Frame stretchOutputFrames(Frame frames,const StretchSettings &p) {
    validateStretchSettings(p);require(frames>0 && frames<=1000000000,"Stretch input span exceeds admission");
    const auto product=std::uint64_t(frames)*p.timeNumerator;
    const auto target=product/p.timeDenominator+std::uint64_t(product%p.timeDenominator!=0);
    require(target && target<=1000000000,"Stretch output span exceeds admission");return Frame(target);
}
Frame scaleStretchFrame(Frame frame,std::uint64_t n,std::uint64_t d,bool ceiling) {
    require(n && d,"Invalid stretch scale");
    const bool negative=frame<0;
    const auto magnitude=negative ? std::uint64_t(-(frame+1))+1 : std::uint64_t(frame);
    const auto p=divide(multiply(magnitude,n),d);
    const auto extra=std::uint64_t(p.remainder && (negative ? !ceiling : ceiling));
    const auto limit=std::uint64_t(INT64_MAX)+std::uint64_t(negative);
    require(p.quotient<=limit && extra<=limit-p.quotient,"Stretch frame scale overflow");
    const auto value=p.quotient+extra;
    if (negative && value==std::uint64_t(INT64_MAX)+1) return INT64_MIN;
    return negative ? -Frame(value) : Frame(value);
}
SourcePosition scaleSourcePosition(SourcePosition p,std::uint64_t n,std::uint64_t d) {
    p=canonical(p);require(n && d,"Invalid stretch coordinate scale");
    const auto ratioGcd=std::gcd(n,d);n/=ratioGcd;d/=ratioGcd;
    const auto whole=divide(multiply(std::uint64_t(p.frame),n),d);
    if(!p.fraction) {
        require(whole.quotient<=std::uint64_t(INT64_MAX),"Stretch source frame overflow");
        return canonical({Frame(whole.quotient),whole.remainder,d});
    }
    auto f=p.fraction,den=p.denominator,fn=n,fd=d;
    const auto a=std::gcd(f,fd);f/=a;fd/=a;
    const auto b=std::gcd(fn,den);fn/=b;den/=b;
    require(den<=UINT64_MAX/fd,"Stretch source denominator exceeds representation");
    const auto fractionDen=den*fd;
    const auto fraction=divide(multiply(f,fn),fractionDen);
    const auto wholeGcd=std::gcd(whole.remainder,d);
    const auto wholeDen=d/wholeGcd,wholeRemainder=whole.remainder/wholeGcd;
    const auto common=std::gcd(wholeDen,fractionDen);
    require(wholeDen/common<=UINT64_MAX/fractionDen,"Stretch source common denominator overflow");
    const auto resultDen=wholeDen/common*fractionDen;
    const auto left=wholeRemainder*(resultDen/wholeDen),right=fraction.remainder*(resultDen/fractionDen);
    const bool carry=left>=resultDen-right;
    const auto remainder=carry ? left-(resultDen-right) : left+right;
    require(whole.quotient<=std::uint64_t(INT64_MAX) &&
            fraction.quotient<=std::uint64_t(INT64_MAX)-whole.quotient &&
            std::uint64_t(carry)<=std::uint64_t(INT64_MAX)-whole.quotient-fraction.quotient,
            "Stretch source coordinate overflow");
    return canonical({Frame(whole.quotient+fraction.quotient+std::uint64_t(carry)),remainder,resultDen});
}
void validateClipStretch(const ClipStretchAnchor &p,const Asset &source,const Asset &rendered) {
    validateStretchProcessor(p.processor,p.settings);
    require(p.sourceAssetId==source.id && source.id!=rendered.id && p.sourceSha256==source.sha256 &&
            digest(p.sourceSha256) && digest(p.renderKey),"Stretch source/artifact identity mismatch");
    require(source.sampleRate==rendered.sampleRate && source.layout==rendered.layout,
            "Stretch changed physical source rate or channel layout");
    require(rendered.frames==stretchOutputFrames(p.sourceFrames,p.settings),
            "Stretch artifact duration differs from processor state");
    const auto origin=canonical(p.sourceOrigin);
    require(origin==p.sourceOrigin,"Stretch source origin must be canonical");
    const SourceFrameMap map(source.sampleRate,source.sampleRate,origin);
    require(map.at(p.sourceFrames-1).frame<source.frames,"Stretch raw span exceeds source");
    require(canonicalStretchSettings(p.settings)==p.settings,"Stretch settings must be canonical");
}
ClipStretchPlan prepareClipStretch(const Session &s,const Id &track,const Id &clip,StretchSettings settings) {
    validate(s);const auto &c=find(find(s.tracks,track).clips,clip);
    const auto &current=find(s.assets,c.assetId);
    ClipStretchPlan result{track,c,{}, {}};
    if(c.stretch) {
        result.anchor=*c.stretch;result.source=find(s.assets,c.stretch->sourceAssetId);
    } else {
        result.source=current;result.anchor.sourceAssetId=current.id;
        result.anchor.sourceSha256=current.sha256;
        result.anchor.sourceOrigin=canonical(clipSourceMap(c,current.sampleRate,s.sampleRate).at(0));
        const auto duration=SourceFrameMap(current.sampleRate,s.sampleRate,Frame(0),c.playbackRate).at(c.lengthFrames);
        require(duration.frame<INT64_MAX || !duration.fraction,"Stretch duration overflow");
        result.anchor.sourceFrames=std::min(duration.frame+Frame(duration.fraction!=0),
                                             current.frames-result.anchor.sourceOrigin.frame);
    }
    result.anchor.settings=canonicalStretchSettings(settings);result.anchor.renderKey.clear();
    result.anchor.processor=stretchProcessorFor(result.anchor.settings);
    (void)stretchOutputFrames(result.anchor.sourceFrames,result.anchor.settings);
    return result;
}
void adoptClipStretch(Session &s,const ApplyClipStretch &e) {
    auto &clips=find(s.tracks,e.track).clips;
    auto &destination=find(clips,e.clip);
    auto c=destination;
    require(c==e.expected,"Clip changed before stretch adoption",ErrorCode::InvalidState);
    const auto plan=prepareClipStretch(s,e.track,e.clip,e.value.settings);
    auto intended=plan.anchor;intended.renderKey=e.value.renderKey;
    require(plan.source==e.source && intended==e.value,"Stretch raw anchor changed before adoption",ErrorCode::InvalidState);
    validateClipStretch(e.value,e.source,e.rendered);
    const auto oldFrames=c.stretch ? find(s.assets,c.assetId).frames : e.value.sourceFrames;
    const auto newFrames=e.rendered.frames;
    const auto origin=c.stretch ? scaleSourcePosition({c.sourceFrame,c.sourceTiming.fraction,c.sourceTiming.denominator},
                                                       std::uint64_t(newFrames),std::uint64_t(oldFrames)) : SourcePosition{};
    auto length=scaleStretchFrame(c.lengthFrames,std::uint64_t(newFrames),std::uint64_t(oldFrames),true);
    const SourceFrameMap map(e.rendered.sampleRate,s.sampleRate,origin,c.playbackRate);
    // A rounded interval can reach the physical end; retain only real samples.
    Frame low=0,high=length;
    while(low<high) {const auto mid=low+(high-low)/2+1;bool fits=false;
        try {fits=map.at(mid-1).frame<newFrames;} catch(const ProjectError &error) {
            if(error.code()!=ErrorCode::InvalidParameter) throw;
        }
        if(fits) low=mid;else high=mid-1;
    }
    require(low>0,"Stretch leaves no source audio");length=low;
    for(auto *fade:{&c.processing.fadeIn,&c.processing.fadeOut}) if(fade->startFrame!=fade->endFrame) {
        fade->startFrame=scaleStretchFrame(fade->startFrame,std::uint64_t(newFrames),std::uint64_t(oldFrames),false);
        fade->endFrame=scaleStretchFrame(fade->endFrame,std::uint64_t(newFrames),std::uint64_t(oldFrames),true);
    }
    auto found=std::find_if(s.assets.begin(),s.assets.end(),[&](const auto &a){return a.id==e.rendered.id;});
    if(found==s.assets.end()) s.assets.push_back(e.rendered);
    else require(*found==e.rendered,"Derived asset identity already names different media",ErrorCode::InvalidState);
    c.assetId=e.rendered.id;c.sourceFrame=origin.frame;c.sourceTiming={origin.fraction,origin.denominator};
    c.lengthFrames=length;c.stretch=e.value;
    destination=std::move(c);
}
} // namespace soundcurrent::daw
