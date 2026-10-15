// SPDX-License-Identifier: GPL-3.0-only
#include <soundcurrent/protected_core_sampling.hpp>
#include <nlohmann/json.hpp>
#include <algorithm>
#include <bit>
#include <cmath>
#include <iostream>
#include <limits>
#include <numbers>
#include <type_traits>
using namespace soundcurrent::daw;
using Json=nlohmann::json;
namespace {
std::uint64_t checks=0;
void check(bool v,const char *m){++checks;if(!v)throw std::runtime_error(m);}
template<class F>void refuses(F f){bool seen=false;try{f();}catch(const ProjectError &){seen=true;}check(seen,"Expected protected sampling refusal");}
Json position(SourcePosition p){return Json::array({p.frame,p.fraction,p.denominator});}
SourcePosition position(const Json &p){return {p[0].get<Frame>(),p[1].get<std::uint64_t>(),p[2].get<std::uint64_t>()};}
double value(SourcePosition p){return double(p.frame)+double(p.fraction)/double(p.denominator);}
std::vector<WarpAnchor> markers(bool fractional){return {
 {Id("00000000-0000-0000-0000-000000000001"),{8192,fractional?1u:0u,3},{12288,fractional?2u:0u,5}},
 {Id("00000000-0000-0000-0000-000000000002"),{24576,fractional?2u:0u,7},{36864,fractional?1u:0u,11}}};}
std::vector<float> render(const PreparedProtectedCoreSampling &prepared,std::size_t index,
    const std::vector<float> &raw,unsigned channels,unsigned block){
 const auto &w=prepared.windows()[index];std::vector<float> result(std::size_t(w.endOutput-w.firstOutput)*channels);
 for(Frame at=0;at<w.endOutput-w.firstOutput;){const auto n=unsigned(std::min<Frame>(block,w.endOutput-w.firstOutput-at));
  const auto range=prepared.sourceRange(index,at,n);check(range.frames<=prepared.maximumSourceWindowFrames(),"Raw context exceeded declared bound");
  check(range.first>=0 && std::uint64_t(range.first)+range.frames<=raw.size()/channels,"Raw context outside asset");
  prepared.process(index,at,range.first,std::span<const float>(raw).subspan(std::size_t(range.first)*channels,range.frames*channels),
                   std::span<float>(result).subspan(std::size_t(at)*channels,std::size_t(n)*channels));at+=n;
 }return result;
}
Json planCase(const Json &input){
 ResourceLedger ledger(8*1024*1024,"Protected sampling oracle");Json result;
 {
  std::vector<WarpAnchor> anchors;unsigned index=0;
  for(const auto &m:input.at("markers")){auto id=markers(false).at(index++).id;anchors.push_back({id,position(m[0]),position(m[1])});}
  ClipWarpRegion region{position(input.at("rawOrigin")),48000,input.value("sourceFrames",Frame(65536)),32768,49152,{}, {32768,0,1}};
  ProtectedWarpPlan plan(region,anchors,{256,512,64,64},ledger);
  const auto clock=SourceFrameMap(input.at("sourceRate"),input.at("outputRate"),position(input.at("clockOrigin")));
  PreparedProtectedCoreSampling prepared(plan,clock,input.at("outputFrames"),2,ledger,[]{});
  result=Json::array();for(const auto &w:prepared.windows())result.push_back({{"owner",w.owner.str()},
   {"first",w.firstOutput},{"end",w.endOutput},{"rawBegin",position(w.rawBegin)},{"rawEnd",position(w.rawEnd)},
   {"outputBegin",position(w.outputBegin)},{"outputEnd",position(w.outputEnd)},
   {"firstRaw",position(w.rawSamples.at(0))},{"lastRaw",position(w.rawSamples.at(w.endOutput-w.firstOutput-1))}});
 }
 check(!ledger.usage().reservedBytes,"Oracle plan retained credits");return result;
}
}
int main(int argc,char **argv){try{
 if(argc==2 && std::string_view(argv[1])=="--plan"){Json input;std::cin>>input;Json result=Json::array();for(const auto &row:input)result.push_back(planCase(row));std::cout<<result.dump()<<'\n';return 0;}
 check(argc==1,"Unexpected sampling fixture argument");
 static_assert(!std::is_copy_constructible_v<PreparedProtectedCoreSampling> && !std::is_move_assignable_v<PreparedProtectedCoreSampling>);
 ResourceLedger ledger(8*1024*1024,"Protected core acceptance");std::uint64_t samples=0,integerSamples=0;double maximumError=0,peak=0;
 for(bool fractional:{false,true})for(unsigned outputRate:{44100u,48000u,96000u})for(bool cropped:{false,true})for(unsigned channels:{1u,2u,8u}){
  {
   const SourcePosition origin{1024,fractional?1u:0u,7};const auto anchors=markers(fractional);
   const SourcePosition clockOrigin=cropped?SourcePosition{12300,1,11}:SourcePosition{};
   const SourceFrameMap clock(48000,outputRate,clockOrigin);
   const Frame count=cropped?137:clock.projectFramesForSource(49151);
   auto prepared=[&]{ProtectedWarpPlan plan({origin,48000,65536,32768,49152,{}, {32768,0,1}},anchors,{256,512,64,64},ledger);
    return PreparedProtectedCoreSampling(plan,clock,count,channels,ledger,[]{});}();
   check(!prepared.windows().empty(),"No protected samples admitted");
   // Original planner has been destroyed. The sampling plan owns every binding.
   std::vector<float> raw(std::size_t(65536)*channels);
   for(Frame f=0;f<65536;++f)for(unsigned ch=0;ch<channels;++ch)
    raw[std::size_t(f)*channels+ch]=float(2.5*std::sin(2*std::numbers::pi*211*(double(f)-3*ch)/48000));
   for(std::size_t index=0;index<prepared.windows().size();++index){
    const auto &w=prepared.windows()[index];const auto actual=render(prepared,index,raw,channels,512);
    check(actual==render(prepared,index,raw,channels,37),"Protected sample partition changed audio");
    const auto a=std::find_if(anchors.begin(),anchors.end(),[&](const auto &m){return m.id==w.owner;});check(a!=anchors.end(),"Semantic owner lost");
    for(Frame at=0;at<w.endOutput-w.firstOutput;++at)for(unsigned ch=0;ch<channels;++ch){
     const auto rawAt=value(origin)+value(a->source)+value(clock.at(w.firstOutput+at))-value(a->output);
     const auto analytic=2.5*std::sin(2*std::numbers::pi*211*(rawAt-3*ch)/48000);
     const auto v=actual[std::size_t(at)*channels+ch];const auto error=std::abs(double(v)-analytic);
     maximumError=std::max(maximumError,error);peak=std::max(peak,std::abs(double(v)));check(error<2e-6,"Protected fractional/clock phase differs from analytic signal");++samples;
     if(!fractional && outputRate==48000 && !cropped){check(std::bit_cast<std::uint32_t>(v)==std::bit_cast<std::uint32_t>(raw[std::size_t(Frame(rawAt))*channels+ch]),"Integer protected copy changed bits");++integerSamples;}
    }
    if(index==0){
     auto tail=actual;const Frame skip=std::min<Frame>(17,w.endOutput-w.firstOutput-1);const auto n=unsigned(std::min<Frame>(31,w.endOutput-w.firstOutput-skip));
     const auto range=prepared.sourceRange(index,skip,n);std::vector<float> out(std::size_t(n)*channels);
     prepared.process(index,skip,range.first,std::span<const float>(raw).subspan(std::size_t(range.first)*channels,range.frames*channels),out);
     check(std::equal(out.begin(),out.end(),tail.begin()+skip*channels),"Seek changed protected phase");
     ProtectedWarpPlan full({origin,48000,65536,32768,49152,{}, {32768,0,1}},anchors,{256,512,64,64},ledger);
     PreparedProtectedCoreSampling crop(full,clock.advanced(w.firstOutput+skip),n,channels,ledger,[]{});
     check(crop.windows().size()==1 && crop.windows()[0].firstOutput==0 && crop.windows()[0].endOutput==n,"Cropped core sample ownership differs");
     check(render(crop,0,raw,channels,5)==out,"Cropping changed protected sample phase");
    }
   }
  }
  check(!ledger.usage().reservedBytes,"Sampling case retained credits");
 }
 {
  ProtectedWarpPlan plan({{},48000,65536,32768,49152,{}, {32768,0,1}},markers(false),{256,512,64,64},ledger);
  const auto baseline=ledger.usage().reservedBytes;SourceFrameMap clock(48000,48000);
  refuses([&]{PreparedProtectedCoreSampling p(plan,clock,0,2,ledger,[]{});});
  refuses([&]{PreparedProtectedCoreSampling p(plan,clock,49153,2,ledger,[]{});});
  refuses([&]{PreparedProtectedCoreSampling p(plan,clock,49152,0,ledger,[]{});});
  refuses([&]{PreparedProtectedCoreSampling p(plan,clock,49152,257,ledger,[]{});});
  refuses([&]{PreparedProtectedCoreSampling p(plan,clock,49152,2,ledger,{});});
  refuses([&]{PreparedProtectedCoreSampling p(plan,clock,49152,2,ledger,[]{},{1,1024*1024});});
  refuses([&]{PreparedProtectedCoreSampling p(plan,clock,49152,2,ledger,[]{},{4096,1});});
  ResourceLedger tiny(1,"Too small");refuses([&]{PreparedProtectedCoreSampling p(plan,clock,49152,2,tiny,[]{});});
  unsigned polls=0;refuses([&]{PreparedProtectedCoreSampling p(plan,clock,49152,2,ledger,[&]{if(++polls==20)throw ProjectError(ErrorCode::Canceled,"Canceled plan");});});
  check(polls==20 && ledger.usage().reservedBytes==baseline && !tiny.usage().reservedBytes,"Refused preparation retained credits");
  PreparedProtectedCoreSampling p(plan,clock,49152,2,ledger,[]{});std::vector<float> raw(65536*2,2.5f),out(2,91.f);
  const auto unchanged=out;
  refuses([&]{p.sourceRange(p.windows().size(),0,1);});refuses([&]{p.sourceRange(0,-1,1);});
  refuses([&]{p.sourceRange(0,0,513);});refuses([&]{p.sourceRange(0,p.windows()[0].endOutput-p.windows()[0].firstOutput,1);});
  refuses([&]{p.process(0,0,0,{},out);});check(out==unchanged,"Rejected context wrote output");
  const auto range=p.sourceRange(0,0,1);auto src=std::span<float>(raw).subspan(std::size_t(range.first)*2,range.frames*2);
  refuses([&]{p.process(0,0,range.first,src,src.first(2));});check(raw[std::size_t(range.first)*2]==2.5f,"Aliasing changed raw input");
  src[0]=std::numeric_limits<float>::quiet_NaN();refuses([&]{p.process(0,0,range.first,src,out);});check(out==unchanged,"Nonfinite raw input wrote output");
  auto moved=std::move(p);check(!moved.windows().empty(),"Moved sampler lost owned bindings");
 }

 {
  ProtectedWarpPlan plan({{1024,1,7},48000,65536,32768,49152,{}, {32768,0,1}},markers(true),{256,512,64,64},ledger);
  PreparedProtectedCoreSampling p(plan,SourceFrameMap(48000,44100,SourcePosition{12289,1,13}),3,256,ledger,[]{});
  check(p.windows().size()==1,"256-channel core missing");const auto range=p.sourceRange(0,0,3);
  std::vector<float> raw(range.frames*256),out(3*256);
  for(std::size_t f=0;f<range.frames;++f)for(unsigned c=0;c<256;++c)raw[f*256+c]=2.5f+float(c)/512;
  p.process(0,0,range.first,raw,out);
  for(std::size_t f=0;f<3;++f)for(unsigned c=0;c<256;++c)check(std::abs(out[f*256+c]-(2.5f+float(c)/512))<2e-6,"256-channel protected isolation/headroom changed");
 }
 {
  const std::vector<WarpAnchor> edge{{Id("00000000-0000-0000-0000-000000000003"),{256,0,1},{256,0,1}},
                                   {Id("00000000-0000-0000-0000-000000000004"),{7679,0,1},{7679,0,1}}};
  ProtectedWarpPlan plan({{0,1,7},48000,8192,8192,8192,{}, {8192,0,1}},edge,{256,512,64,1},ledger);
  PreparedProtectedCoreSampling p(plan,SourceFrameMap(48000,48000),8192,1,ledger,[]{});
  check(p.sourceRange(0,0,1).first==0,"Left raw-asset zero-extension boundary missing");
  const auto &last=p.windows().back();const auto range=p.sourceRange(1,last.endOutput-last.firstOutput-1,1);
  check(range.first+Frame(range.frames)==8192,"Right raw-asset zero-extension boundary missing");
  std::vector<float> raw(8192,2.5f);for(unsigned index=0;index<2;++index){const auto out=render(p,index,raw,1,37);check(std::all_of(out.begin(),out.end(),[](float v){return std::isfinite(v);}),"Asset-edge output is nonfinite");}
 }

 {
  const Frame available=INT64_MAX;const SourcePosition origin{available-65536,0,1};
  ProtectedWarpPlan plan({origin,48000,available,32768,49152,{}, {32768,0,1}},markers(false),{256,512,64,64},ledger);
  PreparedProtectedCoreSampling p(plan,SourceFrameMap(48000,48000,SourcePosition{12288,0,1}),1,2,ledger,[]{});
  const auto range=p.sourceRange(0,0,1);check(range.first==available-65536+8192 && range.frames==1,"Large exact raw coordinate changed");
  std::array<float,2> raw{-0.f,2.5f},out{};p.process(0,0,range.first,raw,out);
  check(std::bit_cast<std::uint32_t>(raw[0])==std::bit_cast<std::uint32_t>(out[0]) && out[1]==raw[1],"Large-coordinate neutral copy changed bits/headroom");
 }
 {
  ProtectedWarpPlan plan({{1024,1,UINT64_MAX},48000,65536,32768,49152,{}, {32768,0,1}},markers(false),{256,512,64,64},ledger);
  const auto baseline=ledger.usage().reservedBytes;
  refuses([&]{PreparedProtectedCoreSampling p(plan,SourceFrameMap(48000,48000,SourcePosition{12288,1,2}),1,2,ledger,[]{});});
  check(ledger.usage().reservedBytes==baseline,"Unrepresentable rational coordinate retained credits");
 }
 check(!ledger.usage().reservedBytes && peak>1,"Final credits/headroom differ");
 std::cout<<Json{{"checks",checks},{"samples",samples},{"integerBitExactSamples",integerSamples},{"maximumAnalyticError",maximumError},{"peak",peak},{"allCreditsReleased",true},{"workerIntegrated",false},{"fullQualityAdmission",false}}.dump()<<'\n';return 0;
}catch(const std::exception &e){std::cerr<<e.what()<<'\n';return 1;}}
