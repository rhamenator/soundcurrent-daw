// SPDX-License-Identifier: GPL-3.0-only
// Original bounded streaming assembly promoted from PR89's retained prototype.
#include <soundcurrent/protected_warp_render.hpp>
#include <soundcurrent/stretch.hpp>
#include <rubberband/RubberBandStretcher.h>
#include <algorithm>
#include <cmath>
#include <memory>
namespace soundcurrent::daw {
namespace {
void require(bool v,const char *s){if(!v)throw ProjectError(ErrorCode::MediaMismatch,s);}
Frame integer(SourcePosition p){require(p.frame>=0 && !p.fraction,"Protected renderer refuses fractional acoustic positions");return p.frame;}
}
void validateProtectedWarpRender(const ProtectedWarpPlan &plan,std::uint32_t rate,std::uint32_t channels){
    require(channels>0 && channels<=256 && rate>=8000 && rate<=192000,"Unsupported protected render format");
    (void)plan.map().vendorInteriorFrames();require(!plan.map().region().rawOrigin.fraction,"Protected renderer refuses fractional raw origin");
    require(plan.policy().halo>0 && plan.policy().halo<=1024,"Protected render halo exceeds bounded workspace");
    for(const auto &gap:plan.gaps()){
        const auto source=integer(gap.sourceEnd)-integer(gap.sourceBegin),output=integer(gap.outputEnd)-integer(gap.outputBegin);
        if(source!=output && (source<2048 || output<2048))throw ProjectError(ErrorCode::UnsupportedSchema,"Nonunity protected gaps below 2048 frames have no acoustic admission");
    }
}
ProtectedWarpReport renderProtectedWarp(const ProtectedWarpPlan &plan,std::uint32_t rate,
    std::uint32_t channels,const WarpRead &read,const WarpWrite &write,ResourceLedger ledger,
    const std::function<void()> &poll) {
    validateProtectedWarpRender(plan,rate,channels);
    require(bool(read) && bool(write) && bool(poll),"Missing protected I/O/poll callback");
    const auto region=plan.map().region();require(!region.rawOrigin.fraction,"Protected renderer refuses fractional raw origin");
    const auto halo=plan.policy().halo;require(halo>0 && halo<=1024,"Protected render halo exceeds bounded workspace");
    auto credit=ledger.reserve((3*512+2*std::size_t(halo))*channels*sizeof(float)+65536);
    std::vector<float> input(512*channels),output(512*channels),interleaved(512*channels),rawHalo(2*std::size_t(halo)*channels);
    std::vector<const float *> in(channels);std::vector<float *> out(channels);
    for(std::size_t ch=0;ch<channels;++ch){in[ch]=input.data()+ch*512;out[ch]=output.data()+ch*512;}
    Frame cursor=0;ProtectedWarpReport report;
    auto emit=[&](std::size_t n){poll();require(n<=512 && Frame(n)<=region.outputFrames-cursor,"Protected emission exceeds exact duration");for(std::size_t i=0;i<n*channels;++i)require(std::isfinite(interleaved[i]),"Nonfinite protected output");write({interleaved.data(),n*channels});cursor+=Frame(n);};
    auto fill=[&](Frame first,std::span<float> values){require(values.size()%channels==0,"Protected read layout");for(std::size_t at=0;at<values.size()/channels;at+=512){poll();const auto n=std::min<std::size_t>(512,values.size()/channels-at);read(first+Frame(at),values.subspan(at*channels,n*channels));}};
    auto copyUntil=[&](Frame end){
        require(end>=cursor && end<=region.outputFrames,"Protected copy range reverses");if(end==cursor)return;
        const auto source=integer(plan.map().outputToSource({cursor,0,1})),last=integer(plan.map().outputToSource({end,0,1}));
        require(last-source==end-cursor,"Protected copy interval lacks unity slope");
        const auto begin=cursor;while(cursor<end){const auto n=std::size_t(std::min<Frame>(512,end-cursor));fill(source+cursor-begin,{interleaved.data(),n*channels});emit(n);}
    };
    using RB=RubberBand::RubberBandStretcher;
    for(const auto &gap:plan.gaps()) {
        const auto sb=integer(gap.sourceBegin),se=integer(gap.sourceEnd),ob=integer(gap.outputBegin),oe=integer(gap.outputEnd);
        const Frame before=sb?halo:0,after=se<region.inputFrames?halo:0;
        require(sb>=before && se<=region.inputFrames-after && ob>=before && oe<=region.outputFrames-after,"Protected actual-source halo exceeds extent");
        copyUntil(ob-before);const Frame count=se-sb+before+after,target=oe-ob+before+after;
        if(se-sb==oe-ob){for(Frame at=0;at<count;at+=512){const auto n=std::size_t(std::min<Frame>(512,count-at));fill(sb-before+at,{interleaved.data(),n*channels});emit(n);}continue;}
        fill(sb-before,{rawHalo.data(),std::size_t(before)*channels});
        fill(se,{rawHalo.data()+std::size_t(before)*channels,std::size_t(after)*channels});
        RB rb(rate,channels,RB::OptionProcessOffline|RB::OptionThreadingNever|RB::OptionEngineFiner|RB::OptionFormantPreserved|RB::OptionPitchHighQuality|
            (channels<=2?RB::OptionChannelsTogether:RB::OptionChannelsApart),double(target)/double(count),1.);
        rb.setDebugLevel(0);rb.setMaxProcessSize(512);rb.setExpectedInputDuration(std::size_t(count));
        std::map<std::size_t,std::size_t> keys;if(before)keys.emplace(std::size_t(before),std::size_t(before));if(after)keys.emplace(std::size_t(before+se-sb),std::size_t(before+oe-ob));if(!keys.empty())rb.setKeyFrameMap(keys);
        auto planarRead=[&](Frame at,std::size_t n){fill(sb-before+at,{interleaved.data(),n*channels});for(std::size_t ch=0;ch<channels;++ch)for(std::size_t f=0;f<n;++f)input[ch*512+f]=interleaved[f*channels+ch];};
        for(Frame at=0;at<count;at+=512){const auto n=std::size_t(std::min<Frame>(512,count-at));planarRead(at,n);rb.study(in.data(),n,at+Frame(n)==count);poll();}
        Frame generated=0;
        auto drain=[&]{while(rb.available()>0){poll();const auto n=std::size_t(std::min(512,rb.available()));const auto got=rb.retrieve(out.data(),n);require(got>0 && got<=n && Frame(got)<=target-generated,"Protected gap exceeds exact duration");
            for(std::size_t f=0;f<got;++f){const auto at=generated+Frame(f);double w=1;std::size_t rawAt=0;
                if(at<before){w=double(at+1)/double(before+1);rawAt=std::size_t(at);}
                else if(at>=target-after){w=double(target-at)/double(after+1);rawAt=std::size_t(before+at-(target-after));}
                for(std::size_t ch=0;ch<channels;++ch){const auto sample=out[ch][f];require(std::isfinite(sample),"Nonfinite protected gap output");
                    if(w==1)interleaved[f*channels+ch]=sample;
                    else {const auto original=rawHalo[rawAt*channels+ch];interleaved[f*channels+ch]=original==sample?sample:float((1-w)*double(original)+w*double(sample));}
                }
            }
            emit(got);generated+=Frame(got);
        }};
        for(Frame at=0;at<count;at+=512){const auto n=std::size_t(std::min<Frame>(512,count-at));planarRead(at,n);rb.process(in.data(),n,at+Frame(n)==count);drain();poll();}
        drain();require(rb.available()==-1 && generated==target,"Protected gap did not drain exactly; no repair");++report.processedGaps;
    }
    copyUntil(region.outputFrames);report.writtenFrames=cursor;return report;
}
} // namespace soundcurrent::daw
