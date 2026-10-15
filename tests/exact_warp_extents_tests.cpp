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
using namespace soundcurrent::daw;
using Json=nlohmann::json;
namespace {
std::uint64_t checks=0,cropSamples=0,coreSamples=0;
void check(bool v,const char *m){++checks;if(!v)throw std::runtime_error(m);}
template<class F>void refuses(F f){bool caught=false;try{f();}catch(const ProjectError &){caught=true;}check(caught,"Missing exact-domain refusal");}
Json pos(SourcePosition p){return Json::array({p.frame,p.fraction,p.denominator});}
SourcePosition pos(const Json &p){return {p[0].get<Frame>(),p[1].get<std::uint64_t>(),p[2].get<std::uint64_t>()};}
const Id markerId("00000000-0000-0000-0000-000000000001");
Json probe(const Json &j){
 ResourceLedger ledger(8*1024*1024);Json result;
 {
  const auto input=pos(j.at("input")),output=pos(j.at("output")),origin=pos(j.at("origin"));
  std::vector<WarpAnchor> markers{{markerId,pos(j.at("anchorSource")),pos(j.at("anchorOutput"))}};
  ExactClipWarpRegion region{origin,48000,j.at("sourceFrames"),input,output,{},input};
  ClipWarpMap map(region,markers,ledger);result["points"]=Json::array();
  for(const auto &p:map.points())result["points"].push_back({pos(p.source),pos(p.output)});
  result["rows"]=Json::array();
  for(const auto &p:j.at("queries")){const auto at=pos(p),mapped=map.sourceToOutput(at);result["rows"].push_back({pos(mapped),pos(map.outputToSource(mapped)),pos(map.rawAt(at))});}
 }
 check(!ledger.usage().reservedBytes,"Oracle leaked credits");return result;
}
std::vector<float> render(const PreparedClockedProtectedWarp &p,const std::vector<float> &raw,ResourceLedger ledger){
 std::vector<float> out;auto baseline=ledger.usage().reservedBytes;
 auto r=renderClockedProtectedWarp(p,[&](Frame first,std::span<float> samples){check(first>=0 && samples.size()<=512 && std::size_t(first)+samples.size()<=raw.size(),"Unbounded exact raw read");std::copy_n(raw.begin()+first,samples.size(),samples.begin());},[&](std::span<const float> samples){check(samples.size()<=512,"Unbounded exact write");out.insert(out.end(),samples.begin(),samples.end());},ledger,[]{});
 check(r.writtenFrames==p.outputFrames() && out.size()==std::size_t(p.outputFrames()),"Exact duration lost");
 check(ledger.usage().reservedBytes==baseline,"Render credits leaked");return out;
}
}
int main(int argc,char **argv){try{
 if(argc==2 && std::string_view(argv[1])=="--probe"){Json j;std::cin>>j;Json out=Json::array();for(const auto &row:j)out.push_back(probe(row));std::cout<<out.dump()<<'\n';return 0;}
 check(argc==1,"Unexpected exact extent argument");ResourceLedger ledger(32*1024*1024);
 const ExactClipWarpRegion region{{1024,1,7},48000,32000,{20000,1,3},{26000,2,5},{},{20000,1,3}};
 const std::vector<WarpAnchor> anchors{{markerId,{8192,1,3},{12288,1,3}}};
 std::vector<float> raw(32000);for(std::size_t i=0;i<raw.size();++i)raw[i]=float(2.5*std::sin(2*std::numbers::pi*211*double(i)/48000));
 {
  ClipWarpMap map(region,anchors,ledger);check(map.points().back().source==region.inputExtent && map.points().back().output==region.outputExtent,"Exact endpoints changed");
  check(map.sourceToOutput(region.inputExtent)==region.outputExtent && map.outputToSource(region.outputExtent)==region.inputExtent,"Endpoint lookup changed");
  check(map.rawAt(region.inputExtent)==addSourcePosition(region.rawOrigin,region.inputExtent),"Raw endpoint changed");
  refuses([&]{(void)map.region();});refuses([&]{(void)map.vendorInteriorFrames();});
  refuses([&]{(void)map.outputToSource(addSourcePosition(region.outputExtent,{0,1,100}));});
  refuses([&]{(void)map.rawAt(addSourcePosition(region.inputExtent,{0,1,100}));});
  auto integral=region;integral.inputExtent={20000,0,1};integral.outputExtent={26000,0,1};integral.visibleSourceEnd=integral.inputExtent;
  ClipWarpMap explicitExact(integral,anchors,ledger);refuses([&]{(void)explicitExact.region();});
 }
 for(unsigned rate:{48000u,96000u}){
  const SourceFrameMap clock(48000,rate);const Frame count=rate==48000?26001:52001;
  // The exclusive sample-grid endpoint exceeds the semantic extent. Only
  // actual sample positions are admitted; the exact boundary remains 26000.4.
  auto prepared=[&]{ProtectedWarpPlan plan(region,anchors,{512,512,64,64},ledger);
   check(plan.map().points().back().source==region.inputExtent && plan.map().points().back().output==region.outputExtent,"Protected endpoints changed");
   refuses([&]{validateProtectedWarpRender(plan,48000,1);});
   return PreparedClockedProtectedWarp(plan,clock,count,1,ledger,[]{});}();
  const auto full=render(prepared,raw,ledger);
  for(Frame first:{Frame(0),Frame(rate==48000?11788:23576),count-97}){
   const Frame length=first==0?97:std::min<Frame>(512,count-first);
   auto crop=[&]{ProtectedWarpPlan plan(region,anchors,{512,512,64,64},ledger);return PreparedClockedProtectedWarp(plan,SourceFrameMap(48000,rate,clock.at(first)),length,1,ledger,[]{});}();
   const auto pcm=render(crop,raw,ledger);for(Frame i=0;i<length;++i){check(std::bit_cast<std::uint32_t>(pcm[std::size_t(i)])==std::bit_cast<std::uint32_t>(full[std::size_t(first+i)]),"Exact outer crop changed audio");++cropSamples;}
  }
  ProtectedWarpPlan plan(region,anchors,{512,512,64,64},ledger);PreparedProtectedCoreSampling core(plan,clock,count,1,ledger,[]{});
  for(std::size_t w=0;w<core.windows().size();++w){const auto &window=core.windows()[w];for(Frame offset=0;offset<window.endOutput-window.firstOutput;){const auto n=unsigned(std::min<Frame>(512,window.endOutput-window.firstOutput-offset));const auto r=core.sourceRange(w,offset,n);std::vector<float> pcm(n);core.process(w,offset,r.first,std::span<const float>(raw).subspan(std::size_t(r.first),r.frames),pcm);for(unsigned i=0;i<n;++i){check(std::bit_cast<std::uint32_t>(pcm[i])==std::bit_cast<std::uint32_t>(full[std::size_t(window.firstOutput+offset+i)]),"Exact core changed raw sampling");++coreSamples;}offset+=n;}}
  refuses([&]{PreparedClockedProtectedWarp invalid(plan,SourceFrameMap(48000,48000,region.outputExtent),1,1,ledger,[]{});});
 }
 {
  ExactClipWarpRegion shortRegion{{1024,1,7},48000,16000,{10000,1,3},{12000,2,5},{},{10000,1,3}};
  std::vector<WarpAnchor> shortAnchors{{markerId,{4500,1,3},{6000,1,3}}};
  ProtectedWarpPlan shortPlan(shortRegion,shortAnchors,{512,512,64,64},ledger);
  PreparedClockedProtectedWarp shortRender(shortPlan,SourceFrameMap(48000,48000),12001,1,ledger,[]{});
  const auto baseline=ledger.usage().reservedBytes;refuses([&]{(void)render(shortRender,raw,ledger);});
  check(ledger.usage().reservedBytes==baseline,"Short exact render leaked backend credits");
 }
 check(!ledger.usage().reservedBytes,"Owned exact plans leaked");
 {
  // Historical source-grid admission remains distinct from semantic admission.
  ClipWarpMap old(ClipWarpRegion{{0,1,2},48000,8,8,8,{}, {8,0,1}},{},ledger);
  check(old.hasLegacyRegion() && old.region().inputFrames==8,"Legacy grid changed");
  refuses([&]{ClipWarpMap exact(ExactClipWarpRegion{{0,1,2},48000,8,{8,0,1},{8,0,1},{},{8,0,1}},{},ledger);});
 }
 {
  ExactClipWarpRegion exact=region;exact.inputExtent={20000,0,1};exact.outputExtent={26000,0,1};exact.visibleSourceEnd=exact.inputExtent;
  ProtectedWarpPlan old(ClipWarpRegion{region.rawOrigin,48000,32000,20000,26000,{}, {20000,0,1}},anchors,{512,512,64,64},ledger);
  ProtectedWarpPlan current(exact,anchors,{512,512,64,64},ledger);
  PreparedClockedProtectedWarp a(old,SourceFrameMap(48000,48000),26000,1,ledger,[]{}),b(current,SourceFrameMap(48000,48000),26000,1,ledger,[]{});
  const auto previous=render(a,raw,ledger),actual=render(b,raw,ledger);check(previous.size()==actual.size(),"Integer-domain size changed");
  for(std::size_t i=0;i<actual.size();++i)check(std::bit_cast<std::uint32_t>(previous[i])==std::bit_cast<std::uint32_t>(actual[i]),"Integer-domain clocked output changed");
 }
 for(unsigned mutation=0;mutation<5;++mutation){auto bad=region;if(mutation==0)bad.inputExtent={};if(mutation==1)bad.outputExtent={0,0,0};if(mutation==2)bad.visibleSourceEnd=addSourcePosition(bad.inputExtent,{0,1,5});if(mutation==3)bad.rawOrigin={32000,0,1};if(mutation==4)bad.outputExtent={500,0,1};refuses([&]{ProtectedWarpPlan invalid(bad,anchors,{512,512,64,64},ledger);});check(!ledger.usage().reservedBytes,"Rejected exact plan leaked");}
 ResourceLedger tiny(1);refuses([&]{ProtectedWarpPlan denied(region,anchors,{512,512,64,64},tiny);});check(!tiny.usage().reservedBytes,"Tiny exact ledger leaked");
 std::cout<<Json{{"checks",checks},{"cropSamples",cropSamples},{"coreSamples",coreSamples},{"fractionalOuterExtentsPreserved",true},{"legacyAdaptersRefused",true},{"allCreditsReleased",!ledger.usage().reservedBytes},{"workerIntegrated",false}}.dump()<<'\n';return 0;
}catch(const std::exception &e){std::cerr<<e.what()<<'\n';return 1;}}
