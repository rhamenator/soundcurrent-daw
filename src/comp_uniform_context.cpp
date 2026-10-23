// SPDX-License-Identifier: GPL-3.0-only
#include <soundcurrent/comp_uniform_context.hpp>
#include <soundcurrent/comp_regeneration_source.hpp>
#include <soundcurrent/positioned_resampling.hpp>
#include <rubberband/RubberBandStretcher.h>
#include <algorithm>
#include <cmath>
#include <limits>
namespace soundcurrent::daw {
namespace {
void require(bool v,const char *m,ErrorCode code=ErrorCode::InvalidParameter){if(!v)throw ProjectError(code,m);}
Frame ceiling(SourcePosition p){require(p.frame>=0 && p.frame<=1000000000 && (!p.fraction || p.frame<1000000000),"Uniform context exceeds frame bounds");return p.frame+Frame(p.fraction!=0);}
double scalar(SourcePosition p){return double(p.frame)+double(p.fraction)/double(p.denominator);}
}
CompUniformContextPlan prepareUniformContextCompSpan(const CompHandleEnvelope &envelope,ResourceLedger ledger,const std::function<void()> &poll){
 require(bool(poll),"Missing uniform context poll");poll();const auto &e=envelope.data();
 const auto &original=e.regeneration?e.regeneration->data().originalNominalClip:e.expected;
 require(original.stretch && !original.stretch->warp,"Uniform context requires original uniform geometry",ErrorCode::UnsupportedSchema);
 const auto &anchor=*original.stretch;const auto nominal=stretchGeometry(anchor,e.raw.frames);
 require(anchor.sourceAssetId==e.raw.id && anchor.sourceSha256==e.raw.sha256 && e.raw.layout==e.base.layout,"Uniform context raw binding differs",ErrorCode::MediaMismatch);
 require(e.raw.sampleRate>=8000 && e.raw.sampleRate<=192000 && e.base.sampleRate>=8000 && e.base.sampleRate<=192000 && e.raw.layout.channels>0 && e.raw.layout.channels<=256,"Unsupported uniform context format",ErrorCode::UnsupportedSchema);
 auto first=nominal.inputOrigin,last=addSourcePosition(first,{nominal.inputFrames,0,1});
 const auto verifyMap=[&](std::span<const CompHandlePoint> map){
  require(map.size()>=2 && map.size()<=16388 && map.front().output==SourcePosition{},"Invalid uniform context map");
  const auto span=subtractSourcePosition(map.back().raw,map.front().raw);const auto ratio=sourceIntervalRatio(span,map.back().output);
  for(std::size_t i=0;i<map.size();++i){poll();const auto &p=map[i];require(scaleSourcePosition(p.raw,1,1)==p.raw && scaleSourcePosition(p.output,1,1)==p.output,"Noncanonical uniform context knot");
   if(i)require(sourcePositionLess(map[i-1].raw,p.raw) && sourcePositionLess(map[i-1].output,p.output),"Uniform context map reverses");
   require(addSourcePosition(map.front().raw,scaleSourcePosition(p.output,ratio.numerator,ratio.denominator))==p.raw,"Uniform context map is nonaffine",ErrorCode::MediaMismatch);
  }
  require(!sourcePositionLess({e.raw.frames,0,1},map.back().raw),"Uniform context map exceeds original source",ErrorCode::MediaMismatch);
  if(sourcePositionLess(map.front().raw,first))first=map.front().raw;
  if(sourcePositionLess(last,map.back().raw))last=map.back().raw;
  return ratio;
 };
 const auto slope=verifyMap(e.map);require(e.outputFrames>0 && e.map.back().output==SourcePosition{e.outputFrames,0,1},"Uniform context candidate extent differs");
 SourcePosition baked{1,0,1};
 if(e.regeneration){const auto &history=e.regeneration->data();require(history.raw==e.raw && history.current==e.base && history.originalNominal.has_value(),"Uniform context lineage binding differs",ErrorCode::MediaMismatch);baked=history.bakedPitchScale;
  // Only this clip's ancestry extends its domain; unrelated tracks cannot alter PCM.
  auto id=e.base.id;std::size_t depth=0;
  for(;;){poll();const auto found=std::find_if(history.history.begin(),history.history.end(),[&](const auto &x){return x.source.rendered.id==id;});
   if(found==history.history.end()){require(id==original.assetId,"Uniform lineage ends at another nominal source",ErrorCode::MediaMismatch);break;}
   require(++depth<=history.history.size(),"Uniform lineage cycle",ErrorCode::MediaMismatch);require(found->source.raw==e.raw,"Uniform ancestor raw asset differs",ErrorCode::MediaMismatch);(void)verifyMap(found->source.rawMap);id=found->source.base.id;
  }
 }
 const auto clock=scaleSourcePosition(baked,e.raw.sampleRate,e.base.sampleRate);
 const auto expected=scaleSourcePosition(clock,nominal.mapDenominator,nominal.mapNumerator);
 require(scaleSourcePosition({1,0,1},slope.numerator,slope.denominator)==expected,"Uniform candidate clock differs from nominal/baked history",ErrorCode::MediaMismatch);
 require(!sourcePositionLess({e.raw.frames,0,1},last),"Uniform original context exceeds raw source",ErrorCode::MediaMismatch);
 const auto span=subtractSourcePosition(last,first),extent=scaleSourcePosition(span,nominal.mapNumerator,nominal.mapDenominator);
 const auto inputs=ceiling(span),renders=ceiling(extent);require(inputs>0 && renders>0,"Empty uniform original context");
 const auto input=ExactProcessingGrid::processingGrid(first,span,inputs);
 // The full render has its own integer storage grid. Compose raw-to-intrinsic
 // timing with the current candidate clock without rounding either origin.
 const auto samplingRatio=sourceIntervalRatio({renders,0,1},span);
 const auto origin=scaleSourcePosition(subtractSourcePosition(e.map.front().raw,first),samplingRatio.numerator,samplingRatio.denominator);
 const auto sampleExtent=scaleSourcePosition(subtractSourcePosition(e.map.back().raw,e.map.front().raw),samplingRatio.numerator,samplingRatio.denominator);
 const auto samples=ExactProcessingGrid::processingGrid(origin,sampleExtent,e.outputFrames);
 require(sourcePositionLess(input.at(inputs-1),{e.raw.frames,0,1}) && sourcePositionLess(samples.at(e.outputFrames-1),{renders,0,1}),"Uniform sampling exceeds context");
 PayloadCharge charge("Uniform original context owner",SIZE_MAX);charge.add(sizeof(CompUniformContextPlan)+4096);charge.add(e.raw.relativePath.size()+e.raw.sha256.size());
 for(const auto &f:e.raw.nativeEvidence){poll();charge.add(sizeof(ProjectEvidenceFile)+256);charge.add(f.relativePath.size()+f.sha256.size());}
 auto credit=ledger.reserve(charge.bytes());poll();return {std::move(credit),e.raw,input,samples,span,extent,baked,inputs,renders,e.outputFrames,e.base.sampleRate,anchor.settings};
}
CompUniformContextReport renderUniformContextCompSpan(const CompUniformContextPlan &p,const CompSpanRead &read,const CompSpanWrite &write,ResourceLedger ledger,const std::function<void()> &poll){
 require(bool(read)&&bool(write)&&bool(poll),"Missing uniform context callbacks");poll();const auto &a=p.source();const auto channels=a.layout.channels;
 require(channels>0 && channels<=256 && !a.relativePath.empty() && !a.sha256.empty(),"Moved uniform context plan");
 PreparedPositionedResampling inputKernel(p.input(),channels),kernel(p.samples(),channels);
 const auto rawFrames=inputKernel.maximumSourceWindowFrames(512),bufferFrames=kernel.maximumSourceWindowFrames(512)+512;
 PayloadCharge charge("Uniform context bounded slabs",SIZE_MAX);charge.add(rawFrames+bufferFrames+4*512,std::size_t(channels)*sizeof(float));charge.add(65536);auto credit=ledger.reserve(charge.bytes());
 std::vector<float> raw(rawFrames*channels),buffer(bufferFrames*channels),interleaved(512*channels),input(512*channels),output(512*channels),generated(512*channels);
 std::vector<const float *> in(channels);std::vector<float *> out(channels);for(unsigned ch=0;ch<channels;++ch){in[ch]=input.data()+ch*512;out[ch]=output.data()+ch*512;}
 CompUniformContextReport report;const auto outputStep=scaleSourcePosition(p.intrinsicExtent(),1,std::uint64_t(p.renderFrames()));
 // Compensate both input/output grid rounding before sampling the baked clock.
 // Baked varispeed is applied by samples(), not by retuning nominal formants.
 report.backendPitchFactor=std::exp2(double(p.settings().pitchMilliCents)/1200000.)*scalar(outputStep)*double(p.input().denominator())/double(p.input().numerator());
 require(std::isfinite(report.backendPitchFactor)&&report.backendPitchFactor>0,"Invalid intrinsic uniform pitch");
 using RB=RubberBand::RubberBandStretcher;
 RB rb(a.sampleRate,channels,RB::OptionProcessOffline|RB::OptionEngineFiner|RB::OptionThreadingNever|(channels<=2?RB::OptionChannelsTogether:RB::OptionChannelsApart)|(p.settings().formantPreserved?RB::OptionFormantPreserved:RB::OptionFormantShifted),double(p.renderFrames())/double(p.inputFrames()),report.backendPitchFactor);
 rb.setDebugLevel(0);rb.setMaxProcessSize(512);rb.setExpectedInputDuration(std::size_t(p.inputFrames()));
 require(std::size_t(p.inputFrames())>=rb.getSamplesRequired(),"Full original uniform context shorter than backend window",ErrorCode::UnsupportedSchema);
 const auto fill=[&](Frame at,std::uint32_t n){poll();const auto r=inputKernel.sourceRange(p.input(),at,n,a.frames);require(r.frames<=rawFrames,"Uniform source range exceeds slab");
  std::fill_n(raw.begin(),r.frames*channels,std::numeric_limits<float>::quiet_NaN());
  for(std::size_t f=0;f<r.frames;f+=512){poll();const auto count=std::min<std::size_t>(512,r.frames-f);read(r.first+Frame(f),{raw.data()+f*channels,count*channels});}
  for(std::size_t i=0;i<r.frames*channels;++i)require(std::isfinite(raw[i]),"Incomplete/nonfinite uniform source",ErrorCode::MediaMismatch);
  inputKernel.process(p.input(),at,a.frames,r.first,{raw.data(),r.frames*channels},{interleaved.data(),std::size_t(n)*channels});
  for(unsigned ch=0;ch<channels;++ch)for(unsigned f=0;f<n;++f)input[ch*512+f]=interleaved[f*channels+ch];
  poll();
 };
 for(Frame at=0;at<p.inputFrames();at+=512){const auto n=std::uint32_t(std::min<Frame>(512,p.inputFrames()-at));fill(at,n);rb.study(in.data(),n,at+n==p.inputFrames());poll();}
 Frame received=0,delivered=0,base=0;std::size_t buffered=0;
 const auto consume=[&](std::span<const float> block){poll();const auto n=block.size()/channels;require(block.size()%channels==0 && Frame(n)<=p.renderFrames()-received,"Intrinsic uniform output extent differs");for(float v:block)require(std::isfinite(v),"Nonfinite uniform backend output");
  if(delivered<p.outputFrames()){
   const auto wanted=kernel.sourceRange(p.samples(),delivered,std::uint32_t(std::min<Frame>(512,p.outputFrames()-delivered)),p.renderFrames());const auto first=std::max(received,wanted.first),end=received+Frame(n);
   if(first<end){if(!buffered)base=first;require(base+Frame(buffered)==first,"Uniform context buffer discontinuity");const auto keep=std::size_t(end-first);require(buffered+keep<=bufferFrames,"Uniform context exceeded bounded buffer");std::copy_n(block.begin()+std::size_t(first-received)*channels,keep*channels,buffer.begin()+buffered*channels);buffered+=keep;report.maximumBufferedFrames=std::max(report.maximumBufferedFrames,buffered);}
   while(delivered<p.outputFrames()){
    poll();const auto frames=std::uint32_t(std::min<Frame>(512,p.outputFrames()-delivered));const auto r=kernel.sourceRange(p.samples(),delivered,frames,p.renderFrames());
    if(!buffered || base>r.first || base+Frame(buffered)<r.first+Frame(r.frames))break;
    kernel.process(p.samples(),delivered,p.renderFrames(),base,{buffer.data(),buffered*channels},{interleaved.data(),std::size_t(frames)*channels});
    for(std::size_t i=0;i<std::size_t(frames)*channels;++i){require(std::isfinite(interleaved[i]),"Nonfinite uniform candidate output");report.floatPeak=std::max(report.floatPeak,std::abs(double(interleaved[i])));}
    write({interleaved.data(),std::size_t(frames)*channels});delivered+=frames;
    if(delivered<p.outputFrames()){const auto next=kernel.sourceRange(p.samples(),delivered,std::uint32_t(std::min<Frame>(512,p.outputFrames()-delivered)),p.renderFrames());require(next.first>=base,"Uniform crop reversed");const auto drop=std::min<std::size_t>(buffered,std::size_t(next.first-base));std::move(buffer.begin()+drop*channels,buffer.begin()+buffered*channels,buffer.begin());base+=Frame(drop);buffered-=drop;}
   }
  }
  received+=Frame(n);
 };
 const auto drain=[&]{while(rb.available()>0){poll();const auto n=std::size_t(std::min(512,rb.available()));const auto got=rb.retrieve(out.data(),n);require(got>0 && got<=n,"Uniform backend stalled");for(std::size_t f=0;f<got;++f)for(unsigned ch=0;ch<channels;++ch)generated[f*channels+ch]=out[ch][f];consume({generated.data(),got*channels});}};
 for(Frame at=0;at<p.inputFrames();at+=512){const auto n=std::uint32_t(std::min<Frame>(512,p.inputFrames()-at));fill(at,n);rb.process(in.data(),n,at+n==p.inputFrames());drain();poll();}
 drain();require(rb.available()==-1 && received==p.renderFrames() && delivered==p.outputFrames(),"Uniform context did not drain exactly; no repair");report.generatedFrames=received;report.writtenFrames=delivered;return report;
}
} // namespace soundcurrent::daw
