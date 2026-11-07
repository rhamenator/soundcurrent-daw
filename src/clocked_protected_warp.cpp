// SPDX-License-Identifier: GPL-3.0-only
#include <soundcurrent/clocked_protected_warp.hpp>
#include <soundcurrent/stretch.hpp>
#include <rubberband/RubberBandStretcher.h>
#include <algorithm>
#include <cmath>
#include <limits>
namespace soundcurrent::daw {
namespace {
void require(bool v,const char *m){if(!v)throw ProjectError(ErrorCode::InvalidParameter,m);}
Frame ceiling(SourcePosition p){require(p.frame>=0 && p.frame<1000000000,"Clocked gap exceeds frame admission");return p.frame+Frame(p.fraction!=0);}
double scalar(SourcePosition p){return double(p.frame)+double(p.fraction)/double(p.denominator);}
Frame firstAt(const SourceFrameMap &m,Frame count,SourcePosition p,const std::function<void()> &poll){
    Frame low=0,high=count;while(low<high){poll();const auto mid=low+(high-low)/2;if(sourcePositionLess(m.at(mid),p))low=mid+1;else high=mid;}return low;
}
SourceFrameMap sampleMap(SourcePosition origin,const SourceFrameMap &clock,Frame count){return SourceFrameMap::processingGrid(origin,scaleSourcePosition({count,0,1},clock.numerator(),clock.denominator()),count);}
}
PreparedClockedProtectedWarp::PreparedClockedProtectedWarp(const ProtectedWarpPlan &plan,
    SourceFrameMap clock,Frame count,std::uint32_t channels,ResourceLedger ledger,
    const std::function<void()> &poll,ClipWarpLimits limits)
    :clock_(clock),outputFrames_(count),sourceFrames_(plan.map().exactRegion().availableSourceFrames),channels_(channels),rate_(plan.map().exactRegion().physicalRate){
    require(bool(poll),"Missing clocked preparation poll");poll();
    require(count>0 && count<=1000000000 && channels>0 && channels<=256 && rate_<=192000,"Invalid clocked protected format");
    const auto &region=plan.map().exactRegion();const SourcePosition end=region.outputExtent;
    require(sourcePositionLess(clock.at(count-1),end),"Clocked protected output exceeds original geometry");
    require(plan.policy().halo>0 && plan.policy().halo<=1024 && plan.gaps().size()<=limits.maximumMarkers && limits.maximumMarkers<=65536,"Invalid clocked protected bounds");
    PayloadCharge charge("Clocked protected partition",limits.maximumPayloadBytes);charge.add(sizeof(*this)+4096);charge.add(plan.gaps().size()*2+1,sizeof(ClockedProtectedPiece));
    lease_=ledger.reserve(charge.bytes());pieces_.reserve(plan.gaps().size()*2+1);
    const SourcePosition halo{plan.policy().halo,0,1};SourcePosition cursor{};
    auto direct=[&](SourcePosition endAt){
        require(!sourcePositionLess(endAt,cursor),"Clocked direct partition reverses");if(endAt==cursor)return;
        const auto source=plan.map().outputToSource(cursor),last=plan.map().outputToSource(endAt);
        require(subtractSourcePosition(last,source)==subtractSourcePosition(endAt,cursor),"Direct interval is not unity");
        // Endpoint equality alone is not enough: every original interior segment
        // must have the same unity slope, including touching protected spans.
        for(const auto &p:plan.map().points())if(sourcePositionLess(cursor,p.output) && sourcePositionLess(p.output,endAt))
            require(subtractSourcePosition(p.source,source)==subtractSourcePosition(p.output,cursor),"Direct interval crosses nonunity geometry");
        const auto first=firstAt(clock,count,cursor,poll),lastSample=firstAt(clock,count,endAt,poll);
        if(first<lastSample){ClockedProtectedPiece p;p.begin=cursor;p.end=endAt;p.first=first;p.last=lastSample;
            p.samples=sampleMap(plan.map().rawAt(plan.map().outputToSource(clock.at(first))),clock,lastSample-first);require(sourcePositionLess(p.samples.at(lastSample-first-1),{sourceFrames_,0,1}),"Direct clocked sample exceeds original asset");pieces_.push_back(std::move(p));}
        cursor=endAt;
    };
    for(const auto &gap:plan.gaps()){
        poll();const auto sourceSpan=subtractSourcePosition(gap.sourceEnd,gap.sourceBegin),outputSpan=subtractSourcePosition(gap.outputEnd,gap.outputBegin);
        if(sourceSpan==outputSpan)continue;
        require(!sourcePositionLess(sourceSpan,{2048,0,1}) && !sourcePositionLess(outputSpan,{2048,0,1}),"Nonunity clocked gap lacks minimum acoustic span");
        const bool before=gap.sourceBegin!=SourcePosition{},after=gap.sourceEnd!=region.inputExtent;
        const auto begin=before?subtractSourcePosition(gap.outputBegin,halo):gap.outputBegin;
        const auto finish=after?addSourcePosition(gap.outputEnd,halo):gap.outputEnd;
        direct(begin);const auto first=firstAt(clock,count,begin,poll),last=firstAt(clock,count,finish,poll);
        if(first<last){
            const auto slope=sourceIntervalRatio(sourceSpan,outputSpan);
            const auto rawBegin=plan.map().rawAt(gap.sourceBegin),rawEnd=plan.map().rawAt(gap.sourceEnd);
            // Linear gap context extends the gap's own slope. Its halo is mixed
            // with original unity audio, rather than rounding vendor keyframes.
            const auto inputBegin=before?subtractSourcePosition(rawBegin,scaleSourcePosition(halo,slope.numerator,slope.denominator)):rawBegin;
            const auto inputEnd=after?addSourcePosition(rawEnd,scaleSourcePosition(halo,slope.numerator,slope.denominator)):rawEnd;
            const auto inputSpan=subtractSourcePosition(inputEnd,inputBegin),renderSpan=subtractSourcePosition(finish,begin);
            ClockedProtectedPiece p;p.stretched=true;p.begin=begin;p.end=finish;p.gapBegin=gap.outputBegin;p.gapEnd=gap.outputEnd;p.rawGapBegin=rawBegin;p.rawGapEnd=rawEnd;p.first=first;p.last=last;
            p.inputFrames=ceiling(inputSpan);p.renderFrames=ceiling(renderSpan);
            p.input=ExactProcessingGrid::processingGrid(inputBegin,inputSpan,p.inputFrames);
            require(sourcePositionLess(p.input.at(p.inputFrames-1),{sourceFrames_,0,1}),"Linear gap context exceeds original asset");
            p.renderStep=scaleSourcePosition(renderSpan,1,std::uint64_t(p.renderFrames));
            const auto inv=sourceIntervalRatio({1,0,1},p.renderStep);
            const auto outputOrigin=scaleSourcePosition(subtractSourcePosition(clock.at(first),begin),inv.numerator,inv.denominator);
            const auto outputExtent=scaleSourcePosition(scaleSourcePosition({last-first,0,1},clock.numerator(),clock.denominator()),inv.numerator,inv.denominator);
            p.samples=ExactProcessingGrid::processingGrid(outputOrigin,outputExtent,last-first);
            require(sourcePositionLess(p.samples.at(last-first-1),{p.renderFrames,0,1}),"Gap sampling exceeds intrinsic output grid");
            pieces_.push_back(std::move(p));
        }
        cursor=finish;
    }
    direct(end);Frame covered=0;for(const auto &p:pieces_){require(p.first==covered && p.last>p.first,"Clocked output partition has a hole or overlap");covered=p.last;}require(covered==count,"Incomplete clocked output partition");poll();
}
ClockedProtectedReport renderClockedProtectedWarp(const PreparedClockedProtectedWarp &p,
    const WarpRead &read,const WarpWrite &write,ResourceLedger ledger,const std::function<void()> &poll){
    require(bool(read)&&bool(write)&&bool(poll),"Missing clocked render callback");poll();
    const auto channels=p.channels();require(channels>0 && !p.pieces().empty(),"Moved clocked plan");
    PreparedPositionedResampling rawKernel(p.clock(),channels);std::size_t rawFrames=rawKernel.maximumSourceWindowFrames(512),bufferFrames=0;
    for(const auto &piece:p.pieces()){
        poll();PreparedPositionedResampling k(piece.samples,channels);
        if(piece.stretched){PreparedPositionedResampling in(piece.input,channels);rawFrames=std::max(rawFrames,in.maximumSourceWindowFrames(512));bufferFrames=std::max(bufferFrames,k.maximumSourceWindowFrames(512)+512);}
        else rawFrames=std::max(rawFrames,k.maximumSourceWindowFrames(512));
    }
    PayloadCharge charge("Clocked protected explicit slabs",SIZE_MAX);charge.add(rawFrames+bufferFrames+5*512,std::size_t(channels)*sizeof(float));charge.add(65536);
    auto credit=ledger.reserve(charge.bytes());
    std::vector<float> raw(rawFrames*channels),buffer(bufferFrames*channels),interleaved(512*channels),original(512*channels),input(512*channels),output(512*channels),generated(512*channels);
    std::vector<const float *> in(channels);std::vector<float *> out(channels);for(unsigned ch=0;ch<channels;++ch){in[ch]=input.data()+ch*512;out[ch]=output.data()+ch*512;}
    ClockedProtectedReport report;
    auto fill=[&](const PreparedPositionedResampling &k,const auto &map,Frame at,std::uint32_t n,std::span<float> destination){
        poll();const auto r=k.sourceRange(map,at,n,p.sourceFrames());require(r.frames<=rawFrames,"Raw context exceeds admitted workspace");
        std::fill_n(raw.begin(),r.frames*channels,std::numeric_limits<float>::quiet_NaN());
        for(std::size_t f=0;f<r.frames;f+=512){poll();const auto count=std::min<std::size_t>(512,r.frames-f);read(r.first+Frame(f),{raw.data()+f*channels,count*channels});}
        for(std::size_t i=0;i<r.frames*channels;++i)require(std::isfinite(raw[i]),"Incomplete/nonfinite clocked raw source");
        k.process(map,at,p.sourceFrames(),r.first,{raw.data(),r.frames*channels},destination);poll();
    };
    auto emit=[&](std::span<const float> block){poll();require(block.size()%channels==0 && block.size()/channels<=512 && Frame(block.size()/channels)<=p.outputFrames()-report.writtenFrames,"Clocked emission exceeds output");
        for(float v:block){require(std::isfinite(v),"Nonfinite clocked output");report.floatPeak=std::max(report.floatPeak,std::abs(double(v)));}write(block);report.writtenFrames+=Frame(block.size()/channels);poll();};
    for(const auto &piece:p.pieces()){
        require(report.writtenFrames==piece.first,"Clocked piece emitted out of order");const auto samples=piece.last-piece.first;
        PreparedPositionedResampling kernel(piece.samples,channels);
        if(!piece.stretched){for(Frame at=0;at<samples;at+=512){const auto n=std::uint32_t(std::min<Frame>(512,samples-at));fill(kernel,piece.samples,at,n,{interleaved.data(),std::size_t(n)*channels});emit({interleaved.data(),std::size_t(n)*channels});}continue;}
        using RB=RubberBand::RubberBandStretcher;
        const double pitch=scalar(piece.renderStep)*double(piece.input.denominator())/double(piece.input.numerator());require(std::isfinite(pitch)&&pitch>0,"Invalid intrinsic gap pitch compensation");
        RB rb(p.rate(),channels,RB::OptionProcessOffline|RB::OptionThreadingNever|RB::OptionEngineFiner|RB::OptionFormantPreserved|RB::OptionPitchHighQuality|(channels<=2?RB::OptionChannelsTogether:RB::OptionChannelsApart),double(piece.renderFrames)/double(piece.inputFrames),pitch);
        rb.setDebugLevel(0);rb.setMaxProcessSize(512);rb.setExpectedInputDuration(std::size_t(piece.inputFrames));
        require(std::size_t(piece.inputFrames)>=rb.getSamplesRequired(),"Clocked gap shorter than backend analysis window");
        PreparedPositionedResampling inputKernel(piece.input,channels);
        auto planar=[&](Frame at,std::uint32_t n){fill(inputKernel,piece.input,at,n,{interleaved.data(),std::size_t(n)*channels});for(unsigned ch=0;ch<channels;++ch)for(unsigned f=0;f<n;++f)input[ch*512+f]=interleaved[f*channels+ch];};
        for(Frame at=0;at<piece.inputFrames;at+=512){const auto n=std::uint32_t(std::min<Frame>(512,piece.inputFrames-at));planar(at,n);rb.study(in.data(),n,at+n==piece.inputFrames);poll();}
        Frame delivered=0,received=0,base=0;std::size_t buffered=0;
        auto blend=[&](Frame at,std::uint32_t n){
            Frame offset=0;while(offset<n){const auto index=piece.first+at+offset;const auto x=p.clock().at(index);const bool left=sourcePositionLess(x,piece.gapBegin),right=!sourcePositionLess(x,piece.gapEnd);
                if(!left&&!right){++offset;continue;}
                std::uint32_t run=1;while(offset+run<n){const auto next=p.clock().at(index+run);if(left? !sourcePositionLess(next,piece.gapBegin):sourcePositionLess(next,piece.gapEnd))break;++run;}
                const auto rawAt=left?subtractSourcePosition(piece.rawGapBegin,subtractSourcePosition(piece.gapBegin,x)):addSourcePosition(piece.rawGapEnd,subtractSourcePosition(x,piece.gapEnd));
                const auto map=sampleMap(rawAt,p.clock(),run);fill(rawKernel,map,0,run,{original.data(),std::size_t(run)*channels});
                const auto length=left?subtractSourcePosition(piece.gapBegin,piece.begin):subtractSourcePosition(piece.end,piece.gapEnd);
                for(unsigned f=0;f<run;++f){const auto pos=p.clock().at(index+f);const auto distance=left?subtractSourcePosition(pos,piece.begin):subtractSourcePosition(piece.end,pos);const double weight=scalar(distance)/scalar(length);require(weight>=0 && weight<=1,"Halo weight outside semantic interval");
                    for(unsigned ch=0;ch<channels;++ch){auto &v=interleaved[std::size_t(offset+f)*channels+ch];const auto rawValue=original[std::size_t(f)*channels+ch];if(weight==0)v=rawValue;else if(v!=rawValue)v=float((1-weight)*double(rawValue)+weight*double(v));}}
                offset+=run;
            }
        };
        auto consume=[&](std::span<const float> block){
            require(block.size()%channels==0,"Invalid intrinsic gap layout");const auto n=block.size()/channels;for(float v:block)require(std::isfinite(v),"Nonfinite intrinsic gap sample");
            require(Frame(n)<=piece.renderFrames-received,"Intrinsic gap exceeded exact duration");
            if(delivered<samples){const auto count=std::uint32_t(std::min<Frame>(512,samples-delivered));const auto wanted=kernel.sourceRange(piece.samples,delivered,count,piece.renderFrames);
                const auto first=std::max(received,wanted.first),end=received+Frame(n);
                if(first<end){if(!buffered)base=first;require(base+Frame(buffered)==first,"Intrinsic gap buffer is discontinuous");const auto keep=std::size_t(end-first);require(buffered+keep<=bufferFrames,"Intrinsic gap exceeded bounded buffer");
                    std::copy_n(block.begin()+std::size_t(first-received)*channels,keep*channels,buffer.begin()+buffered*channels);buffered+=keep;report.maximumBufferedFrames=std::max(report.maximumBufferedFrames,buffered);}
                while(delivered<samples){const auto frames=std::uint32_t(std::min<Frame>(512,samples-delivered));const auto r=kernel.sourceRange(piece.samples,delivered,frames,piece.renderFrames);
                    if(!buffered || base>r.first || base+Frame(buffered)<r.first+Frame(r.frames))break;
                    kernel.process(piece.samples,delivered,piece.renderFrames,base,{buffer.data(),buffered*channels},{interleaved.data(),std::size_t(frames)*channels});blend(delivered,frames);emit({interleaved.data(),std::size_t(frames)*channels});delivered+=frames;
                    if(delivered<samples){const auto next=kernel.sourceRange(piece.samples,delivered,std::uint32_t(std::min<Frame>(512,samples-delivered)),piece.renderFrames);const auto drop=std::min<std::size_t>(buffered,std::size_t(next.first-base));
                        std::move(buffer.begin()+drop*channels,buffer.begin()+buffered*channels,buffer.begin());base+=Frame(drop);buffered-=drop;}
                }
            }
            received+=Frame(n);
        };
        auto drain=[&]{while(rb.available()>0){poll();const auto n=std::size_t(std::min(512,rb.available()));const auto got=rb.retrieve(out.data(),n);require(got>0 && got<=n,"Intrinsic gap backend stalled");
            for(std::size_t f=0;f<got;++f)for(unsigned ch=0;ch<channels;++ch)generated[f*channels+ch]=out[ch][f];
            consume({generated.data(),got*channels});}};
        for(Frame at=0;at<piece.inputFrames;at+=512){const auto n=std::uint32_t(std::min<Frame>(512,piece.inputFrames-at));planar(at,n);rb.process(in.data(),n,at+n==piece.inputFrames);drain();poll();}
        drain();require(rb.available()==-1 && received==piece.renderFrames && delivered==samples,"Clocked gap did not drain exactly; no repair");
        report.generatedGapFrames+=received;++report.processedGaps;
    }
    require(report.writtenFrames==p.outputFrames(),"Clocked render duration differs");return report;
}
} // namespace soundcurrent::daw
