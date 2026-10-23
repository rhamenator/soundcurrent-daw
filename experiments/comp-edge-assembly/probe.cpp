// SPDX-License-Identifier: GPL-3.0-only
// Actual separate edge renders plus production bounded PCM assembly. All owned
// software observations; no project admission, media authority or native audio.
#include <soundcurrent/comp_handle_pcm.hpp>
#include <soundcurrent/positioned_resampling.hpp>
#include <soundcurrent/stretch.hpp>
#include <rubberband/RubberBandStretcher.h>
#include <nlohmann/json.hpp>
#include <algorithm>
#include <array>
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
Json position(SourcePosition p){return Json::array({p.frame,p.fraction,p.denominator});}
void pcm(const std::filesystem::path &path,const std::vector<float> &v){std::ofstream f(path,std::ios::binary);f.write(reinterpret_cast<const char *>(v.data()),std::streamsize(v.size()*sizeof(float)));require(bool(f),"PCM write failed");}
std::vector<float> prepare(const std::vector<float> &raw,const SourceFrameMap &grid,Frame frames){
    PreparedPositionedResampling kernel(grid,2);std::vector<float> result(std::size_t(frames)*2);
    for(Frame at=0;at<frames;at+=512){const auto n=std::uint32_t(std::min<Frame>(512,frames-at));const auto range=kernel.sourceRange(grid,at,n,Frame(raw.size()/2));
        kernel.process(grid,at,Frame(raw.size()/2),range.first,std::span(raw).subspan(std::size_t(range.first)*2,range.frames*2),std::span(result).subspan(std::size_t(at)*2,std::size_t(n)*2));}
    return result;
}
std::vector<float> stretch(const std::vector<float> &input,Frame target,double pitch){
    const auto frames=input.size()/2;require(target>0 && target<100000,"Probe output exceeds bound");
    RB rb(48000,2,RB::OptionProcessOffline|RB::OptionEngineFiner|RB::OptionThreadingNever|RB::OptionChannelsTogether|RB::OptionFormantShifted,double(target)/double(frames),pitch);
    rb.setDebugLevel(0);rb.setMaxProcessSize(512);rb.setExpectedInputDuration(frames);
    require(frames>=rb.getSamplesRequired(),"Actual edge context shorter than prepared window");
    std::array<std::array<float,512>,2> inputSlab{},outputSlab{};
    const float *in[]{inputSlab[0].data(),inputSlab[1].data()};float *out[]{outputSlab[0].data(),outputSlab[1].data()};
    const auto read=[&](std::size_t at,std::size_t n){for(std::size_t f=0;f<n;++f)for(unsigned ch=0;ch<2;++ch)inputSlab[ch][f]=input[(at+f)*2+ch];};
    for(std::size_t at=0;at<frames;at+=512){const auto n=std::min<std::size_t>(512,frames-at);read(at,n);rb.study(in,n,at+n==frames);}
    std::vector<float> v;v.reserve(std::size_t(target)*2);
    const auto drain=[&]{while(rb.available()>0){const auto count=std::size_t(std::min(512,rb.available()));const auto got=rb.retrieve(out,count);
        require(got && got<=count && got<=std::size_t(target)-v.size()/2,"Edge backend exceeded exact duration");
        for(std::size_t f=0;f<got;++f)for(unsigned ch=0;ch<2;++ch){require(std::isfinite(outputSlab[ch][f]),"Nonfinite edge output");v.push_back(outputSlab[ch][f]);}}};
    for(std::size_t at=0;at<frames;at+=512){const auto n=std::min<std::size_t>(512,frames-at);read(at,n);rb.process(in,n,at+n==frames);drain();}
    drain();require(rb.available()==-1 && v.size()==std::size_t(target)*2,"Edge backend duration differs");return v;
}
Json metrics(const std::optional<CompHandleJoinMetrics> &m){if(!m)return nullptr;Json j{{"stepPeak",m->stepPeak}};j["slopeDeviationPeak"]=m->slopeDeviationPeak?Json(*m->slopeDeviationPeak):Json(nullptr);return j;}
}
int main(int argc,char **argv){try{
    require(argc==2,"Supply new observation directory");const std::string bytes=argv[1];const std::filesystem::path root(std::u8string(bytes.begin(),bytes.end()));
    require(std::filesystem::create_directory(root),"Observation directory exists");
    const SourcePosition origin{32768,1,7};const Frame rawFrames=16384,edgeFrames=26;
    ResourceLedger ledger(4*1024*1024,"Edge assembly probe");Json rows=Json::array();unsigned renders=0,caseNumber=0;
    for(const unsigned signal:{0,1}){
        std::vector<float> raw(98304*2);
        for(std::size_t f=0;f<raw.size()/2;++f)for(unsigned ch=0;ch<2;++ch){const auto hz=ch?1703.:997.;double value=2.5*std::sin(2*std::numbers::pi*hz*double(f)/48000);
            if(signal){const auto at=double(f%4096);value=.3*value+(ch?-.8:1.5)*std::exp(-at/47.)*std::cos(at*.11);}
            raw[f*2+ch]=float(value);}
        pcm(root/("raw-"+std::to_string(signal)+".pcm"),raw);
        for(const unsigned numerator:{1,3})for(const int pitch:{-1200000,0,700007}){
            const auto outFrames=rawFrames*Frame(numerator)/2;
            const auto nominalInput=prepare(raw,SourceFrameMap::processingGrid(origin,{rawFrames,0,1},rawFrames),rawFrames);
            const auto requested=std::exp2(double(pitch)/1200000.);const auto current=stretch(nominalInput,outFrames,requested);++renders;
            const auto before=outFrames/4,contextFrames=before+edgeFrames+outFrames;
            const auto span=scaleSourcePosition({contextFrames,0,1},2,numerator);
            const auto processingFrames=span.frame+Frame(span.fraction!=0);
            const auto prefixOrigin=subtractSourcePosition(origin,scaleSourcePosition({before+edgeFrames,0,1},2,numerator));
            const auto prefixGrid=SourceFrameMap::processingGrid(prefixOrigin,span,processingFrames);
            const auto suffixGrid=SourceFrameMap::processingGrid(origin,span,processingFrames);
            const auto backendPitch=requested*double(prefixGrid.denominator())/prefixGrid.numerator();
            const auto prefix=stretch(prepare(raw,prefixGrid,processingFrames),contextFrames,backendPitch);
            const auto suffix=stretch(prepare(raw,suffixGrid,processingFrames),contextFrames,backendPitch);renders+=2;
            std::vector<float> candidate(std::size_t(outFrames+2*edgeFrames)*2,0);
            std::copy_n(prefix.begin()+std::size_t(before)*2,std::size_t(edgeFrames)*2,candidate.begin());
            std::copy_n(suffix.begin()+std::size_t(outFrames)*2,std::size_t(edgeFrames)*2,candidate.begin()+std::size_t(edgeFrames+outFrames)*2);
            const auto read=[&](const std::vector<float> &v){return [&v](Frame first,std::span<float> dst){require(first>=0 && std::size_t(first)*2<=v.size() && dst.size()<=v.size()-std::size_t(first)*2,"Probe PCM read exceeded extent");std::copy_n(v.begin()+std::size_t(first)*2,dst.size(),dst.begin());};};
            std::vector<float> assembled;auto result=assembleCompHandlePcm({2,127,outFrames,edgeFrames,edgeFrames},read(current),read(candidate),
                [&](std::span<const float> slab){assembled.insert(assembled.end(),slab.begin(),slab.end());},ledger,[]{});
            require(assembled.size()==candidate.size() && std::equal(current.begin(),current.end(),assembled.begin()+std::size_t(edgeFrames)*2),"Assembly changed current PCM");
            std::vector<float> other;assembleCompHandlePcm({2,511,outFrames,edgeFrames,edgeFrames},read(current),read(candidate),
                [&](std::span<const float> slab){other.insert(other.end(),slab.begin(),slab.end());},ledger,[]{});require(other==assembled,"Assembly partition changed samples");
            require(ledger.usage().reservedBytes==0,"Assembly lease retained");
            const auto name="case-"+std::to_string(caseNumber++);require(std::filesystem::create_directory(root/name),"Case directory exists");
            pcm(root/name/"nominal.pcm",current);pcm(root/name/"prefix-context.pcm",prefix);pcm(root/name/"suffix-context.pcm",suffix);pcm(root/name/"assembled.pcm",assembled);
            rows.push_back(Json{{"case",name},{"signal",signal},{"channels",2},{"rate",48000},{"pitchMilliCents",pitch},{"timeNumerator",numerator},{"timeDenominator",2},
                {"nominalRawOrigin",position(origin)},{"nominalRawFrames",rawFrames},{"nominalOutputFrames",outFrames},{"edgeFrames",edgeFrames},{"contextOutputFrames",contextFrames},
                {"prefixCropFrame",before},{"suffixCropFrame",outFrames},{"prefixRawOrigin",position(prefixOrigin)},{"suffixRawOrigin",position(origin)},{"contextRawSpan",position(span)},
                {"processingFrames",processingFrames},{"processingStep",{prefixGrid.numerator(),prefixGrid.denominator()}},{"backendPitchFactor",backendPitch},
                {"writtenFrames",result.writtenFrames},{"floatPeak",result.floatPeak},{"prefixJoin",metrics(result.prefixJoin)},{"suffixJoin",metrics(result.suffixJoin)}});
        }
    }
    const Json report{{"format","sc-comp-edge-assembly-probe-v1"},{"actualRenders",renders},{"cases",rows},{"innerPcmPreserved",true},{"qualityAdmission",false},{"nativeEndpoints",false}};
    std::ofstream out(root/"report.json");out<<report.dump(2)<<'\n';require(bool(out),"Report write failed");
    std::cout<<Json{{"renders",renders},{"cases",rows.size()},{"qualityAdmission",false}}.dump()<<'\n';return 0;
}catch(const std::exception &e){std::cerr<<e.what()<<'\n';return 1;}}
