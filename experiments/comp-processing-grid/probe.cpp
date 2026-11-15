// SPDX-License-Identifier: GPL-3.0-only
// Bounded software feasibility experiment; no project mutation or endpoints.
#include <soundcurrent/positioned_resampling.hpp>
#include <soundcurrent/stretch.hpp>
#include <rubberband/RubberBandStretcher.h>
#include <nlohmann/json.hpp>
#include <array>
#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <numbers>
#include <vector>
using namespace soundcurrent::daw;
using Json=nlohmann::json;
using RB=RubberBand::RubberBandStretcher;
namespace {
void require(bool v,const char *m){if(!v)throw std::runtime_error(m);}
void pcm(const std::filesystem::path &path,const std::vector<float> &v){std::ofstream out(path,std::ios::binary);out.write(reinterpret_cast<const char *>(v.data()),std::streamsize(v.size()*sizeof(float)));require(bool(out),"PCM write failed");}
std::vector<float> prepare(const std::vector<float> &source,const SourceFrameMap &map,Frame frames){
    PreparedPositionedResampling kernel(map,2);std::vector<float> v(std::size_t(frames)*2);
    for(Frame at=0;at<frames;at+=512){const auto n=std::uint32_t(std::min<Frame>(512,frames-at));const auto range=kernel.sourceRange(map,at,n,Frame(source.size()/2));
        kernel.process(map,at,Frame(source.size()/2),range.first,std::span(source).subspan(std::size_t(range.first)*2,range.frames*2),std::span(v).subspan(std::size_t(at)*2,std::size_t(n)*2));}
    return v;
}
std::vector<float> render(const std::vector<float> &input,std::size_t target,double pitch,unsigned block){
    const auto frames=input.size()/2;
    RB rb(48000,2,RB::OptionProcessOffline|RB::OptionEngineFiner|RB::OptionThreadingNever|RB::OptionChannelsTogether|RB::OptionFormantShifted,double(target)/double(frames),pitch);
    rb.setDebugLevel(0);rb.setMaxProcessSize(block);rb.setExpectedInputDuration(frames);
    require(frames>=rb.getSamplesRequired(),"Probe span below backend input requirement");
    std::array<std::array<float,512>,2> slab{},output{};
    const float *in[]{slab[0].data(),slab[1].data()};float *out[]{output[0].data(),output[1].data()};
    const auto read=[&](std::size_t at,std::size_t count){for(std::size_t f=0;f<count;++f)for(unsigned ch=0;ch<2;++ch)slab[ch][f]=input[(at+f)*2+ch];};
    for(std::size_t at=0;at<frames;at+=block){const auto n=std::min<std::size_t>(block,frames-at);read(at,n);rb.study(in,n,at+n==frames);}
    std::vector<float> v;v.reserve(target*2);
    const auto drain=[&]{while(rb.available()>0){const auto count=std::size_t(std::min(512,rb.available()));const auto got=rb.retrieve(out,count);
        require(got && got<=count && got<=target-v.size()/2,"Probe exceeded exact output duration");
        for(std::size_t f=0;f<got;++f)for(unsigned ch=0;ch<2;++ch){require(std::isfinite(output[ch][f]),"Nonfinite output");v.push_back(output[ch][f]);}}};
    for(std::size_t at=0;at<frames;at+=block){const auto n=std::min<std::size_t>(block,frames-at);read(at,n);rb.process(in,n,at+n==frames);drain();}
    drain();require(rb.available()==-1 && v.size()==target*2,"Probe did not drain exact output duration");return v;
}
double frequency(const std::vector<float> &v,unsigned ch){
    const auto frames=v.size()/2;std::vector<double> crossing;
    for(std::size_t f=frames/4+1;f<frames*3/4;++f){const double a=v[(f-1)*2+ch],b=v[f*2+ch];if(a<=0 && b>0)crossing.push_back(double(f-1)-a/(b-a));}
    require(crossing.size()>20,"Too few steady-tone crossings");
    // Linear regression over interpolated cycles averages edge/phase jitter.
    const auto mean=double(crossing.size()-1)/2;double top=0,bottom=0;
    for(std::size_t i=0;i<crossing.size();++i){const auto x=double(i)-mean;top+=x*crossing[i];bottom+=x*x;}
    return 48000/(top/bottom);
}
}
int main(int argc,char **argv){try{
    require(argc==2,"Supply a new evidence directory");const std::string pathBytes=argv[1];
    const std::filesystem::path root(std::u8string(pathBytes.begin(),pathBytes.end()));
    require(std::filesystem::create_directory(root),"Evidence directory already exists");
    std::vector<float> source(32768*2);constexpr std::array<double,2> hz{997,1703};
    for(std::size_t f=0;f<source.size()/2;++f)for(unsigned ch=0;ch<2;++ch)source[f*2+ch]=float(2.5*std::sin(2*std::numbers::pi*hz[ch]*double(f)/48000));
    pcm(root/"raw-stereo-float32.pcm",source);Json rows=Json::array();unsigned renders=0;double maximumRelativeError=0;
    for(const Frame frames:{8193,9216}){
        const auto map=SourceFrameMap::processingGrid({4096,1,7},{8192,2,3},frames);
        const auto step=double(map.numerator())/map.denominator();const auto input=prepare(source,map,frames);
        pcm(root/("prepared-"+std::to_string(frames)+".pcm"),input);
        for(const int pitch:{-1200000,0,700007})for(const bool compensated:{false,true}){
            const auto requested=std::exp2(double(pitch)/1200000.);const auto rbPitch=compensated?requested/step:requested;
            const auto v=render(input,12289,rbPitch,512);++renders;
            const auto filename="output-"+std::to_string(frames)+"-"+std::to_string(pitch)+(compensated?"-compensated.pcm":"-uncompensated.pcm");pcm(root/filename,v);
            Json observed=Json::array();double peak=0;for(const auto x:v)peak=std::max(peak,std::abs(double(x)));
            for(unsigned ch=0;ch<2;++ch){const auto actual=frequency(v,ch),wanted=hz[ch]*requested;
                if(compensated)maximumRelativeError=std::max(maximumRelativeError,std::abs(actual/wanted-1));
                observed.push_back(Json{{"channel",ch},{"rawHz",hz[ch]},{"requestedHz",wanted},{"observedHz",actual}});}
            rows.push_back(Json{{"processingFrames",frames},{"rawOrigin",{4096,1,7}},{"rawSpan",{8192,2,3}},{"step",{map.numerator(),map.denominator()}},
                {"outputFrames",12289},{"pitchMilliCents",pitch},{"pitchScale",rbPitch},{"compensated",compensated},{"frequencies",observed},{"floatPeak",peak},{"pcm",filename}});
        }
    }
    // Test the proposed full rerender against a nominal artifact: identical raw
    // mapping alone does not promise identical context-dependent R3 samples.
    const auto nominalGrid=SourceFrameMap::processingGrid({4096,1,7},{8192,0,1},8192);
    const auto expandedOrigin=subtractSourcePosition({4096,1,7},scaleSourcePosition({26,0,1},2,3));
    const auto expandedGrid=SourceFrameMap::processingGrid(expandedOrigin,{8226,2,3},8227);
    const auto nominal=render(prepare(source,nominalGrid,8192),12288,1,512);
    const auto expanded=render(prepare(source,expandedGrid,8227),12340,double(expandedGrid.denominator())/expandedGrid.numerator(),512);renders+=2;
    pcm(root/"nominal.pcm",nominal);pcm(root/"expanded.pcm",expanded);
    double difference=0;std::size_t changed=0;
    for(std::size_t i=0;i<nominal.size();++i){const auto error=std::abs(double(nominal[i])-expanded[i+26*2]);difference=std::max(difference,error);changed+=nominal[i]!=expanded[i+26*2];}
    const Json report{{"format","sc-comp-processing-grid-probe-v1"},{"renders",renders},{"rows",rows},{"compensatedMaxRelativeFrequencyError",maximumRelativeError},
        {"nominalComparison",{{"prefixOutputFrames",26},{"nominalOutputFrames",12288},{"suffixOutputFrames",26},{"changedSamples",changed},{"maxSampleDifference",difference}}},
        {"nativeEndpoints",false},{"productionCompProducer",false},{"formantsQualified",false},{"nominalPcmPreservationQualified",false}};
    std::ofstream out(root/"report.json");out<<report.dump(2)<<'\n';require(bool(out),"Report write failed");
    require(maximumRelativeError<.005,"Compensated pitch differs by more than 0.5 percent");std::cout<<report.dump()<<'\n';return 0;
}catch(const std::exception &e){std::cerr<<e.what()<<'\n';return 1;}}
