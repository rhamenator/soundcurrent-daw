// SPDX-License-Identifier: GPL-3.0-only
// Metadata ancestry fixtures are declarations, not verified/adopted media.
// Actual candidate audio is rendered from owned synthetic original PCM below.
#include <soundcurrent/comp_uniform_context.hpp>
#include <soundcurrent/positioned_resampling.hpp>
#include <rubberband/RubberBandStretcher.h>
#include <soundcurrent/comp_regeneration_source.hpp>
#include <soundcurrent/comp_playback_state.hpp>
#include <soundcurrent/clip_timing.hpp>
#include <nlohmann/json.hpp>
#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <iostream>
#include <filesystem>
#include <fstream>
#include <numbers>
using namespace soundcurrent::daw;
using Json=nlohmann::json;
namespace {
std::uint64_t checks=0,compared=0;
const CompHandlePlanContext context{91,"uniform-original/é"};
void check(bool v,const char *m){++checks;if(!v)throw std::runtime_error(m);}
template<class F>void refuses(F f){bool caught=false;try{f();}catch(const ProjectError &){caught=true;}check(caught,"Expected clocked comp refusal");}
SourcePosition product(SourcePosition a,SourcePosition b){const auto r=sourceIntervalRatio(b,{1,0,1});return scaleSourcePosition(a,r.numerator,r.denominator);}
Json pos(SourcePosition p){return Json::array({p.frame,p.fraction,p.denominator});}
void save(const std::filesystem::path &path,const std::vector<float> &v){std::ofstream f(path,std::ios::binary);for(float sample:v){const auto bits=std::bit_cast<std::uint32_t>(sample);for(unsigned i=0;i<4;++i)f.put(char((bits>>(8*i))&255));}f.flush();check(bool(f),"Candidate evidence write failed");}
Session fixture(unsigned rawRate){
 auto s=makeOneTrackSession("Uniform context comp","Audio",48000);auto &t=s.tracks[0];t.layout={LayoutKind::Mono,1};
 Asset raw;raw.layout=t.layout;raw.sampleRate=rawRate;raw.frames=196608;raw.relativePath="media/raw.wav";raw.sha256=std::string(64,'a');
 TakeLane lane;Clip take;take.assetId=raw.id;take.lengthFrames=Frame(196608)*48000/rawRate;lane.captureEndFrame=take.lengthFrames;lane.clips={take};t.takeLanes={lane};
 ClipStretchAnchor a;a.sourceAssetId=raw.id;a.sourceSha256=raw.sha256;a.sourceOrigin={32768,1,7};a.sourceFrames=65536;a.settings=canonicalStretchSettings({3,2,0,true});
 a.processor=std::string(stretchProcessorFor(a.settings,a.context,a.warp));a.renderKey=std::string(64,'b');
 Asset base=raw;base.id=Id::generate();base.relativePath="media/base.wav";base.frames=98304;base.sha256=std::string(64,'c');s.assets={raw,base};
 Clip c;c.assetId=base.id;c.startFrame=100000;c.lengthFrames=Frame(98304)*48000/rawRate;c.stretch=a;c.takeOrigin=ClipTakeOrigin{t.id,lane.id,take.id};t.clips={c};validate(s);return s;
}
CompHandleEnvelope envelope(const Session &s,Frame prefix,Frame suffix,ResourceLedger ledger){const auto &t=s.tracks[0];const auto &c=t.clips[0];return prepareCompHandleEnvelope(s,t.id,c.id,c.startFrame-prefix,c.startFrame+c.lengthFrames+suffix,context,ledger);}
Session layer(Session s,bool post,ResourceLedger ledger){
 auto e=envelope(s,0,0,ledger);const auto &h=e.data();CompPlaybackSourceData d;d.original=d.playback=h.expected;d.raw=h.raw;d.base=h.base;d.nominal=h.nominalAsset;d.rendered=h.base;d.rendered.id=Id::generate();d.rendered.sha256=std::string(64,'f');d.rendered.nativeEvidence.clear();d.rawMap=h.map;d.prefixFrames=h.prefixFrames;d.suffixFrames=h.suffixFrames;d.projectRate=s.sampleRate;
 check(!h.prefixFrames && !h.suffixFrames,"Fixture unexpectedly extends parent");d.domain=post?CompProcessingDomain::ProjectAfterSrc:CompProcessingDomain::PhysicalArtifact;
 if(post){const auto clock=clipSourceMap(h.expected,h.base.sampleRate,s.sampleRate);d.expandedOrigin=clock.at(0);d.expandedNumerator=clock.numerator();d.expandedDenominator=clock.denominator();d.rendered.sampleRate=s.sampleRate;d.rendered.frames=h.expected.lengthFrames;d.playback.sourceFrame=0;d.playback.sourceTiming={};d.playback.playbackRate={};}
 const auto selection=Id::generate();d.rendered.relativePath="media/comp-results/"+selection.str()+"/"+d.rendered.id.str()+".wav";d.playback.assetId=d.rendered.id;d.playback.stretch.reset();d.playback.compHandles.reset();
 CompAssemblyReference assembly{Id::generate(),selection,std::string(64,'e'),{d.rendered.id,{}}};s.compPairSelections.push_back({selection,std::string(64,'d')});s.assets.push_back(d.rendered);s.compPlaybackSources.push_back({assembly,0,std::string(64,'d'),d});s.tracks[0].clips[0]=d.playback;validate(s);return s;
}
std::vector<float> reference(const CompUniformContextPlan &p,const std::vector<float> &raw){
 // Independent full-context accumulator; production must use bounded streaming.
 using RB=RubberBand::RubberBandStretcher;const auto step=scaleSourcePosition(p.intrinsicExtent(),1,std::uint64_t(p.renderFrames()));
 const double pitch=std::exp2(double(p.settings().pitchMilliCents)/1200000.)*(double(step.frame)+double(step.fraction)/double(step.denominator))*p.input().denominator()/p.input().numerator();
 RB rb(p.source().sampleRate,1,RB::OptionProcessOffline|RB::OptionEngineFiner|RB::OptionThreadingNever|RB::OptionChannelsTogether|(p.settings().formantPreserved?RB::OptionFormantPreserved:RB::OptionFormantShifted),double(p.renderFrames())/p.inputFrames(),pitch);
 rb.setDebugLevel(0);rb.setMaxProcessSize(512);rb.setExpectedInputDuration(std::size_t(p.inputFrames()));
 PreparedPositionedResampling inKernel(p.input(),1);std::vector<float> input(std::size_t(p.inputFrames())),full;
 for(Frame i=0;i<p.inputFrames();i+=512){auto n=std::uint32_t(std::min<Frame>(512,p.inputFrames()-i));inKernel.process(p.input(),i,p.source().frames,0,raw,{input.data()+i,n});}
 const float *in=nullptr;std::array<float,512> slab;float *out=slab.data();
 for(Frame i=0;i<p.inputFrames();i+=512){auto n=std::size_t(std::min<Frame>(512,p.inputFrames()-i));in=input.data()+i;rb.study(&in,n,i+Frame(n)==p.inputFrames());}
 const auto drain=[&]{while(rb.available()>0){auto n=rb.retrieve(&out,std::min<std::size_t>(512,rb.available()));check(n>0,"Reference stalled");full.insert(full.end(),slab.begin(),slab.begin()+n);}};
 for(Frame i=0;i<p.inputFrames();i+=512){auto n=std::size_t(std::min<Frame>(512,p.inputFrames()-i));in=input.data()+i;rb.process(&in,n,i+Frame(n)==p.inputFrames());drain();}
 drain();check(rb.available()==-1 && full.size()==std::size_t(p.renderFrames()),"Full-context reference extent differs");return full;
}
double frequency(const std::vector<float> &v,unsigned rate){std::vector<double> crossings;for(std::size_t i=v.size()/4+1;i<v.size()*3/4;++i)if(v[i-1]<=0 && v[i]>0)crossings.push_back(double(i-1)-v[i-1]/double(v[i]-v[i-1]));check(crossings.size()>4,"Insufficient pitch observations");return double(crossings.size()-1)*rate/(crossings.back()-crossings.front());}
}
int main(int argc,char **argv){try{
 check(argc==2,"Supply new evidence directory");const std::string argument=argv[1];const std::filesystem::path root(std::u8string(argument.begin(),argument.end()));check(std::filesystem::create_directory(root),"Evidence directory exists");ResourceLedger ledger(64*1024*1024);Json rows=Json::array();bool short4054=false;
 for(unsigned rate:{44100u,48000u,96000u}){
  std::vector<float> raw(196608);for(std::size_t i=0;i<raw.size();++i)raw[i]=float(2.5*std::sin(2*std::numbers::pi*211.*double(i)/rate));
  auto original=fixture(rate);
  for(unsigned layers:{1u,2u}){
   auto s=original;auto &c=s.tracks[0].clips[0];c.sourceFrame=28000;c.sourceTiming={1,7};c.lengthFrames=3990;c.playbackRate={3,2};validate(s);
   const auto originalOrigin=c.stretch->sourceOrigin;auto expectedRaw=addSourcePosition(originalOrigin,scaleSourcePosition({28000,1,7},2,3));
   auto currentStep=scaleSourcePosition({1,0,1},rate,48000);SourcePosition baked{1,1,2};s=layer(s,true,ledger);
   if(layers==2){auto &x=s.tracks[0].clips[0];x.sourceFrame=13;x.sourceTiming={2,11};x.lengthFrames=3000;x.playbackRate={2,3};validate(s);expectedRaw=addSourcePosition(expectedRaw,product({13,2,11},currentStep));currentStep=scaleSourcePosition(currentStep,2,3);baked={1,0,1};s=layer(s,true,ledger);}
   s=layer(s,false,ledger);const auto snapshot=s;
   {
    auto e=envelope(s,32,32,ledger);auto p=prepareUniformContextCompSpan(e,ledger,[]{});
    check(p.bakedPitchScale()==baked && p.outputSampleRate()==48000,"Baked pitch or current sample rate lost");
    check(p.inputFrames()==65536 && p.renderFrames()==98304 && p.input().at(0)==originalOrigin,"Short crop changed original analysis domain");
    const auto candidateRaw=subtractSourcePosition(expectedRaw,scaleSourcePosition(currentStep,e.data().prefixFrames,1));
    check(p.samples().at(0)==scaleSourcePosition(subtractSourcePosition(candidateRaw,originalOrigin),3,2),"Fractional crop phase changed");
    check(scaleSourcePosition({1,0,1},p.samples().numerator(),p.samples().denominator())==scaleSourcePosition(currentStep,3,2),"Exact sampling step changed");
    auto reader=[&](Frame at,std::span<float> v){check(at>=0 && v.size()<=512 && std::size_t(at)+v.size()<=raw.size(),"Raw read exceeds bounded source");std::copy_n(raw.begin()+at,v.size(),v.begin());};
    if(rate==48000 && layers==1){auto legacy=prepareCompSpan(e,ledger);check(legacy.processingFrames()==4054,"Regression no longer exercises 4054 frames");unsigned writes=0;refuses([&]{renderCompSpan(legacy,reader,[&](auto){++writes;},ledger,[]{});});check(!writes,"Unsupported legacy render wrote candidate");short4054=true;}
    const auto baseline=ledger.usage().reservedBytes;std::vector<float> actual;auto report=renderUniformContextCompSpan(p,reader,[&](auto v){check(v.size()<=512,"Candidate write exceeded slab");actual.insert(actual.end(),v.begin(),v.end());},ledger,[]{});
    check(report.writtenFrames==p.outputFrames() && actual.size()==std::size_t(p.outputFrames()) && report.generatedFrames==98304 && report.floatPeak>1,"Short uniform candidate duration/headroom lost");check(ledger.usage().reservedBytes==baseline,"Streaming context credits leaked");
    const auto full=reference(p,raw);PreparedPositionedResampling k(p.samples(),1);std::vector<float> expected(actual.size());
    for(Frame at=0;at<p.outputFrames();at+=257){const auto n=std::uint32_t(std::min<Frame>(257,p.outputFrames()-at));k.process(p.samples(),at,p.renderFrames(),0,full,{expected.data()+at,n});}
    check(actual==expected,"Bounded candidate differs from independent full-context extraction");compared+=actual.size();
    const auto wanted=211.*(double(baked.frame)+double(baked.fraction)/double(baked.denominator)),hz=frequency(actual,48000);check(std::abs(hz/wanted-1)<.02,"Short uniform candidate lost baked pitch");
    if(rate==48000 && layers==1){unsigned writes=0;
     refuses([&]{renderUniformContextCompSpan(p,[](Frame,std::span<float>){},[&](auto){++writes;},ledger,[]{});});check(!writes,"Incomplete source published output");
     refuses([&]{renderUniformContextCompSpan(p,reader,[&](auto){++writes;throw ProjectError(ErrorCode::Io,"Writer refusal");},ledger,[]{});});check(writes==1,"Writer refusal did not reach crop");
     writes=0;refuses([&]{renderUniformContextCompSpan(p,reader,[&](auto){++writes;},ledger,[&]{if(writes)throw ProjectError(ErrorCode::Canceled,"Late cancellation");});});check(writes==1,"Late cancellation did not interrupt crop");
     ResourceLedger tiny(1);refuses([&]{renderUniformContextCompSpan(p,reader,[](auto){},tiny,[]{});});check(!tiny.usage().reservedBytes && ledger.usage().reservedBytes==baseline,"Failure retained slab credits");
     refuses([&]{(void)prepareUniformContextCompSpan(e,ledger,[]{throw ProjectError(ErrorCode::Canceled,"Preparation canceled");});});
    }
    save(root/("candidate-"+std::to_string(rate)+"-"+std::to_string(layers)+".f32le"),actual);
    rows.push_back({{"rate",rate},{"layers",layers},{"candidateFrames",p.outputFrames()},{"inputFrames",p.inputFrames()},{"renderFrames",p.renderFrames()},{"bakedPitch",pos(baked)},{"samplingOrigin",pos(p.samples().at(0))},{"maximumBufferedFrames",report.maximumBufferedFrames},{"peak",report.floatPeak},{"wantedHz",wanted},{"observedHz",hz}});check(s==snapshot,"Preparation/render changed session");
   }
   check(!ledger.usage().reservedBytes,"Context owners leaked credits");
  }
 }
 // Stereo, explicit pitch and formant settings must survive the new path too.
 auto stereo=fixture(48000);auto &track=stereo.tracks[0];track.layout={LayoutKind::Stereo,2};for(auto &asset:stereo.assets)asset.layout=track.layout;
 auto &sc=track.clips[0];sc.stretch->settings.pitchMilliCents=1200000;sc.stretch->settings.formantPreserved=false;sc.stretch->processor=std::string(stretchProcessorFor(sc.stretch->settings));
 sc.sourceFrame=28000;sc.sourceTiming={1,7};sc.lengthFrames=3990;sc.playbackRate={3,2};validate(stereo);stereo=layer(stereo,true,ledger);
 Json stereoEvidence;
 {auto e=envelope(stereo,32,32,ledger);auto p=prepareUniformContextCompSpan(e,ledger,[]{});std::vector<float> out;
  auto result=renderUniformContextCompSpan(p,[](Frame at,std::span<float> v){check(at>=0 && v.size()<=1024 && v.size()%2==0 && at+Frame(v.size()/2)<=196608,"Stereo read bounds differ");for(std::size_t i=0;i<v.size()/2;++i)for(unsigned ch=0;ch<2;++ch)v[i*2+ch]=float(2.5*std::sin(2*std::numbers::pi*(ch?317.:211.)*double(at+Frame(i))/48000));},[&](auto v){check(v.size()<=1024,"Stereo write exceeded slab");out.insert(out.end(),v.begin(),v.end());},ledger,[]{});
  check(result.floatPeak>1 && out.size()==std::size_t(p.outputFrames())*2 && p.settings().pitchMilliCents==1200000 && !p.settings().formantPreserved,"Stereo pitch/settings or duration lost");Json measured=Json::array();
  for(unsigned ch=0;ch<2;++ch){std::vector<float> mono;for(std::size_t i=ch;i<out.size();i+=2)mono.push_back(out[i]);const double wanted=(ch?317.:211.)*3.;const auto hz=frequency(mono,48000);check(std::abs(hz/wanted-1)<.02,"Stereo channels or pitch changed");measured.push_back({{"channel",ch},{"wantedHz",wanted},{"observedHz",hz}});}
  save(root/"stereo-pitched.f32le",out);stereoEvidence={{"frames",p.outputFrames()},{"peak",result.floatPeak},{"frequency",measured},{"maximumBufferedFrames",result.maximumBufferedFrames}};
 }
 check(!ledger.usage().reservedBytes,"Stereo context leaked credits");
 check(short4054,"Missing short-edit regression");
 auto edge=fixture(48000);edge.tracks[0].clips[0].stretch->sourceOrigin={};edge.tracks[0].clips[0].sourceFrame=0;edge.tracks[0].clips[0].lengthFrames=3000;validate(edge);
 Json edges=Json::array();
 for(bool right:{false,true}){
  if(right){edge.tracks[0].clips[0].stretch->sourceOrigin={131072,0,1};edge.tracks[0].clips[0].sourceFrame=95304;validate(edge);}
  auto edgeLayer=layer(edge,true,ledger);auto e=envelope(edgeLayer,0,0,ledger);auto p=prepareUniformContextCompSpan(e,ledger,[]{});
  if(!right)check(p.input().at(0)==SourcePosition{} && p.samples().at(0)==SourcePosition{},"Source-zero context moved origin");
  Frame reads=0,writes=0;auto r=renderUniformContextCompSpan(p,[&](Frame at,std::span<float> v){check(at>=0 && at+Frame(v.size())<=196608 && v.size()<=512,"Edge read escaped raw asset");reads+=Frame(v.size());for(std::size_t i=0;i<v.size();++i)v[i]=float(2.5*std::sin(2*std::numbers::pi*211.*double(at+Frame(i))/48000));},[&](auto v){writes+=Frame(v.size());},ledger,[]{});
  check(writes==p.outputFrames() && r.writtenFrames==3000 && reads>0,"Source-edge output failed");edges.push_back({{"rightEdge",right},{"outputFrames",writes},{"readSamples",reads}});
  ResourceLedger tinyBudget(1);refuses([&]{(void)prepareUniformContextCompSpan(e,tinyBudget,[]{});});check(!tinyBudget.usage().reservedBytes,"Refused preparation leaked credits");
 }
 auto tiny=fixture(48000);auto &clip=tiny.tracks[0].clips[0];clip.stretch->sourceFrames=512;tiny.assets[1].frames=768;clip.lengthFrames=768;validate(tiny);
 {auto e=envelope(tiny,0,0,ledger);auto p=prepareUniformContextCompSpan(e,ledger,[]{});unsigned writes=0;refuses([&]{renderUniformContextCompSpan(p,[](Frame,std::span<float> v){std::fill(v.begin(),v.end(),1.f);},[&](auto){++writes;},ledger,[]{});});check(!writes,"Short full original context was silently repaired");}
 check(!ledger.usage().reservedBytes,"Final credits leaked");Json result={{"checks",checks},{"rows",rows},{"stereo",stereoEvidence},{"edges",edges},{"comparedCandidateSamples",compared},{"short4054Rendered",short4054},{"allCreditsReleased",true},{"ownedWorkerIntegrated",false},{"qualityQualified",false}};std::ofstream f(root/"report.json");f<<result.dump(2)<<'\n';check(bool(f),"Evidence report write failed");std::cout<<result.dump()<<'\n';return 0;
}catch(const std::exception &e){std::cerr<<e.what()<<'\n';return 1;}}
