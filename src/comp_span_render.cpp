// SPDX-License-Identifier: GPL-3.0-only
#include <soundcurrent/comp_span_render.hpp>
#include <soundcurrent/positioned_resampling.hpp>
#include <rubberband/RubberBandStretcher.h>
#include <algorithm>
#include <cmath>
#include <limits>
#include <memory>
namespace soundcurrent::daw {
namespace {
void check(bool v,const char *m,ErrorCode code=ErrorCode::InvalidParameter){if(!v)throw ProjectError(code,m);}
}
CompSpanPlan prepareCompSpan(const CompHandleEnvelope &envelope,ResourceLedger ledger){
    const auto &e=envelope.data();
    check(!e.regeneration,"Repeated comp spans require clock-aware production and historical file verification",ErrorCode::UnsupportedSchema);
    check(e.expected.stretch.has_value(),"Comp span requires a processed source");
    check(e.map.size()>=2 && e.map.front().output==SourcePosition{0,0,1} && e.map.back().output==SourcePosition{e.outputFrames,0,1},"Comp span map endpoints differ");
    const auto span=subtractSourcePosition(e.map.back().raw,e.map.front().raw);
    const auto ratio=sourceIntervalRatio(span,{e.outputFrames,0,1});
    for(std::size_t i=1;i<e.map.size();++i){const auto r=sourceIntervalRatio(subtractSourcePosition(e.map[i].raw,e.map[i-1].raw),subtractSourcePosition(e.map[i].output,e.map[i-1].output));
        check(r.numerator==ratio.numerator && r.denominator==ratio.denominator,"Nonuniform comp span requires a mapped producer",ErrorCode::UnsupportedSchema);}
    check(span.frame>=0 && span.frame<=1000000000 && (!span.fraction || span.frame<1000000000) && e.outputFrames>0 && e.outputFrames<=1000000000,"Comp span exceeds frame bounds");
    const auto count=span.frame+Frame(span.fraction!=0);check(count>0,"Empty comp processing grid");
    auto grid=SourceFrameMap::processingGrid(e.map.front().raw,span,count);
    check(scaleSourcePosition(grid.at(count),1,1)==scaleSourcePosition(e.map.back().raw,1,1) && grid.at(count-1).frame<e.raw.frames,"Comp processing grid exceeds actual source");
    check(e.raw.sampleRate>=8000 && e.raw.sampleRate<=192000 && e.raw.layout.channels>0 && e.raw.layout.channels<=256,"Unsupported comp span format",ErrorCode::UnsupportedSchema);
    auto settings=e.expected.stretch->settings;validateStretchSettings(settings);
    PayloadCharge payload("Retained comp span",SIZE_MAX);payload.add(sizeof(CompSpanPlan)+4096);payload.add(e.raw.relativePath.size()+e.raw.sha256.size());
    for(const auto &f:e.raw.nativeEvidence){payload.add(sizeof(ProjectEvidenceFile)+256);payload.add(f.relativePath.size()+f.sha256.size());}
    auto lease=ledger.reserve(payload.bytes());return {std::move(lease),e.raw,grid,span,count,e.outputFrames,settings};
}
CompSpanReport renderCompSpan(const CompSpanPlan &p,const CompSpanRead &read,const CompSpanWrite &write,ResourceLedger ledger,const std::function<void()> &poll){
    check(bool(read) && bool(write) && bool(poll),"Missing comp span callbacks");poll();const auto &a=p.source();
    check(p.processingFrames()>0 && p.outputFrames()>0 && a.layout.channels>0 && !a.relativePath.empty() && !a.sha256.empty(),"Invalid moved comp span");
    const auto channels=a.layout.channels;PreparedPositionedResampling kernel(p.grid(),channels);
    const auto sourceFrames=kernel.maximumSourceWindowFrames(512);
    PayloadCharge scratch("Comp span explicit slabs",SIZE_MAX);scratch.add(sourceFrames,channels*sizeof(float));scratch.add(3*512,channels*sizeof(float));scratch.add(65536);
    auto credit=ledger.reserve(scratch.bytes());std::vector<float> raw(sourceFrames*channels),interleaved(512*channels),input(512*channels),output(512*channels);
    std::vector<const float *> in(channels);std::vector<float *> out(channels);
    for(unsigned ch=0;ch<channels;++ch){in[ch]=input.data()+ch*512;out[ch]=output.data()+ch*512;}
    CompSpanReport report;report.backendPitchFactor=std::exp2(double(p.settings().pitchMilliCents)/1200000.)*double(p.grid().denominator())/p.grid().numerator();
    report.copied=p.grid().numerator()==p.grid().denominator() && p.processingFrames()==p.outputFrames() && p.settings().pitchMilliCents==0;
    using RB=RubberBand::RubberBandStretcher;std::unique_ptr<RB> rb;
    if(!report.copied){
        const auto options=RB::OptionProcessOffline|RB::OptionEngineFiner|RB::OptionThreadingNever|
            (channels<=2?RB::OptionChannelsTogether:RB::OptionChannelsApart)|
            (p.settings().formantPreserved?RB::OptionFormantPreserved:RB::OptionFormantShifted);
        rb=std::make_unique<RB>(a.sampleRate,channels,options,double(p.outputFrames())/double(p.processingFrames()),report.backendPitchFactor);
        rb->setDebugLevel(0);rb->setMaxProcessSize(512);rb->setExpectedInputDuration(std::size_t(p.processingFrames()));
        check(std::size_t(p.processingFrames())>=rb->getSamplesRequired(),"Comp span shorter than prepared R3 window",ErrorCode::UnsupportedSchema);
    }
    const auto fill=[&](Frame at,std::uint32_t n){poll();const auto r=kernel.sourceRange(p.grid(),at,n,a.frames);
        std::fill_n(raw.begin(),r.frames*channels,std::numeric_limits<float>::quiet_NaN());read(r.first,{raw.data(),r.frames*channels});poll();
        for(std::size_t i=0;i<r.frames*channels;++i)check(std::isfinite(raw[i]),"Incomplete/nonfinite comp span source",ErrorCode::MediaMismatch);
        kernel.process(p.grid(),at,a.frames,r.first,{raw.data(),r.frames*channels},{interleaved.data(),std::size_t(n)*channels});
        for(unsigned ch=0;ch<channels;++ch)for(unsigned f=0;f<n;++f)input[std::size_t(ch)*512+f]=interleaved[std::size_t(f)*channels+ch];
    };
    const auto emit=[&](std::size_t n){poll();check(n<=512 && Frame(n)<=p.outputFrames()-report.writtenFrames,"Comp span output exceeded exact duration",ErrorCode::MediaMismatch);
        for(std::size_t i=0;i<n*channels;++i){check(std::isfinite(interleaved[i]),"Nonfinite comp span output",ErrorCode::MediaMismatch);report.floatPeak=std::max(report.floatPeak,std::abs(double(interleaved[i])));}
        write({interleaved.data(),n*channels});report.writtenFrames+=Frame(n);
    };
    if(rb)for(Frame at=0;at<p.processingFrames();at+=512){const auto n=std::uint32_t(std::min<Frame>(512,p.processingFrames()-at));fill(at,n);rb->study(in.data(),n,at+n==p.processingFrames());poll();}
    const auto drain=[&]{while(rb->available()>0){poll();const auto n=std::size_t(std::min(512,rb->available()));const auto got=rb->retrieve(out.data(),n);
        check(got>0 && got<=n,"Comp span output stalled",ErrorCode::MediaMismatch);
        for(std::size_t f=0;f<got;++f)for(unsigned ch=0;ch<channels;++ch)interleaved[f*channels+ch]=out[ch][f];
        emit(got);
    }};
    for(Frame at=0;at<p.processingFrames();at+=512){const auto n=std::uint32_t(std::min<Frame>(512,p.processingFrames()-at));fill(at,n);
        if(rb){rb->process(in.data(),n,at+n==p.processingFrames());drain();}else emit(n);poll();}
    if(rb){drain();check(rb->available()==-1,"Comp span backend did not retire",ErrorCode::MediaMismatch);}
    check(report.writtenFrames==p.outputFrames(),"Comp span did not drain exact duration; no repair",ErrorCode::MediaMismatch);return report;
}
} // namespace soundcurrent::daw
