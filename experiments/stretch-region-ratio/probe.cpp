// SPDX-License-Identifier: GPL-3.0-only
// Original bounded duration experiment, outside the application build.
#include <rubberband/RubberBandStretcher.h>
#include <nlohmann/json.hpp>
#include <array>
#include <vector>
#include <cmath>
#include <iostream>
#include <stdexcept>
#include <algorithm>
using RB=RubberBand::RubberBandStretcher;
int main(){try {
    unsigned renders=0;
    const std::array<std::array<int,3>,6> points{{{1,1,0},{3,2,0},{3,2,700007},{1,4,-2400000},{4,1,2400000},{13,6,0}}};
    for(std::size_t frames:{8193u,8319u,8320u})for(auto point:points)for(bool nominal:{false,true}) {
        const auto target=(frames*std::size_t(point[0])+std::size_t(point[1])-1)/std::size_t(point[1]);
        const auto ratio=nominal?double(point[0])/point[1]:double(target)/frames;
        RB rb(48000,2,RB::OptionProcessOffline|RB::OptionEngineFiner|RB::OptionThreadingNever|RB::OptionChannelsTogether|RB::OptionFormantPreserved,
              ratio,std::exp2(double(point[2])/1200000.));
        rb.setDebugLevel(0);rb.setMaxProcessSize(512);rb.setExpectedInputDuration(frames);
        std::array<std::array<float,512>,2> input{},output{};
        const float *in[]{input[0].data(),input[1].data()};float *out[]{output[0].data(),output[1].data()};
        auto read=[&](std::size_t first,std::size_t count){for(std::size_t f=0;f<count;++f)for(unsigned ch=0;ch<2;++ch)
            input[ch][f]=float(.3*std::sin(2*3.141592653589793*(ch?730:440)*(first+f)/48000.)+((first+f==4096)?(ch?.75:1.5):0));};
        for(std::size_t at=0;at<frames;at+=512){const auto count=std::min<std::size_t>(512,frames-at);read(at,count);rb.study(in,count,at+count==frames);}
        std::size_t written=0;double peak=0;
        auto drain=[&]{while(rb.available()>0){const auto count=std::size_t(std::min(512,rb.available()));const auto got=rb.retrieve(out,count);
            if(!got || got>count || written+got>target+32)throw std::runtime_error("Unbounded duration probe drain");
            for(std::size_t f=0;f<got;++f)for(unsigned ch=0;ch<2;++ch){if(!std::isfinite(output[ch][f]))throw std::runtime_error("Nonfinite probe output");peak=std::max(peak,std::abs(double(output[ch][f])));}
            written+=got;}};
        for(std::size_t at=0;at<frames;at+=512){const auto count=std::min<std::size_t>(512,frames-at);read(at,count);rb.process(in,count,at+count==frames);drain();}
        drain();if(rb.available()!=-1)throw std::runtime_error("Probe did not drain");++renders;
        std::cout<<nlohmann::json({{"frames",frames},{"numerator",point[0]},{"denominator",point[1]},{"pitchMilliCents",point[2]},
            {"nominalRatio",nominal},{"ceilTarget",target},{"writtenFrames",written},{"exactCeil",written==target},{"peak",peak}}).dump()<<'\n';
    }
    std::cout<<nlohmann::json({{"summary",true},{"renders",renders},{"nativeAudio",false},{"fullProcessingQuality",false}}).dump()<<'\n';return 0;
}catch(const std::exception &e){std::cerr<<e.what()<<'\n';return 1;}}
