// SPDX-License-Identifier: GPL-3.0-only
// Original bounded control-side scheduler experiment; not a shipping processor.
#include "../warp-map/warp_map.hpp"
#include "signalsmith-stretch.h"
#include <nlohmann/json.hpp>
#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <numbers>
#include <vector>
using namespace soundcurrent::daw;
using namespace soundcurrent::daw::experimental;
using Json=nlohmann::json;
namespace {
void require(bool value,const char *message){if(!value)throw std::runtime_error(message);}
void little(std::ofstream &file,std::uint32_t value,unsigned bytes=4){
    for(unsigned i=0;i<bytes;++i)file.put(char((value>>(8*i))&255));
}
void wave(const std::filesystem::path &path,const std::vector<float> &samples,unsigned channels){
    require(!std::filesystem::exists(path),"Owned probe WAV already exists");
    std::ofstream file(path,std::ios::binary);require(bool(file),"Cannot create owned probe WAV");
    const auto bytes=std::uint32_t(samples.size()*4);
    file.write("RIFF",4);little(file,36+bytes);file.write("WAVEfmt ",8);little(file,16);
    little(file,3,2);little(file,channels,2);little(file,48000);little(file,48000*channels*4);little(file,channels*4,2);little(file,32,2);
    file.write("data",4);little(file,bytes);
    for(auto x:samples)little(file,std::bit_cast<std::uint32_t>(x));
    file.flush();require(bool(file),"Cannot flush owned probe WAV");
}
}
int main(int argc,char **argv){try {
 require(argc==3,"Expected request and new owned output directory");
 const std::string text(argv[1]);require(text.size()<4096,"Probe request bank exceeded");const auto j=Json::parse(text);
 const auto family=j.at("family").get<std::string>(),profile=j.at("profile").get<std::string>();
 require(j.at("block").is_number_integer(),"Integer chunk required");const int block=j.at("block");require(block==97||block==512,"Frozen chunks only");
 require(family=="impulse"||family=="attack"||family=="sustain"||family=="cancellation","Frozen signal bank only");
 require(profile=="identity"||profile=="uniform"||profile=="nonuniform","Frozen map bank only");
 const unsigned channels=family=="impulse"?2:8;
 constexpr std::size_t frames=32768;const std::size_t target=profile=="identity"?frames:49152;
 const std::array<std::size_t,4> events{4096,12288,20480,28672},targets{6144,16384,32768,43008};
 const std::array<float,8> gains{1,-.5f,.25f,-.125f,.75f,-.375f,.1875f,-.09375f};
 std::vector<float> source(frames*channels);
 for(std::size_t f=0;f<frames;++f)for(unsigned ch=0;ch<channels;++ch){
  const auto delay=family=="cancellation"?0:3*ch;double x=0;
  if(family=="sustain")x=.7*std::sin(2*std::numbers::pi*440*(double(f)-delay)/48000)+.2*std::sin(2*std::numbers::pi*730*(double(f)-delay)/48000);
  else for(auto event:events){
   if(family=="impulse"&&f==event+delay)x+=1.5;
   if(family!="impulse"&&f>=event+delay&&f<event+delay+1536){
    const double t=double(f-event-delay),env=std::min(1.,t/8)*std::exp(-t/192);
    x+=env*(.8*std::sin(2*std::numbers::pi*440*t/48000)+.4*std::sin(2*std::numbers::pi*880*t/48000)+.2*std::sin(2*std::numbers::pi*1320*t/48000));
   }
  }
  source[f*channels+ch]=float(x*(family=="cancellation"?(ch%2?-1:1):gains[ch]));
 }
 std::vector<WarpMarker> markers;
 for(unsigned i=0;i<events.size();++i){auto id=std::string("00000000-0000-0000-0000-00000000000")+char('1'+i);
  const auto out=profile=="identity"?events[i]:(profile=="uniform"?events[i]*3/2:targets[i]);
  markers.push_back({Id(id),{Frame(events[i]),0,1},{Frame(out),0,1}});
 }
 WarpMap map({{0,0,1},48000,Frame(frames),Frame(frames),Frame(target),{0,0,1},{Frame(frames),0,1}},markers,ResourceLedger(1024*1024,"Anchored experiment"));
 signalsmith::stretch::SignalsmithStretch<float> candidate(20261010);candidate.presetDefault(int(channels),48000,false);
 const auto il=std::size_t(candidate.inputLatency()),ol=std::size_t(candidate.outputLatency());require(il<frames&&ol<8192,"Unbounded latency");
 std::vector<float> first(il*channels),input(512*channels),output(8192*channels),generated;generated.reserve((target+ol)*channels);
 std::vector<const float*> in(channels),initial(channels);std::vector<float*> out(channels);
 for(unsigned ch=0;ch<channels;++ch){for(std::size_t f=0;f<il;++f)first[ch*il+f]=source[f*channels+ch];initial[ch]=first.data()+ch*il;in[ch]=input.data()+ch*512;out[ch]=output.data()+ch*8192;}
 const auto initialRatio=double(map.points()[1].source.frame)/double(map.points()[1].output.frame);
 candidate.seek(initial.data(),int(il),initialRatio);
 Json schedule=Json::array(),points=Json::array();
 for(const auto &p:map.points())points.push_back({{"source",p.source.frame},{"output",p.output.frame},{"id",p.id?Json(p.id->str()):Json(nullptr)}});
 std::size_t cursor=il,zeroLookahead=0;
 auto append=[&](std::size_t n){for(std::size_t f=0;f<n;++f)for(unsigned ch=0;ch<channels;++ch){require(std::isfinite(out[ch][f]),"Nonfinite candidate output");generated.push_back(out[ch][f]);}};
 for(std::size_t at=0;at<target;){
  auto end=std::min(target,at+std::size_t(block));for(const auto &p:map.points())if(std::size_t(p.output.frame)>at){end=std::min(end,std::size_t(p.output.frame));break;}
  const auto exact=map.outputToSource({Frame(end),0,1});const auto next=std::size_t(exact.frame)+il;
  require(next>=cursor&&next-cursor<=512&&end>at,"Scheduler bank exceeded");const auto count=next-cursor;
  for(std::size_t f=0;f<count;++f){const auto raw=cursor+f;if(raw>=frames)++zeroLookahead;for(unsigned ch=0;ch<channels;++ch)input[ch*512+f]=raw<frames?source[raw*channels+ch]:0;}
  schedule.push_back({{"outputBegin",at},{"outputEnd",end},{"inputCursor",cursor},{"nextInputCursor",next},{"inverseEnd",Json::array({exact.frame,exact.fraction,exact.denominator})}});
  candidate.process(in.data(),int(count),out.data(),int(end-at));append(end-at);cursor=next;at=end;
 }
 require(cursor==frames+il&&zeroLookahead==il,"Explicit edge/lookahead mismatch");
 candidate.flush(out.data(),int(ol),float(double(frames-map.points()[map.points().size()-2].source.frame)/double(target-map.points()[map.points().size()-2].output.frame)));append(ol);
 require(generated.size()==(target+ol)*channels,"Generated extent mismatch");
 std::vector<float> rendered(generated.begin()+std::ptrdiff_t(ol*channels),generated.end());
 const std::filesystem::path root(argv[2]);require(std::filesystem::create_directory(root),"Output already exists");
 wave(root/"source.wav",source,channels);wave(root/"generated.wav",generated,channels);wave(root/"rendered.wav",rendered,channels);
 Json report={{"format","sc-anchored-stretch-render-v1"},{"request",j},{"frames",frames},{"target",target},{"channels",channels},{"points",points},{"schedule",schedule},
 {"inputLatency",il},{"outputLatency",ol},{"zeroFinalLookaheadFrames",zeroLookahead},{"generatedFrames",target+ol},{"trimmedLeadingFrames",ol},{"writtenFrames",rendered.size()/channels},
 {"seed",20261010},{"blockFrames",candidate.blockSamples()},{"intervalFrames",candidate.intervalSamples()},{"nativeAudio",false},{"shippingAdopted",false},{"fullQualityQualified",false}};
 std::ofstream metadata(root/"render.json");metadata<<report.dump(2)<<'\n';metadata.flush();require(bool(metadata),"Metadata failure");std::cout<<report.dump()<<'\n';return 0;
}catch(const std::exception &e){std::cerr<<e.what()<<'\n';return 1;}}
