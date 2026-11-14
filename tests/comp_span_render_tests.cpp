// SPDX-License-Identifier: GPL-3.0-only
#include <soundcurrent/comp_span_render.hpp>
#include <soundcurrent/positioned_resampling.hpp>
#include <nlohmann/json.hpp>
#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <complex>
#include <fstream>
#include <filesystem>
#include <iostream>
#include <numbers>
#include <type_traits>
using namespace soundcurrent::daw;
namespace {
using Json=nlohmann::json;unsigned checks=0;
void check(bool v,const char *m){++checks;if(!v)throw std::runtime_error(m);}
template<class F>void refuses(F f){try{f();}catch(const ProjectError &){++checks;return;}throw std::runtime_error("Invalid span accepted");}
Session fixture(unsigned rate,unsigned ratio,int pitch,bool formant){
    auto s=makeOneTrackSession("Comp span","Audio",48000);auto &t=s.tracks[0];t.layout={LayoutKind::Stereo,2};
    Asset raw;raw.layout=t.layout;raw.sampleRate=rate;raw.frames=98304;raw.relativePath="media/raw.wav";raw.sha256=std::string(64,'a');
    TakeLane lane;Clip take;take.assetId=raw.id;take.lengthFrames=SourceFrameMap(rate,48000).projectFramesForSource(raw.frames);lane.captureEndFrame=take.lengthFrames;lane.clips={take};t.takeLanes={lane};
    ClipStretchAnchor a;a.sourceAssetId=raw.id;a.sourceSha256=raw.sha256;a.sourceOrigin={32768,1,7};a.sourceFrames=16384;a.settings=canonicalStretchSettings({ratio,2,pitch,formant});a.processor=std::string(stretchProcessorFor(a.settings));a.renderKey=std::string(64,'b');
    Asset base=raw;base.id=Id::generate();base.relativePath="media/base.wav";base.frames=16384*ratio/2;base.sha256=std::string(64,'c');s.assets={raw,base};
    Clip c;c.assetId=base.id;c.startFrame=100;c.stretch=a;c.takeOrigin=ClipTakeOrigin{t.id,lane.id,take.id};c.playbackRate={3,2};c.sourceTiming={1,3};
    c.lengthFrames=clipSourceMap(c,rate,48000).projectFramesForSource(base.frames);while(clipSourceMap(c,rate,48000).at(c.lengthFrames-1).frame>=base.frames)--c.lengthFrames;
    t.clips={c};validate(s);return s;
}
CompHandleEnvelope envelope(const Session &s,ResourceLedger ledger){const auto &t=s.tracks[0];const auto &c=t.clips[0];return prepareCompHandleEnvelope(s,t.id,c.id,c.startFrame-26,c.startFrame+c.lengthFrames+26,{7,"owned-span"},ledger);}
CompSpanRead reader(const std::vector<float> &raw,unsigned channels){return [&raw,channels](Frame at,std::span<float> out){check(at>=0 && std::size_t(at)*channels<=raw.size() && out.size()<=raw.size()-std::size_t(at)*channels,"Span read exceeds actual PCM");std::copy_n(raw.begin()+at*channels,out.size(),out.begin());};}
struct PitchObservation {double fundamental=0,globalPeak=0,fundamentalRelativeAmplitude=0;};
PitchObservation frequency(const std::vector<float> &v,unsigned ch,unsigned rate,double wanted){
    const auto first=v.size()/2/4,last=3*(v.size()/2)/4,count=last-first;std::size_t n=1;while(n<count*8)n*=2;
    std::vector<std::complex<double>> spectrum(n);
    for(std::size_t i=0;i<count;++i)spectrum[i]=double(v[(first+i)*2+ch])*(.5-.5*std::cos(2*std::numbers::pi*double(i)/double(count-1)));
    for(std::size_t i=1,j=0;i<n;++i){auto bit=n>>1;for(;j&bit;bit>>=1)j^=bit;j^=bit;if(i<j)std::swap(spectrum[i],spectrum[j]);}
    for(std::size_t size=2;size<=n;size*=2){const auto unit=std::polar(1.,-2*std::numbers::pi/double(size));
        for(std::size_t at=0;at<n;at+=size){std::complex<double> weight=1;for(std::size_t k=0;k<size/2;++k){const auto even=spectrum[at+k],odd=spectrum[at+k+size/2]*weight;spectrum[at+k]=even+odd;spectrum[at+k+size/2]=even-odd;weight*=unit;}}}
    const auto peak=[&](double low,double high){const auto begin=std::max<std::size_t>(1,std::size_t(std::ceil(low*double(n)/rate))),end=std::min(n/2,std::size_t(std::floor(high*double(n)/rate)));check(begin<end,"Spectral pitch search empty");
        auto best=begin;for(auto i=begin+1;i<=end;++i)if(std::norm(spectrum[i])>std::norm(spectrum[best]))best=i;return best;};
    const auto fundamental=peak(wanted*.9,wanted*1.1),global=peak(wanted*.25,std::min(wanted*4,double(rate)*.49));
    const auto relative=std::abs(spectrum[fundamental])/std::abs(spectrum[global]);
    check(std::isfinite(relative) && relative>.01,"Requested fundamental absent from rendered spectrum");
    return {double(fundamental)*rate/double(n),double(global)*rate/double(n),relative};
}
void pcm(const std::filesystem::path &p,const std::vector<float> &v){std::ofstream f(p,std::ios::binary);f.write(reinterpret_cast<const char *>(v.data()),std::streamsize(v.size()*4));check(bool(f),"Span PCM write failed");}
}
int main(int argc,char **argv){try{
    static_assert(!std::is_copy_constructible_v<CompSpanPlan>);static_assert(!std::is_move_assignable_v<CompSpanPlan>);
    check(argc==2,"Supply new output root");const std::string argument=argv[1];const std::filesystem::path root(std::u8string(argument.begin(),argument.end()));check(std::filesystem::create_directory(root),"Span output root already exists");
    ResourceLedger ledger(32*1024*1024,"Comp span slabs");Json rows=Json::array();unsigned renders=0;double maxError=0;
    for(unsigned rate:{44100u,48000u,96000u}){std::vector<float> raw(98304*2);for(std::size_t f=0;f<raw.size()/2;++f)for(unsigned ch=0;ch<2;++ch)raw[f*2+ch]=float(2.5*std::sin(2*std::numbers::pi*(ch?1703.:997.)*double(f)/rate));pcm(root/("source-"+std::to_string(rate)+".pcm"),raw);
        for(unsigned ratio:{1u,3u})for(int pitch:{-1200000,0,700007})for(bool formant:{false,true}){
            auto s=fixture(rate,ratio,pitch,formant);const auto original=s;
            {
                auto e=envelope(s,ledger);auto p=prepareCompSpan(e,ledger);check(scaleSourcePosition(p.grid().at(p.processingFrames()),1,1)==scaleSourcePosition(e.data().map.back().raw,1,1) && scaleSourcePosition(p.grid().at(0),1,1)==scaleSourcePosition(e.data().map.front().raw,1,1),"Exact processing endpoints differ");
                check(p.settings().pitchMilliCents==pitch && p.settings().formantPreserved==formant && e.data().expected.playbackRate==ClipPlaybackRate{3,2},"Span reinterpreted independent pitch/playback");
                std::vector<float> output;auto result=renderCompSpan(p,reader(raw,2),[&](auto slab){output.insert(output.end(),slab.begin(),slab.end());},ledger,[]{});++renders;
                check(result.writtenFrames==p.outputFrames() && output.size()==std::size_t(p.outputFrames())*2 && result.floatPeak>0 && !result.copied,"Actual span failed duration/finite contract");
                check(result.backendPitchFactor==std::exp2(double(pitch)/1200000.)*p.grid().denominator()/p.grid().numerator(),"Fractional-grid pitch compensation differs");
                Json hz=Json::array();for(unsigned ch=0;ch<2;++ch){const auto wanted=(ch?1703.:997.)*std::exp2(double(pitch)/1200000.);const auto observed=frequency(output,ch,rate,wanted);maxError=std::max(maxError,std::abs(observed.fundamental/wanted-1));hz.push_back({{"wanted",wanted},{"measured",observed.fundamental},{"globalPeakHz",observed.globalPeak},{"fundamentalRelativeAmplitude",observed.fundamentalRelativeAmplitude}});}
                const auto name="render-"+std::to_string(renders)+".pcm";pcm(root/name,output);
                rows.push_back({{"rate",rate},{"timeNumerator",ratio},{"timeDenominator",2},{"pitchMilliCents",pitch},{"formantPreserved",formant},{"processingFrames",p.processingFrames()},{"outputFrames",p.outputFrames()},{"rawOrigin",{p.grid().at(0).frame,p.grid().at(0).fraction,p.grid().at(0).denominator}},{"rawSpan",{p.rawSpan().frame,p.rawSpan().fraction,p.rawSpan().denominator}},{"processingStep",{p.grid().numerator(),p.grid().denominator()}},{"backendPitchFactor",result.backendPitchFactor},{"peak",result.floatPeak},{"frequency",hz},{"pcm",name}});
                if(renders==1){const auto baseline=ledger.usage().reservedBytes;unsigned writes=0;
                    refuses([&]{renderCompSpan(p,[](Frame,std::span<float>){},[&](auto){++writes;},ledger,[]{});});check(!writes && ledger.usage().reservedBytes==baseline,"Incomplete input wrote or leaked");
                    refuses([&]{renderCompSpan(p,reader(raw,2),[&](auto){++writes;},ledger,[]{throw ProjectError(ErrorCode::Canceled,"Canceled");});});check(!writes,"Canceled span wrote output");
                    refuses([&]{renderCompSpan(p,reader(raw,2),[](auto){throw ProjectError(ErrorCode::Io,"Write failed");},ledger,[]{});});check(ledger.usage().reservedBytes==baseline,"Failed writer leaked slabs");
                    ResourceLedger tiny(1,"Tiny span");refuses([&]{renderCompSpan(p,reader(raw,2),[](auto){},tiny,[]{});});check(!tiny.usage().reservedBytes,"Tiny span leaked");
                }
                check(s==original,"Span render changed project/source metadata");
            }
            check(!ledger.usage().reservedBytes,"Span owners retained resource credit");
        }
    }
    // Nonuniform maps are not flattened into the new uniform renderer.
    auto mapped=fixture(48000,3,0,true);auto &anchor=*mapped.tracks[0].clips[0].stretch;WarpSettings w;w.protection={8,16,2,8};w.markers.push_back({Id::generate(),{8192,0,1},{10000,0,1}});anchor.warp=w;anchor.processor=std::string(stretchProcessorFor(anchor.settings,anchor.context,anchor.warp));validate(mapped);
    {auto e=envelope(mapped,ledger);refuses([&]{prepareCompSpan(e,ledger);});}
    // Exact-copy path retains the prepared positioned kernel for fractional origin.
    auto unity=fixture(48000,2,0,false);{auto e=envelope(unity,ledger);auto p=prepareCompSpan(e,ledger);std::vector<float> raw(98304*2,2.5f),out;
        auto r=renderCompSpan(p,reader(raw,2),[&](auto v){out.insert(out.end(),v.begin(),v.end());},ledger,[]{});check(r.copied && r.writtenFrames==p.outputFrames(),"Unity span unnecessarily invoked R3");}
    auto shortSource=fixture(48000,3,0,false);shortSource.tracks[0].clips[0].stretch->sourceFrames=512;shortSource.assets[1].frames=768;
    auto &shortClip=shortSource.tracks[0].clips[0];shortClip.lengthFrames=clipSourceMap(shortClip,48000,48000).projectFramesForSource(768);
    while(clipSourceMap(shortClip,48000,48000).at(shortClip.lengthFrames-1).frame>=768)--shortClip.lengthFrames;
    validate(shortSource);
    {auto e=envelope(shortSource,ledger);auto p=prepareCompSpan(e,ledger);unsigned writes=0;
        refuses([&]{renderCompSpan(p,[](Frame,std::span<float>){},[&](auto){++writes;},ledger,[]{});});check(!writes,"Short R3 span emitted padded/partial audio");}
    check(!ledger.usage().reservedBytes,"Refused/copy spans retained credit");

    const Json report{{"format","sc-comp-span-render-tests-v1"},{"checks",checks},{"actualR3Renders",renders},{"rows",rows},{"maxRelativeFrequencyError",maxError},{"frequencyGuard",.01},{"formantsQualified",false},{"nonuniformProducerImplemented",false},{"ownedWorkerCompletion",false},{"qualityAdmission",false},{"nativeEndpoints",false}};
    std::ofstream f(root/"report.json");f<<report.dump(2)<<'\n';check(bool(f),"Span report failed");check(maxError<.01,"Fundamental spectral contract exceeds 1 percent experiment guard");std::cout<<Json{{"checks",checks},{"actualR3Renders",renders},{"maxRelativeFrequencyError",maxError},{"qualityAdmission",false}}.dump()<<'\n';return 0;
}catch(const std::exception &e){std::cerr<<e.what()<<'\n';return 1;}}
