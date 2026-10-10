// SPDX-License-Identifier: GPL-3.0-only
// Original transient-protected control-side rendering, no shipping hook.
#include "protected_plan.hpp"
#include <rubberband/RubberBandStretcher.h>
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
using RB=RubberBand::RubberBandStretcher;
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
int main(int argc,char **argv){try{
 require(argc==3,"Expected frozen request and exclusive output directory");const std::string text(argv[1]);require(text.size()<4096,"Request exceeds probe bank");const auto j=Json::parse(text);
 const auto family=j.at("family").get<std::string>(),profile=j.at("profile").get<std::string>();
 require(family=="impulse"||family=="attack"||family=="long-attack"||family=="attack-bed"||family=="sustain"||family=="cancellation","Unknown frozen family");
 require(profile=="identity"||profile=="uniform"||profile=="nonuniform","Unknown frozen map");
 constexpr std::size_t frames=32768;const unsigned channels=family=="impulse"?2:8;const std::size_t target=profile=="identity"?frames:49152;
 const std::array<std::size_t,4> events{4096,12288,20480,28672},targets{6144,16384,32768,43008};
 const std::array<float,8> gains{1,-.5f,.25f,-.125f,.75f,-.375f,.1875f,-.09375f};
 std::vector<float> source(frames*channels),result(target*channels);
 for(std::size_t f=0;f<frames;++f)for(unsigned ch=0;ch<channels;++ch){const auto delay=family=="cancellation"?0:3*ch;double x=0;
  if(family=="sustain"||family=="attack-bed")x=(family=="sustain"?.7:.07)*std::sin(2*std::numbers::pi*440*(double(f)-delay)/48000)+(family=="sustain"?.2:.02)*std::sin(2*std::numbers::pi*730*(double(f)-delay)/48000);
  if(family!="sustain")for(auto event:events){
   if(family=="impulse"&&f==event+delay)x+=1.5;
   const auto duration=family=="long-attack"?6144:1536;
   if(family!="impulse"&&f>=event+delay&&f<event+delay+duration){const double t=double(f-event-delay),env=std::min(1.,t/8)*std::exp(-t/(family=="long-attack"?2048:192));x+=env*(.8*std::sin(2*std::numbers::pi*440*t/48000)+.4*std::sin(2*std::numbers::pi*880*t/48000)+.2*std::sin(2*std::numbers::pi*1320*t/48000));}
  }
  source[f*channels+ch]=float(x*(family=="cancellation"?(ch%2?-1:1):gains[ch]));
 }
 std::vector<WarpMarker> markers;
 for(unsigned i=0;i<events.size();++i){auto id=std::string("00000000-0000-0000-0000-00000000000")+char('1'+i);const auto out=profile=="identity"?events[i]:(profile=="uniform"?events[i]*3/2:targets[i]);markers.push_back({Id(id),{Frame(events[i]),0,1},{Frame(out),0,1}});}
 ResourceLedger ledger(4*1024*1024,"Protected experiment");ProtectionPolicy policy;ProtectedPlan plan({{0,0,1},48000,Frame(frames),Frame(frames),Frame(target),{0,0,1},{Frame(frames),0,1}},markers,policy,ledger);
 auto integer=[](SourcePosition p){require(!p.fraction,"Integer-only first protected renderer refuses fractional coordinates");return std::size_t(p.frame);};
 const std::filesystem::path root(argv[2]);require(std::filesystem::create_directory(root),"Owned output already exists");wave(root/"source.wav",source,channels);
 Json spans=Json::array(),gaps=Json::array(),points=Json::array();
 for(const auto &s:plan.spans()){
  const auto sb=integer(s.sourceBegin),se=integer(s.sourceEnd),ob=integer(s.outputBegin),oe=integer(s.outputEnd);
  require(se-sb==oe-ob&&se<=frames&&oe<=target,"Protected copy geometry");
  for(std::size_t f=0;f<se-sb;++f)for(unsigned ch=0;ch<channels;++ch)result[(ob+f)*channels+ch]=source[(sb+f)*channels+ch];
  spans.push_back({{"owner",s.owner.str()},{"sourceAnchor",s.sourceAnchor.frame},{"outputAnchor",s.outputAnchor.frame},{"sourceBegin",sb},{"sourceEnd",se},{"outputBegin",ob},{"outputEnd",oe},{"coreSourceBegin",sb+64},{"coreSourceEnd",se-64},{"coreOutputBegin",ob+64},{"coreOutputEnd",oe-64}});
 }
 unsigned index=0;
 for(const auto &gap:plan.gaps()){
  const auto sb=integer(gap.sourceBegin),se=integer(gap.sourceEnd),ob=integer(gap.outputBegin),oe=integer(gap.outputEnd);
  const auto before=sb?std::size_t(policy.halo):0,after=se<frames?std::size_t(policy.halo):0;
  require(sb>=before&&se+after<=frames&&ob>=before&&oe+after<=target,"Gap halo exceeds actual source/output");
  const auto count=se-sb+before+after,outCount=oe-ob+before+after;std::vector<float> raw(count*channels),rendered;
  for(std::size_t f=0;f<count;++f)for(unsigned ch=0;ch<channels;++ch)raw[f*channels+ch]=source[(sb-before+f)*channels+ch];
  const bool unity=(se-sb==oe-ob);
  if(unity)rendered=raw;
  else{
   RB rb(48000,channels,RB::OptionProcessOffline|RB::OptionThreadingNever|RB::OptionEngineFiner|RB::OptionFormantPreserved|RB::OptionPitchHighQuality|(channels==2?RB::OptionChannelsTogether:RB::OptionChannelsApart),double(outCount)/count,1.);
   rb.setDebugLevel(0);rb.setMaxProcessSize(512);rb.setExpectedInputDuration(count);
   std::map<std::size_t,std::size_t> keys;
   if(before)keys.emplace(before,before);
   if(after)keys.emplace(before+se-sb,before+oe-ob);
   if(!keys.empty())rb.setKeyFrameMap(keys);
   std::vector<float> input(512*channels),output(512*channels);std::vector<const float*> in(channels);std::vector<float*> out(channels);
   for(unsigned ch=0;ch<channels;++ch){in[ch]=input.data()+ch*512;out[ch]=output.data()+ch*512;}
   auto read=[&](std::size_t at,std::size_t n){for(unsigned ch=0;ch<channels;++ch)for(std::size_t f=0;f<n;++f)input[ch*512+f]=raw[(at+f)*channels+ch];};
   for(std::size_t at=0;at<count;at+=512){const auto n=std::min<std::size_t>(512,count-at);read(at,n);rb.study(in.data(),n,at+n==count);}
   auto drain=[&]{while(rb.available()>0){const auto n=std::size_t(std::min(512,rb.available()));const auto got=rb.retrieve(out.data(),n);require(got>0&&got<=n&&rendered.size()/channels+got<=outCount+512,"Gap drain bank exceeded");for(std::size_t f=0;f<got;++f)for(unsigned ch=0;ch<channels;++ch){require(std::isfinite(out[ch][f]),"Nonfinite gap output");rendered.push_back(out[ch][f]);}}};
   for(std::size_t at=0;at<count;at+=512){const auto n=std::min<std::size_t>(512,count-at);read(at,n);rb.process(in.data(),n,at+n==count);drain();}
   drain();require(rb.available()==-1,"Gap failed to finish");
  }
  wave(root/("gap-"+std::to_string(index)+"-source.wav"),raw,channels);wave(root/("gap-"+std::to_string(index)+"-generated.wav"),rendered,channels);
  require(rendered.size()==outCount*channels,"Gap duration mismatch; no repair");
  for(std::size_t f=0;f<outCount;++f){double w=1;
   if(f<before)w=double(f+1)/double(before+1);
   else if(f>=outCount-after)w=double(outCount-f)/double(after+1);
   for(unsigned ch=0;ch<channels;++ch){auto &destination=result[(ob-before+f)*channels+ch];const auto sample=rendered[f*channels+ch];
    if(w==1||destination==sample)destination=sample;
    else destination=float((1-w)*double(destination)+w*double(sample));
   }
  }
  gaps.push_back({{"index",index++},{"sourceBegin",sb},{"sourceEnd",se},{"outputBegin",ob},{"outputEnd",oe},{"contextBefore",before},{"contextAfter",after},{"inputFrames",count},{"outputFrames",outCount},{"unityCopy",unity}});
 }
 for(const auto &p:plan.map().points())points.push_back({{"source",p.source.frame},{"output",p.output.frame}});
 for(auto f:result)require(std::isfinite(f),"Nonfinite assembled PCM");
 wave(root/"rendered.wav",result,channels);
 Json report={{"format","sc-protected-warp-render-v1"},{"request",j},{"frames",frames},{"target",target},{"channels",channels},{"spans",spans},{"gaps",gaps},{"points",points},{"beforeFrames",policy.before},{"afterFrames",policy.after},{"haloFrames",policy.halo},{"writtenFrames",result.size()/channels},{"nativeAudio",false},{"shippingAdopted",false},{"fullQualityQualified",false}};
 std::ofstream metadata(root/"render.json");metadata<<report.dump(2)<<'\n';metadata.flush();require(bool(metadata),"Metadata write failed");std::cout<<report.dump()<<'\n';return 0;
}catch(const std::exception &e){std::cerr<<e.what()<<'\n';return 1;}}
