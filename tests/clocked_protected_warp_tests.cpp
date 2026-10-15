// SPDX-License-Identifier: GPL-3.0-only
#include <soundcurrent/clocked_protected_warp.hpp>
#include <soundcurrent/protected_core_sampling.hpp>
#include <soundcurrent/stretch.hpp>
#include <nlohmann/json.hpp>
#include <algorithm>
#include <bit>
#include <cmath>
#include <iostream>
#include <numbers>
#include <limits>
#include <type_traits>
using namespace soundcurrent::daw;
using Json=nlohmann::json;
namespace {
std::uint64_t checks=0,compared=0,protectedSamples=0;
void check(bool b,const char *m){++checks;if(!b)throw std::runtime_error(m);}
template<class F>void refuses(F f){bool caught=false;try{f();}catch(const ProjectError &){caught=true;}check(caught,"Expected clocked render refusal");}
std::vector<WarpAnchor> markers(bool fractional){return {
 {Id("00000000-0000-0000-0000-000000000001"),{8192,fractional?1u:0u,3},{12288,fractional?2u:0u,5}},
 {Id("00000000-0000-0000-0000-000000000002"),{24576,fractional?2u:0u,7},{36864,fractional?1u:0u,11}}};}
struct Result{std::vector<float> pcm;ClockedProtectedReport report;};
Result render(const PreparedClockedProtectedWarp &p,const std::vector<float> &raw,ResourceLedger ledger){
 Result r;const auto baseline=ledger.usage().reservedBytes;const auto c=p.channels();
 r.report=renderClockedProtectedWarp(p,[&](Frame at,std::span<float> out){check(at>=0 && out.size()%c==0 && out.size()/c<=512 && std::size_t(at)*c+out.size()<=raw.size(),"Invalid bounded absolute raw read");std::copy_n(raw.begin()+at*c,out.size(),out.begin());},[&](auto out){check(out.size()%c==0 && out.size()/c<=512,"Invalid output block");r.pcm.insert(r.pcm.end(),out.begin(),out.end());},ledger,[]{});
 check(r.report.writtenFrames==p.outputFrames() && r.pcm.size()==std::size_t(p.outputFrames())*c,"Wrong clocked output length");check(ledger.usage().reservedBytes==baseline,"Render leaked explicit memory credit");return r;
}
}
int main(){try{
 static_assert(!std::is_copy_constructible_v<PreparedClockedProtectedWarp> && !std::is_move_assignable_v<PreparedClockedProtectedWarp>);
 ResourceLedger ledger(16*1024*1024,"Clocked protected acceptance");Json rows=Json::array();std::size_t maximumBuffer=0;double peak=0,maximumPitchError=0;
 for(unsigned scenario=0;scenario<7;++scenario){
  const bool fractional=scenario>=3,compressed=scenario==6;const unsigned rates[]{44100,48000,96000};const auto outputRate=compressed?48000u:rates[scenario%3];
  {
   const unsigned channels=2;const SourcePosition origin{4096,fractional?1u:0u,7};auto anchors=markers(fractional);if(compressed)for(auto &a:anchors)a.output.frame=a.source.frame*3/4;const Frame geometryFrames=compressed?24576:49152;
   ProtectedWarpPlan geometry({origin,48000,65536,32768,geometryFrames,{}, {32768,0,1}},anchors,{256,512,64,2048},ledger);
   const SourceFrameMap clock(48000,outputRate);const auto count=clock.projectFramesForSource(geometryFrames);
   PreparedClockedProtectedWarp p(geometry,clock,count,channels,ledger,[]{});
   check(p.pieces().size()==5,"Expected three gaps and two direct cores");
   std::vector<float> raw(65536*channels);for(Frame f=0;f<65536;++f)for(unsigned ch=0;ch<channels;++ch)raw[std::size_t(f)*channels+ch]=float(2.5*std::sin(2*std::numbers::pi*211*(double(f)-3*ch)/48000));
   const auto full=render(p,raw,ledger);peak=std::max(peak,full.report.floatPeak);maximumBuffer=std::max(maximumBuffer,full.report.maximumBufferedFrames);check(full.report.processedGaps==3 && full.report.floatPeak>1,"No actual nonunity gap/headroom path");
   // Independent tone-frequency check inside each actual stretched gap. This
   // catches omitted/wrong intrinsic pitch compensation after clock conversion.
   for(const auto &gap:geometry.gaps()){
    const auto first=Frame((double(gap.outputBegin.frame)+1024)*outputRate/48000),last=Frame((double(gap.outputEnd.frame)-1024)*outputRate/48000);
    std::vector<double> crossings;
    for(Frame f=first+1;f<last;++f){const auto a=full.pcm[std::size_t(f-1)*channels],b=full.pcm[std::size_t(f)*channels];if(a<0 && b>=0)crossings.push_back(double(f-1)+double(-a)/double(b-a));}
    check(crossings.size()>=8,"Tone observation is too short");const double hz=double(outputRate)*double(crossings.size()-1)/(crossings.back()-crossings.front());
    maximumPitchError=std::max(maximumPitchError,std::abs(hz-211));check(std::abs(hz-211)<2.11,"Gap pitch compensation differs by more than one percent");
   }
   PreparedProtectedCoreSampling cores(geometry,clock,count,channels,ledger,[]{});
   for(std::size_t wi=0;wi<cores.windows().size();++wi){const auto &w=cores.windows()[wi];for(Frame at=0;at<w.endOutput-w.firstOutput;at+=37){const auto n=std::uint32_t(std::min<Frame>(37,w.endOutput-w.firstOutput-at));const auto r=cores.sourceRange(wi,at,n);std::vector<float> out(std::size_t(n)*channels);cores.process(wi,at,r.first,std::span<const float>(raw).subspan(std::size_t(r.first)*channels,r.frames*channels),out);
     for(std::size_t i=0;i<out.size();++i){check(std::bit_cast<std::uint32_t>(out[i])==std::bit_cast<std::uint32_t>(full.pcm[std::size_t(w.firstOutput+at)*channels+i]),"Full renderer changed prepared direct core");++protectedSamples;}}}
   // Crop at both halos, through the core, and wholly inside a gap. Keep the
   // original full geometry and anchors, even when they lie outside the crop.
   std::vector<Frame> starts{0,117,Frame(outputRate/4)-300,Frame(outputRate/4)-80,Frame(outputRate/4)+400,count-71};
   for(const auto &piece:p.pieces())if(piece.stretched){if(piece.first>7)starts.push_back(piece.first-7);if(piece.last<count)starts.push_back(piece.last-7);}
   for(const auto first:starts){const auto frames=std::min<Frame>(193,count-first);PreparedClockedProtectedWarp crop(geometry,clock.advanced(first),frames,channels,ledger,[]{});const auto cut=render(crop,raw,ledger);
    for(std::size_t i=0;i<cut.pcm.size();++i){check(std::bit_cast<std::uint32_t>(cut.pcm[i])==std::bit_cast<std::uint32_t>(full.pcm[std::size_t(first)*channels+i]),"Crop changed intrinsic render or halo phase");++compared;}}
   // A genuinely fractional destination origin is independent of any crop.
   SourceFrameMap shifted(48000,outputRate,SourcePosition{12000,1,13});PreparedClockedProtectedWarp a(geometry,shifted,777,channels,ledger,[]{});const auto shiftedFull=render(a,raw,ledger);
   PreparedClockedProtectedWarp b(geometry,shifted.advanced(97),313,channels,ledger,[]{});const auto shiftedCrop=render(b,raw,ledger);
   check(std::equal(shiftedCrop.pcm.begin(),shiftedCrop.pcm.end(),shiftedFull.pcm.begin()+97*channels),"Fractional-origin crop changes output");compared+=shiftedCrop.pcm.size();
   rows.push_back({{"compressed",compressed},{"fractional",fractional},{"outputRate",outputRate},{"frames",count},{"gapFrames",full.report.generatedGapFrames},{"maximumBufferedFrames",full.report.maximumBufferedFrames},{"peak",full.report.floatPeak}});
  }
  check(!ledger.usage().reservedBytes,"Clocked plan retained credit");
 }
 // Touching protected spans and unity gaps must cover the output without holes,
 // and must not invoke the stretch backend or alter integer samples.
 for(unsigned channels:{1u,8u,256u}){
  const std::vector<WarpAnchor> anchors{{Id("00000000-0000-0000-0000-000000000003"),{1024,0,1},{1024,0,1}},{Id("00000000-0000-0000-0000-000000000004"),{1792,0,1},{1792,0,1}}};
  auto p=[&]{ProtectedWarpPlan geometry({{},48000,4096,4096,4096,{}, {4096,0,1}},anchors,{256,512,64,2048},ledger);return PreparedClockedProtectedWarp(geometry,SourceFrameMap(48000,48000),4096,channels,ledger,[]{});}();
  check(p.pieces().size()==1 && !p.pieces()[0].stretched,"Unity/touching spans were not merged");std::vector<float> raw(4096*channels);
  for(std::size_t i=0;i<raw.size();++i)raw[i]=i%3==0?-0.f:float(i%17)*.25f;
  const auto result=render(p,raw,ledger);check(!result.report.processedGaps,"Unity partition invoked backend");for(std::size_t i=0;i<raw.size();++i)check(std::bit_cast<std::uint32_t>(raw[i])==std::bit_cast<std::uint32_t>(result.pcm[i]),"Unity bit/headroom/channel preservation changed");
 }
 {
  ProtectedWarpPlan geometry({{},48000,65536,32768,49152,{}, {32768,0,1}},markers(false),{256,512,64,2048},ledger);
  SourceFrameMap clock(48000,48000);const auto baseline=ledger.usage().reservedBytes;
  refuses([&]{PreparedClockedProtectedWarp p(geometry,clock,0,2,ledger,[]{});});refuses([&]{PreparedClockedProtectedWarp p(geometry,clock,49153,2,ledger,[]{});});
  refuses([&]{PreparedClockedProtectedWarp p(geometry,clock,49152,0,ledger,[]{});});refuses([&]{PreparedClockedProtectedWarp p(geometry,clock,49152,257,ledger,[]{});});
  refuses([&]{PreparedClockedProtectedWarp p(geometry,clock,49152,2,ledger,{});});refuses([&]{PreparedClockedProtectedWarp p(geometry,clock,49152,2,ledger,[]{},{4096,1});});
  unsigned polls=0;refuses([&]{PreparedClockedProtectedWarp p(geometry,clock,49152,2,ledger,[&]{if(++polls==30)throw ProjectError(ErrorCode::Canceled,"Canceled preparation");});});check(polls==30 && ledger.usage().reservedBytes==baseline,"Preparation cancellation retained credit");
  PreparedClockedProtectedWarp p(geometry,clock,49152,2,ledger,[]{});const auto held=ledger.usage().reservedBytes;unsigned writes=0;auto write=[&](auto){++writes;};
  refuses([&]{renderClockedProtectedWarp(p,[](Frame,auto){},write,ledger,[]{});});check(writes==0 && ledger.usage().reservedBytes==held,"Incomplete read emitted or leaked");
  refuses([&]{renderClockedProtectedWarp(p,[](Frame,auto x){std::fill(x.begin(),x.end(),INFINITY);},write,ledger,[]{});});check(writes==0,"Nonfinite source emitted");
  ResourceLedger tiny(1,"Scratch refusal");refuses([&]{renderClockedProtectedWarp(p,[](Frame,auto){},write,tiny,[]{});});check(!tiny.usage().reservedBytes && writes==0,"Insufficient scratch wrote output");
  polls=0;refuses([&]{renderClockedProtectedWarp(p,[](Frame,auto x){std::fill(x.begin(),x.end(),0.f);},write,ledger,[&]{if(++polls==120)throw ProjectError(ErrorCode::Canceled,"Canceled study");});});check(polls==120 && ledger.usage().reservedBytes==held,"Canceled backend retained scratch");
  writes=0;refuses([&]{renderClockedProtectedWarp(p,[](Frame,auto x){std::fill(x.begin(),x.end(),0.f);},[&](auto){++writes;throw ProjectError(ErrorCode::Io,"Output refused");},ledger,[]{});});check(writes==1 && ledger.usage().reservedBytes==held,"Write failure retained scratch/backend");
  auto moved=std::move(p);check(!moved.pieces().empty(),"Moved plan lost geometry");
 }
 check(!ledger.usage().reservedBytes,"Final clocked ledger not empty");
 std::cout<<Json{{"checks",checks},{"rows",rows},{"cropComparedSamples",compared},{"protectedComparedSamples",protectedSamples},{"maximumBufferedFrames",maximumBuffer},{"peak",peak},{"maximumPitchErrorHz",maximumPitchError},{"allCreditsReleased",true},{"workerIntegrated",false},{"qualityAdmission",false}}.dump()<<'\n';return 0;
}catch(const std::exception &e){std::cerr<<e.what()<<'\n';return 1;}}
