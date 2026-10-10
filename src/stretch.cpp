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
std::string_view stretchProcessorFor(const StretchSettings &p,std::optional<StretchContext> context,const std::optional<WarpSettings> &warp) {
    if(warp){validateStretchSettings(p);validateWarpSettings(*warp);require(!context && p.pitchMilliCents==0 && p.formantPreserved,"Protected warp currently requires zero pitch, preserved formants and no extra context",ErrorCode::UnsupportedSchema);return protectedWarpProcessorId;}
    validateStretchSettings(p);
    if(context)require(context->before>=0 && context->after>=0 && context->before<=1000000000 && context->after<=1000000000,
                       "Invalid explicit stretch context");
    const bool unity=p.timeNumerator==p.timeDenominator && p.pitchMilliCents==0;
    return context ? (unity?regionCopyProcessorId:regionStretchProcessorId) : (unity?unityStretchProcessorId:stretchProcessorId);
}
void validateStretchProcessor(std::string_view id,const StretchSettings &p,std::optional<StretchContext> context,const std::optional<WarpSettings> &warp) {
    if(warp){require(id==stretchProcessorFor(p,context,warp),"Unsupported protected warp processor",ErrorCode::UnsupportedSchema);return;}
    validateStretchSettings(p);
    require(context ? id==stretchProcessorFor(p,context) : (id==stretchProcessorId || (id==unityStretchProcessorId && stretchProcessorFor(p)==unityStretchProcessorId)),
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
bool sourcePositionLess(SourcePosition a,SourcePosition b) {
    a=canonical(a);b=canonical(b);
    if(a.frame!=b.frame)return a.frame<b.frame;
    const auto left=multiply(a.fraction,b.denominator),right=multiply(b.fraction,a.denominator);
    return left.high!=right.high?left.high<right.high:left.low<right.low;
}
SourcePosition subtractSourcePosition(SourcePosition a,SourcePosition b) {
    a=canonical(a);b=canonical(b);require(!sourcePositionLess(a,b),"Negative source-position interval");
    const auto reduced=a.denominator/std::gcd(a.denominator,b.denominator);
    require(reduced<=UINT64_MAX/b.denominator,"Source-position difference denominator overflow");
    const auto denominator=reduced*b.denominator,left=a.fraction*(denominator/a.denominator),right=b.fraction*(denominator/b.denominator);
    const bool borrow=left<right;
    return canonical({a.frame-b.frame-Frame(borrow),borrow?denominator-(right-left):left-right,denominator});
}
namespace {
Frame nominalProcessingFrames(const ClipStretchAnchor &p) {
    validateStretchProcessor(p.processor,p.settings,p.context,p.warp);
    require(p.sourceFrames>0 && p.sourceFrames<=1000000000,"Invalid retained raw span");
    const Frame before=p.context?p.context->before:0,after=p.context?p.context->after:0;
    require(before>=0 && after>=0 && before<=1000000000-p.sourceFrames &&
            after<=1000000000-p.sourceFrames-before,"Context exceeds position-map admission");
    return before+p.sourceFrames+after;
}
ProtectedWarpPlan positionWarp(const ClipStretchAnchor &p,ResourceLedger ledger) {
    require(bool(p.warp),"Missing protected warp state");
    validateStretchProcessor(p.processor,p.settings,p.context,p.warp);
    return {{{0,0,1},48000,p.sourceFrames,p.sourceFrames,stretchOutputFrames(p.sourceFrames,p.settings),{0,0,1},{p.sourceFrames,0,1}},
            p.warp->markers,p.warp->protection,ledger};
}
}
SourcePosition stretchSourceToOutput(const ClipStretchAnchor &p,SourcePosition source) {
    const auto input=nominalProcessingFrames(p);
    source=canonical(source);require(!sourcePositionLess({p.sourceFrames,0,1},source),"Raw position exceeds retained stretch span");
    if(p.warp){ResourceLedger ledger(8*1024*1024,"Warp position map");return positionWarp(p,ledger).map().sourceToOutput(source);}
    const auto before=p.context?p.context->before:0;
    require(before>=0 && source.frame<=INT64_MAX-before,"Context position exceeds representation");source.frame+=before;
    return scaleSourcePosition(source,p.context?p.settings.timeNumerator:std::uint64_t(stretchOutputFrames(input,p.settings)),
                               p.context?p.settings.timeDenominator:std::uint64_t(input));
}
SourcePosition stretchOutputToSource(const ClipStretchAnchor &p,SourcePosition output) {
    const auto input=nominalProcessingFrames(p);
    if(p.warp){ResourceLedger ledger(8*1024*1024,"Warp position map");return positionWarp(p,ledger).map().outputToSource(output);}
    const auto before=p.context?p.context->before:0;
    auto source=scaleSourcePosition(output,p.context?p.settings.timeDenominator:std::uint64_t(input),
                                   p.context?p.settings.timeNumerator:std::uint64_t(stretchOutputFrames(input,p.settings)));
    require(before>=0 && source.frame>=before,"Position precedes retained nominal raw span");source.frame-=before;
    require(!sourcePositionLess({p.sourceFrames,0,1},source),"Position exceeds retained nominal raw span");return source;
}
StretchGeometry stretchGeometry(const ClipStretchAnchor &p,Frame available) {
    validateStretchProcessor(p.processor,p.settings,p.context,p.warp);
    require(canonicalStretchSettings(p.settings)==p.settings,"Stretch settings must be canonical");
    require(canonical(p.sourceOrigin)==p.sourceOrigin && available>0 && p.sourceOrigin.frame<available &&
            p.sourceFrames>0 && p.sourceFrames<=1000000000,"Invalid stretch source region");
    const Frame before=p.context?p.context->before:0,after=p.context?p.context->after:0;
    require(before<=p.sourceOrigin.frame && before<=1000000000-p.sourceFrames &&
            after<=1000000000-p.sourceFrames-before,"Stretch context exceeds source/frame admission");
    StretchGeometry g;g.inputOrigin=p.sourceOrigin;g.inputOrigin.frame-=before;
    g.inputFrames=before+p.sourceFrames+after;
    require(g.inputFrames<=available-g.inputOrigin.frame,"Stretch context exceeds real source extent");
    g.outputFrames=stretchOutputFrames(g.inputFrames,p.settings);
    g.mapNumerator=p.context?p.settings.timeNumerator:std::uint64_t(g.outputFrames);
    g.mapDenominator=p.context?p.settings.timeDenominator:std::uint64_t(g.inputFrames);
    g.visibleBegin=scaleSourcePosition({before,0,1},g.mapNumerator,g.mapDenominator);
    g.visibleEnd=scaleSourcePosition({before+p.sourceFrames,0,1},g.mapNumerator,g.mapDenominator);
    require(!sourcePositionLess({g.outputFrames,0,1},g.visibleEnd),"Stretch visible map exceeds output");return g;
}
void validateClipStretch(const ClipStretchAnchor &p,const Asset &source,const Asset &rendered) {
    const auto geometry=stretchGeometry(p,source.frames);
    require(p.sourceAssetId==source.id && source.id!=rendered.id && p.sourceSha256==source.sha256 &&
            digest(p.sourceSha256) && digest(p.renderKey),"Stretch source/artifact identity mismatch");
    require(source.sampleRate==rendered.sampleRate && source.layout==rendered.layout,
            "Stretch changed physical source rate or channel layout");
    require(rendered.frames==geometry.outputFrames,
            "Stretch artifact duration differs from processor state");
    if(p.warp){ResourceLedger ledger(8*1024*1024,"Persisted warp geometry");ProtectedWarpPlan plan({{0,0,1},source.sampleRate,p.sourceFrames,p.sourceFrames,geometry.outputFrames,{0,0,1},{p.sourceFrames,0,1}},p.warp->markers,p.warp->protection,ledger);}
    const auto origin=canonical(p.sourceOrigin);
    require(origin==p.sourceOrigin,"Stretch source origin must be canonical");
    const SourceFrameMap map(source.sampleRate,source.sampleRate,origin);
    require(map.at(p.sourceFrames-1).frame<source.frames,"Stretch raw span exceeds source");
    require(canonicalStretchSettings(p.settings)==p.settings,"Stretch settings must be canonical");
}
void validateStretchClipWindow(const Clip &c,const Asset &source,const Asset &rendered,std::uint32_t projectRate) {
    require(c.stretch.has_value(),"Missing stretch window anchor");
    if(!c.stretch->context && !c.stretch->warp)return;
    const auto g=stretchGeometry(*c.stretch,source.frames);
    const auto map=clipSourceMap(c,rendered.sampleRate,projectRate);
    require(!sourcePositionLess(map.at(0),g.visibleBegin) && sourcePositionLess(map.at(c.lengthFrames-1),g.visibleEnd),
            "Clip exceeds its explicit stretch visible interval");
}
ClipStretchPlan prepareClipStretch(const Session &s,const Id &track,const Id &clip,StretchSettings settings,std::optional<StretchContext> context,const std::optional<WarpSettings> &warp) {
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
    result.anchor.context=context;result.anchor.warp=warp;result.anchor.processor=stretchProcessorFor(result.anchor.settings,context,warp);
    if(warp){ResourceLedger ledger(8*1024*1024,"Prepared warp geometry");ProtectedWarpPlan plan({{0,0,1},result.source.sampleRate,result.anchor.sourceFrames,result.anchor.sourceFrames,stretchOutputFrames(result.anchor.sourceFrames,result.anchor.settings),{0,0,1},{result.anchor.sourceFrames,0,1}},warp->markers,warp->protection,ledger);}
    (void)stretchGeometry(result.anchor,result.source.frames);
    return result;
}
void adoptClipStretch(Session &s,const ApplyClipStretch &e) {
    auto &clips=find(s.tracks,e.track).clips;
    auto &destination=find(clips,e.clip);
    auto c=destination;
    require(c==e.expected,"Clip changed before stretch adoption",ErrorCode::InvalidState);
    const auto plan=prepareClipStretch(s,e.track,e.clip,e.value.settings,e.value.context,e.value.warp);
    auto intended=plan.anchor;intended.renderKey=e.value.renderKey;
    require(plan.source==e.source && intended==e.value,"Stretch raw anchor changed before adoption",ErrorCode::InvalidState);
    validateClipStretch(e.value,e.source,e.rendered);
    const auto geometry=stretchGeometry(e.value,e.source.frames);const auto newFrames=e.rendered.frames;
    const auto previous=c.stretch?stretchGeometry(*c.stretch,e.source.frames):StretchGeometry{};
    const auto numerator=geometry.mapNumerator*previous.mapDenominator;
    const auto denominator=geometry.mapDenominator*previous.mapNumerator;
    auto origin=geometry.visibleBegin;
    const bool nonlinear=bool(e.value.warp)||(c.stretch && c.stretch->warp);
    Frame mappedLength=0;
    if(nonlinear){
        SourcePosition rawBegin{0,0,1},rawEnd{e.value.sourceFrames,0,1};
        if(c.stretch){const auto oldMap=clipSourceMap(c,find(s.assets,c.assetId).sampleRate,s.sampleRate);auto end=oldMap.at(c.lengthFrames);if(sourcePositionLess(previous.visibleEnd,end))end=previous.visibleEnd;rawBegin=stretchOutputToSource(*c.stretch,oldMap.at(0));rawEnd=stretchOutputToSource(*c.stretch,end);}
        origin=stretchSourceToOutput(e.value,rawBegin);const auto end=stretchSourceToOutput(e.value,rawEnd);
        auto duration=subtractSourcePosition(end,origin);duration=scaleSourcePosition(duration,std::uint64_t(s.sampleRate)*c.playbackRate.denominator,std::uint64_t(e.rendered.sampleRate)*c.playbackRate.numerator);
        require(duration.frame<INT64_MAX || !duration.fraction,"Warped clip duration overflow");mappedLength=duration.frame+Frame(duration.fraction!=0);
    } else if(c.stretch) {
        if(!c.stretch->context && !e.value.context) {
            origin=scaleSourcePosition({c.sourceFrame,c.sourceTiming.fraction,c.sourceTiming.denominator},
                                      std::uint64_t(newFrames),std::uint64_t(previous.outputFrames));
        } else {
        auto raw=scaleSourcePosition({c.sourceFrame,c.sourceTiming.fraction,c.sourceTiming.denominator},previous.mapDenominator,previous.mapNumerator);
        const Frame oldBefore=c.stretch->context?c.stretch->context->before:0,newBefore=e.value.context?e.value.context->before:0;
        require(raw.frame>=oldBefore,"Clip starts before retained visible raw span");raw.frame-=oldBefore;
        require(raw.frame<=INT64_MAX-newBefore,"Stretch crop shift overflow");raw.frame+=newBefore;
        origin=scaleSourcePosition(raw,geometry.mapNumerator,geometry.mapDenominator);
        }
    }
    auto length=nonlinear?mappedLength:scaleStretchFrame(c.lengthFrames,numerator,denominator,true);
    const SourceFrameMap map(e.rendered.sampleRate,s.sampleRate,origin,c.playbackRate);
    // A rounded interval can reach the physical end; retain only real samples.
    Frame low=0,high=length;
    while(low<high) {const auto mid=low+(high-low)/2+1;bool fits=false;
        try {fits=map.at(mid-1).frame<newFrames && sourcePositionLess(map.at(mid-1),geometry.visibleEnd);} catch(const ProjectError &error) {
            if(error.code()!=ErrorCode::InvalidParameter) throw;
        }
        if(fits) low=mid;else high=mid-1;
    }
    require(low>0,"Stretch leaves no source audio");length=low;
    for(auto *fade:{&c.processing.fadeIn,&c.processing.fadeOut}) if(fade->startFrame!=fade->endFrame) {
        fade->startFrame=scaleStretchFrame(fade->startFrame,nonlinear?std::uint64_t(length):numerator,nonlinear?std::uint64_t(c.lengthFrames):denominator,false);
        fade->endFrame=scaleStretchFrame(fade->endFrame,nonlinear?std::uint64_t(length):numerator,nonlinear?std::uint64_t(c.lengthFrames):denominator,true);
    }
    auto found=std::find_if(s.assets.begin(),s.assets.end(),[&](const auto &a){return a.id==e.rendered.id;});
    if(found==s.assets.end()) s.assets.push_back(e.rendered);
    else require(*found==e.rendered,"Derived asset identity already names different media",ErrorCode::InvalidState);
    c.assetId=e.rendered.id;c.sourceFrame=origin.frame;c.sourceTiming={origin.fraction,origin.denominator};
    c.lengthFrames=length;c.stretch=e.value;
    destination=std::move(c);
}
} // namespace soundcurrent::daw
