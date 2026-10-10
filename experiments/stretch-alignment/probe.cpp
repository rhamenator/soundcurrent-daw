// SPDX-License-Identifier: GPL-3.0-only
// Original bounded experiment. This target is outside the application build.
#include <soundcurrent/positioned_resampling.hpp>
#include <rubberband/RubberBandStretcher.h>
#include <nlohmann/json.hpp>
#include <algorithm>
#include <array>
#include <cmath>
#include <iomanip>
#include <iostream>
#include <map>
#include <stdexcept>
#include <vector>
using namespace soundcurrent::daw;
using RB = RubberBand::RubberBandStretcher;
using Audio = std::vector<float>; // interleaved stereo throughout this experiment
using Json = nlohmann::json;
namespace {
void require(bool value, const char *message) {
    if (!value) throw std::runtime_error(message);
}
Audio prepare(const Audio &raw, std::size_t first, std::size_t frames, bool half) {
    const SourceFrameMap map(48000,48000,SourcePosition{Frame(first),half?1u:0u,half?2u:1u});
    const PreparedPositionedResampling kernel(map,2);
    Audio result(frames*2);
    for (std::size_t at=0;at<frames;at+=512) {
        const auto n=std::uint32_t(std::min<std::size_t>(512,frames-at));
        const auto range=kernel.sourceRange(map,Frame(at),n,Frame(raw.size()/2));
        kernel.process(map,Frame(at),Frame(raw.size()/2),range.first,
            std::span<const float>(raw).subspan(std::size_t(range.first)*2,range.frames*2),
            std::span<float>(result).subspan(at*2,n*2));
    }
    return result;
}
Audio render(const Audio &input, unsigned numerator, unsigned denominator,
             int pitchMilliCents, bool finer, bool anchor, std::size_t event,
             std::size_t block) {
    const auto frames=input.size()/2;
    require(frames*numerator%denominator==0,"Probe requires exact integral output duration");
    const auto target=frames*numerator/denominator;
    const auto options=RB::OptionProcessOffline|RB::OptionThreadingNever|RB::OptionChannelsTogether|
        RB::OptionFormantPreserved|RB::OptionPitchHighQuality|
        (finer?RB::OptionEngineFiner:RB::OptionEngineFaster);
    RB rb(48000,2,options,double(target)/double(frames),std::exp2(double(pitchMilliCents)/1200000.));
    rb.setDebugLevel(0);rb.setMaxProcessSize(512);rb.setExpectedInputDuration(frames);
    if(anchor) {
        // The origin/end are implicit. R3 4.0.0 divides by the first map key
        // when initializing its ratio, so do not submit a redundant {0,0}.
        require(event>0 && event<frames && event*numerator%denominator==0,"Invalid probe event anchor");
        rb.setKeyFrameMap({{event,event*numerator/denominator}});
    }
    std::array<std::vector<float>,2> in,out;
    for(auto *buffers:{&in,&out})for(auto &channel:*buffers)channel.resize(512);
    std::array<const float *,2> ip{in[0].data(),in[1].data()};
    std::array<float *,2> op{out[0].data(),out[1].data()};
    auto read=[&](std::size_t at,std::size_t n) {
        for(std::size_t f=0;f<n;++f)for(std::size_t ch=0;ch<2;++ch)in[ch][f]=input[(at+f)*2+ch];
    };
    for(std::size_t at=0;at<frames;at+=block) {
        const auto n=std::min(block,frames-at);read(at,n);rb.study(ip.data(),n,at+n==frames);
    }
    Audio result;result.reserve(target*2);
    auto drain=[&] {
        while(rb.available()>0) {
            const auto n=std::size_t(std::min(512,rb.available()));
            const auto got=rb.retrieve(op.data(),n);
            require(got>0 && got<=n && got<=target-result.size()/2,"Probe drain exceeds exact duration");
            for(std::size_t f=0;f<got;++f)for(std::size_t ch=0;ch<2;++ch) {
                require(std::isfinite(out[ch][f]),"Non-finite probe output");result.push_back(out[ch][f]);
            }
        }
    };
    for(std::size_t at=0;at<frames;at+=block) {
        const auto n=std::min(block,frames-at);read(at,n);rb.process(ip.data(),n,at+n==frames);drain();
    }
    drain();require(rb.available()==-1 && result.size()==target*2,"Probe did not drain to exact duration");
    return result;
}
Audio crop(const Audio &audio,std::size_t first,std::size_t frames) {
    require(first<=audio.size()/2 && frames<=audio.size()/2-first,"Probe crop exceeds output");
    return {audio.begin()+std::ptrdiff_t(first*2),audio.begin()+std::ptrdiff_t((first+frames)*2)};
}
Json compare(const Audio &a,const Audio &b) {
    require(!a.empty() && a.size()==b.size(),"Probe comparison shape mismatch");
    double maximum=0,squared=0;
    for(std::size_t i=0;i<a.size();++i) {const auto d=double(a[i])-b[i];maximum=std::max(maximum,std::abs(d));squared+=d*d;}
    return { {"exact",a==b},{"maximumDifference",maximum},{"rmsDifference",std::sqrt(squared/double(a.size()))} };
}
Json peaks(const Audio &audio) {
    Json positions=Json::array(),amplitudes=Json::array();
    for(std::size_t ch=0;ch<2;++ch) {
        std::size_t at=0;double peak=0;
        for(std::size_t f=0;f<audio.size()/2;++f)if(std::abs(double(audio[f*2+ch]))>peak) {
            at=f;peak=std::abs(double(audio[f*2+ch]));
        }
        positions.push_back(at);amplitudes.push_back(peak);
    }
    return {{"frames",positions},{"amplitudes",amplitudes}};
}
}
int main() {
    try {
        Audio raw(32768*2);raw[16384*2]=1.5f;raw[16384*2+1]=-.75f;
        raw[17200*2]=.5f;raw[17200*2+1]=.25f;
        const auto unchanged=raw;
        const std::array<std::array<int,3>,5> settings{{{1,1,0},{3,2,0},{3,2,700007},{1,4,-2400000},{4,1,2400000}}};
        unsigned renders=0;
        for(bool half:{false,true})for(bool finer:{false,true})for(bool anchor:{false,true})for(auto value:settings) {
            const auto n=unsigned(value[0]),d=unsigned(value[1]);const auto pitch=value[2];
            auto contextInput=prepare(raw,12224,8320,half),wholeInput=prepare(raw,0,32764,half);
            auto context=render(contextInput,n,d,pitch,finer,anchor,4160,512);
            auto whole=render(wholeInput,n,d,pitch,finer,anchor,16384,512);
            auto partition=render(contextInput,n,d,pitch,finer,anchor,4160,97);renders+=3;
            auto a=crop(context,4096*n/d,128*n/d),b=crop(whole,16320*n/d,128*n/d);
            Json record={{"fraction",half?1:0},{"fractionDenominator",half?2:1},
                {"engine",finer?"R3":"R2"},{"explicitEventAnchor",anchor},
                {"timeNumerator",n},{"timeDenominator",d},{"pitchMilliCents",pitch},
                {"pitchHighQuality",true},{"formantPreserved",true},{"cropFrames",128*n/d},
                {"nominalEventFrame",64*n/d},{"contextPeaks",peaks(a)},{"wholePeaks",peaks(b)},
                {"contextVsWhole",compare(a,b)},{"partition97Vs512",compare(context,partition)}};
            if(n==d && pitch==0)record["unityVsPreparedInput"]=compare(context,contextInput);
            std::cout<<record.dump()<<'\n';
        }
        require(raw==unchanged,"Probe mutated raw source");
        std::cout<<Json({{"summary",true},{"actualRenders",renders},{"cases",40},
            {"rawUnchanged",true},{"productionChanged",false},{"nativeAudio",false},
            {"automaticContextQualified",false},{"fullProcessingQuality",false}}).dump()<<'\n';
        return std::cout?0:1;
    }catch(const std::exception &e) {std::cerr<<e.what()<<'\n';return 1;}
}
