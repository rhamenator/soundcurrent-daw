// SPDX-License-Identifier: GPL-3.0-only
// Owned bounded candidate experiment, never a shipping processor.
#include "warp_map.hpp"
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
int main(int argc,char **argv){try {
    require(argc==3,"Expected bounded request and NEW owned output directory");
    const std::string text(argv[1]);require(text.size()<=8192,"Render probe JSON exceeds bank");
    const auto j=Json::parse(text);const auto family=j.at("family").get<std::string>(),profile=j.at("profile").get<std::string>();
    const unsigned channels=j.at("channels"),block=j.at("block");const bool finer=j.at("finer"),together=j.at("together");const int pitch=j.at("pitchMilliCents");
    require((channels==2 || channels==8) && (block==97 || block==512) && (pitch==0 || pitch==700007),"Probe settings exceed frozen matrix");
    require((family=="impulse" || family=="attack" || family=="sustain") && (profile=="constant" || profile=="uniform-map" || profile=="nonuniform-map"),"Unsupported probe signal/map");
    const std::filesystem::path root(argv[2]);require(std::filesystem::create_directory(root),"Owned render directory already exists");
    constexpr std::size_t frames=16384,target=24576;
    const std::array<std::size_t,4> events{2048,6144,10240,14336};
    const std::array<std::size_t,4> targets{3072,8192,16384,21504};
    const std::array<float,8> gains{1,-.5f,.25f,-.125f,.75f,-.375f,.1875f,-.09375f};
    std::vector<float> source(frames*channels),result;result.reserve((target+512)*channels);
    for(std::size_t f=0;f<frames;++f)for(unsigned ch=0;ch<channels;++ch){
        const auto delay=3*ch;double x=0;
        if(family=="sustain")x=.7*std::sin(2*std::numbers::pi*440*(double(f)-delay)/48000)+.2*std::sin(2*std::numbers::pi*730*(double(f)-delay)/48000);
        else for(auto event:events){
            if(family=="impulse" && f==event+delay)x+=1.5;
            if(family=="attack" && f>=event+delay && f<event+delay+1536){
                const double t=double(f-event-delay),envelope=std::min(1.,t/8)*std::exp(-t/192);
                x+=envelope*(.8*std::sin(2*std::numbers::pi*440*t/48000)+.4*std::sin(2*std::numbers::pi*880*t/48000)+.2*std::sin(2*std::numbers::pi*1320*t/48000));
            }
        }
        source[f*channels+ch]=float(x*gains[ch]);
    }
    wave(root/"source.wav",source,channels);
    std::vector<WarpMarker> markers;
    if(profile!="constant")for(unsigned i=0;i<events.size();++i){
        const auto out=profile=="uniform-map"?events[i]*3/2:targets[i];
        auto id=std::string("00000000-0000-0000-0000-00000000000")+char('1'+i);
        markers.push_back({Id(id),{Frame(events[i]),0,1},{Frame(out),0,1}});
    }
    ResourceLedger ledger(1024*1024,"Experimental normalized warp");
    WarpMap map({{0,0,1},48000,Frame(frames),Frame(frames),Frame(target),{0,0,1},{Frame(frames),0,1}},markers,ledger);
    const auto options=RB::OptionProcessOffline|RB::OptionThreadingNever|RB::OptionFormantPreserved|RB::OptionPitchHighQuality|
        (finer?RB::OptionEngineFiner:RB::OptionEngineFaster)|(together?RB::OptionChannelsTogether:RB::OptionChannelsApart);
    RB rb(48000,channels,options,double(target)/frames,std::exp2(double(pitch)/1200000.));
    rb.setDebugLevel(0);rb.setMaxProcessSize(512);rb.setExpectedInputDuration(frames);
    if(profile!="constant")rb.setKeyFrameMap(map.vendorInteriorFrames());
    std::vector<float> input(std::size_t(channels)*512),output(std::size_t(channels)*512);
    std::vector<const float *> in(channels);std::vector<float *> out(channels);
    for(unsigned ch=0;ch<channels;++ch){in[ch]=input.data()+std::size_t(ch)*512;out[ch]=output.data()+std::size_t(ch)*512;}
    auto read=[&](std::size_t at,std::size_t n){for(unsigned ch=0;ch<channels;++ch)for(std::size_t f=0;f<n;++f)input[std::size_t(ch)*512+f]=source[(at+f)*channels+ch];};
    for(std::size_t at=0;at<frames;at+=block){const auto n=std::min<std::size_t>(block,frames-at);read(at,n);rb.study(in.data(),n,at+n==frames);}
    auto drain=[&]{while(rb.available()>0){const auto n=std::size_t(std::min(512,rb.available()));const auto got=rb.retrieve(out.data(),n);
        require(got>0 && got<=n && result.size()/channels+got<=target+512,"Unbounded probe drain");
        for(std::size_t f=0;f<got;++f)for(unsigned ch=0;ch<channels;++ch){require(std::isfinite(out[ch][f]),"Nonfinite probe output");result.push_back(out[ch][f]);}}};
    for(std::size_t at=0;at<frames;at+=block){const auto n=std::min<std::size_t>(block,frames-at);read(at,n);rb.process(in.data(),n,at+n==frames);drain();}
    drain();require(rb.available()==-1,"Probe did not finish draining");
    wave(root/"rendered.wav",result,channels);
    Json points=Json::array();for(const auto &p:map.points())points.push_back({{"source",p.source.frame},{"output",p.output.frame},{"id",p.id?Json(p.id->str()):Json(nullptr)}});
    const auto peak=std::max_element(result.begin(),result.end(),[](float a,float b){return std::abs(a)<std::abs(b);});
    Json report={{"format","sc-warp-candidate-render-v1"},{"request",j},{"frames",frames},{"target",target},{"writtenFrames",result.size()/channels},
                 {"exactDrain",result.size()/channels==target},{"peakLinear",peak==result.end()?0.:std::abs(double(*peak))},{"points",points},{"mapChargeBytes",map.chargedBytes()},
                 {"nativeAudio",false},{"shippingProcessorChanged",false},{"QStretchPassed",false}};
    std::ofstream metadata(root/"render.json");metadata<<report.dump(2)<<'\n';metadata.flush();require(bool(metadata),"Cannot retain probe metadata");
    std::cout<<report.dump()<<'\n';return 0;
}catch(const std::exception &e){std::cerr<<e.what()<<'\n';return 1;}}
