// SPDX-License-Identifier: GPL-3.0-only
// Metadata ancestry fixtures are declarations, not verified/adopted media.
// Actual candidate audio is rendered from owned synthetic original PCM below.
#include <soundcurrent/comp_clocked_protected_span.hpp>
#include <soundcurrent/comp_regeneration_source.hpp>
#include <soundcurrent/comp_playback_state.hpp>
#include <soundcurrent/clip_timing.hpp>
#include <nlohmann/json.hpp>
#include <algorithm>
#include <bit>
#include <cmath>
#include <iostream>
#include <filesystem>
#include <fstream>
#include <numbers>
using namespace soundcurrent::daw;
using Json=nlohmann::json;
namespace {
std::uint64_t checks=0,compared=0,outside=0;
const CompHandlePlanContext context{91,"clocked-original/é"};
void check(bool v,const char *m){++checks;if(!v)throw std::runtime_error(m);}
template<class F>void refuses(F f){bool caught=false;try{f();}catch(const ProjectError &){caught=true;}check(caught,"Expected clocked comp refusal");}
SourcePosition product(SourcePosition a,SourcePosition b){const auto r=sourceIntervalRatio(b,{1,0,1});return scaleSourcePosition(a,r.numerator,r.denominator);}
Json pos(SourcePosition p){return Json::array({p.frame,p.fraction,p.denominator});}
SourcePosition pos(const Json &p){return {p[0].get<Frame>(),p[1].get<std::uint64_t>(),p[2].get<std::uint64_t>()};}
void save(const std::filesystem::path &path,const std::vector<float> &v){std::ofstream f(path,std::ios::binary);for(float sample:v){const auto bits=std::bit_cast<std::uint32_t>(sample);for(unsigned i=0;i<4;++i)f.put(char((bits>>(8*i))&255));}f.flush();check(bool(f),"Candidate evidence write failed");}
Session fixture(unsigned rawRate){
 auto s=makeOneTrackSession("Clocked protected comp","Audio",48000);auto &t=s.tracks[0];t.layout={LayoutKind::Mono,1};
 Asset raw;raw.layout=t.layout;raw.sampleRate=rawRate;raw.frames=196608;raw.relativePath="media/raw.wav";raw.sha256=std::string(64,'a');
 TakeLane lane;Clip take;take.assetId=raw.id;take.lengthFrames=Frame(196608)*48000/rawRate;lane.captureEndFrame=take.lengthFrames;lane.clips={take};t.takeLanes={lane};
 ClipStretchAnchor a;a.sourceAssetId=raw.id;a.sourceSha256=raw.sha256;a.sourceOrigin={32768,1,7};a.sourceFrames=65536;a.settings=canonicalStretchSettings({3,2,0,true});
 WarpSettings warp;warp.protection={256,512,64,2048};warp.markers={{Id("00000000-0000-0000-0000-000000000001"),{16384,1,3},{24576,1,5}},{Id("00000000-0000-0000-0000-000000000002"),{49152,2,7},{73728,2,11}}};a.warp=warp;a.processor=std::string(stretchProcessorFor(a.settings,a.context,a.warp));a.renderKey=std::string(64,'b');
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
std::vector<float> render(const PreparedClockedProtectedWarp &p,const std::vector<float> &raw,ResourceLedger ledger,const CompClockedProtectedSpanPlan *owner=nullptr){
 const auto credits=ledger.usage().reservedBytes;std::vector<float> out;
 auto reader=[&](Frame at,std::span<float> samples){check(at>=0 && samples.size()<=512 && std::size_t(at)+samples.size()<=raw.size(),"Absolute source read invalid");std::copy_n(raw.begin()+at,samples.size(),samples.begin());};auto writer=[&](std::span<const float> samples){check(samples.size()<=512,"Output block exceeded bound");out.insert(out.end(),samples.begin(),samples.end());};
 auto r=owner?renderClockedProtectedCompSpan(*owner,reader,writer,ledger,[]{}):renderClockedProtectedWarp(p,reader,writer,ledger,[]{});
 check(r.writtenFrames==p.outputFrames() && out.size()==std::size_t(p.outputFrames()),"Candidate duration differs");check(credits==ledger.usage().reservedBytes,"Render credits leaked");return out;
}
double frequency(const std::vector<float> &pcm,unsigned rate){
 std::vector<double> crossings;for(std::size_t i=pcm.size()/4+1;i<pcm.size()*3/4;++i)if(pcm[i-1]<=0 && pcm[i]>0)crossings.push_back(double(i-1)-pcm[i-1]/double(pcm[i]-pcm[i-1]));
 check(crossings.size()>5,"Insufficient audible cycles");return double(crossings.size()-1)*rate/(crossings.back()-crossings.front());
}
}
int main(int argc,char **argv){try{
 if(argc==2 && std::string_view(argv[1])=="--clock-oracle"){
  auto s=fixture(48000);const auto &a=*s.tracks[0].clips[0].stretch;ResourceLedger memory(8*1024*1024);
  ProtectedWarpPlan full(ClipWarpRegion{a.sourceOrigin,48000,196608,65536,98304,{}, {65536,0,1}},a.warp->markers,a.warp->protection,memory);
  Json input;std::cin>>input;Json output=Json::array();for(const auto &row:input){std::vector<CompHandlePoint> map;for(const auto &p:row.at("map"))map.push_back({pos(p[0]),pos(p[1])});try{const auto count=row.at("frames").get<Frame>();const auto clock=verifyProtectedCompClock(full,map,count,[]{});output.push_back({{"accepted",true},{"origin",pos(scaleSourcePosition(clock.at(0),1,1))},{"step",pos(scaleSourcePosition({1,0,1},clock.numerator(),clock.denominator()))},{"last",pos(scaleSourcePosition(clock.at(count-1),1,1))}});}catch(const ProjectError &){output.push_back({{"accepted",false}});}}
  std::cout<<output.dump()<<'\n';return 0;
 }
 check(argc==3,"Supply raw sample rate and evidence root");const std::string path=argv[2];const std::filesystem::path base(std::u8string(path.begin(),path.end()));std::filesystem::create_directories(base);const auto root=base/("run-"+Id::generate().str());check(std::filesystem::create_directory(root),"Evidence directory already exists");const auto rate=unsigned(std::stoul(argv[1]));check(rate==44100 || rate==48000 || rate==96000,"Unsupported fixture rate");
 ResourceLedger ledger(96*1024*1024);Json rows=Json::array();double maximumPitchError=0;
 {
  auto original=fixture(rate);const auto anchor=*original.tracks[0].clips[0].stretch;
  std::vector<float> raw(196608);for(std::size_t i=0;i<raw.size();++i)raw[i]=float(2.5*std::sin(2*std::numbers::pi*211*double(i)/rate));save(root/"raw.f32le",raw);
  {
   auto e=envelope(original,1,17,ledger);auto plan=prepareClockedProtectedCompSpan(e,ledger,[]{});check(plan.source()==e.data().raw && plan.outputSampleRate()==rate,"Initial raw/clock binding lost");
   check(plan.geometry().map().exactRegion().rawOrigin==e.data().map.front().raw && plan.geometry().map().exactRegion().inputExtent==subtractSourcePosition(e.data().map.back().raw,e.data().map.front().raw),"Fractional outer span changed");
   const auto pcm=render(plan.sampling(),raw,ledger,&plan);check(*std::max_element(pcm.begin(),pcm.end())>1,"Initial headroom lost");save(root/"candidate-0.f32le",pcm);
   auto omitted=e.data().map;omitted.erase(omitted.begin()+1,omitted.end()-1);
   refuses([&]{(void)verifyProtectedCompClock(plan.geometry(),omitted,e.data().outputFrames,[]{});});
   auto altered=e.data().map;altered[1].raw=addSourcePosition(altered[1].raw,{0,1,97});
   refuses([&]{(void)verifyProtectedCompClock(plan.geometry(),altered,e.data().outputFrames,[]{});});
   const auto baseline=ledger.usage().reservedBytes;unsigned polls=0;bool sawOwned=false;
   refuses([&]{(void)prepareClockedProtectedCompSpan(e,ledger,[&]{++polls;if(ledger.usage().reservedBytes>baseline){sawOwned=true;throw ProjectError(ErrorCode::Canceled,"Cancel exact history preparation");}});});
   check(polls>0 && sawOwned && ledger.usage().reservedBytes==baseline,"Canceled adapter retained credits");
   ResourceLedger tiny(1);refuses([&]{(void)prepareClockedProtectedCompSpan(e,tiny,[]{});});check(!tiny.usage().reservedBytes,"Budget refusal retained credits");
   rows.push_back({{"rawRate",rate},{"layers",0},{"rawOrigin",pos(plan.geometry().map().exactRegion().rawOrigin)},{"frames",pcm.size()}});
  }
  for(unsigned layers:{1u,2u}){
   auto s=original;auto &clip=s.tracks[0].clips[0];clip.sourceFrame=28000;clip.sourceTiming={1,7};clip.lengthFrames=4000;clip.playbackRate={3,2};validate(s);
   SourcePosition semanticOrigin{28000,1,7};SourcePosition step=scaleSourcePosition({1,0,1},std::uint64_t(rate)*3,48000*2);SourcePosition baked{1,1,2};
   s=layer(s,true,ledger);
   if(layers==2){auto &current=s.tracks[0].clips[0];current.sourceFrame=13;current.sourceTiming={2,11};current.lengthFrames=3000;current.playbackRate={2,3};validate(s);semanticOrigin=addSourcePosition(semanticOrigin,product({13,2,11},step));step=scaleSourcePosition(step,2,3);baked={1,0,1};s=layer(s,true,ledger);}
   // Add an unbaked physical ancestor too. Its mere presence must not change pitch.
   s=layer(s,false,ledger);const auto snapshot=s;
   {
    auto e=envelope(s,32,32,ledger);auto plan=prepareClockedProtectedCompSpan(e,ledger,[]{});
    check(plan.bakedPitchScale()==baked && plan.outputSampleRate()==48000,"Baked pitch/output clock lost");
    const auto expectedOrigin=subtractSourcePosition(semanticOrigin,scaleSourcePosition(step,std::uint64_t(e.data().prefixFrames),1));
    const auto &clock=plan.sampling().clock();if(scaleSourcePosition(clock.at(0),1,1)!=expectedOrigin || scaleSourcePosition({1,0,1},clock.numerator(),clock.denominator())!=step)std::cerr<<Json{{"rate",rate},{"layers",layers},{"prefix",e.data().prefixFrames},{"actualOrigin",pos(clock.at(0))},{"expectedOrigin",pos(expectedOrigin)},{"actualStep",pos(scaleSourcePosition({1,0,1},clock.numerator(),clock.denominator()))},{"expectedStep",pos(step)}}.dump()<<'\n';check(scaleSourcePosition(clock.at(0),1,1)==expectedOrigin && scaleSourcePosition({1,0,1},clock.numerator(),clock.denominator())==step,"Recovered original clock differs from independent ancestry calculation");
    check(plan.geometry().map().exactRegion().rawOrigin==anchor.sourceOrigin && plan.geometry().map().exactRegion().inputExtent==SourcePosition{65536,0,1} && plan.geometry().map().exactRegion().outputExtent==SourcePosition{98304,0,1},"Cropped ancestry changed full original extent");
    unsigned missing=0;for(const auto &m:anchor.warp->markers){const auto rawAnchor=addSourcePosition(anchor.sourceOrigin,m.source);if(sourcePositionLess(rawAnchor,e.data().map.front().raw) || !sourcePositionLess(rawAnchor,e.data().map.back().raw))++missing;}
    check(missing==2 && plan.geometry().spans().size()==2,"Fixture lost outside anchors or adapter omitted them");outside+=missing;
    const auto pcm=render(plan.sampling(),raw,ledger,&plan);
    // Independently construct the original nominal plan, then request an earlier
    // clock window with the same phase. Candidate bytes must equal its crop.
    ProtectedWarpPlan originalGeometry(ClipWarpRegion{anchor.sourceOrigin,rate,196608,65536,98304,{}, {65536,0,1}},anchor.warp->markers,anchor.warp->protection,ledger);
    const Frame offset=4096;const auto referenceOrigin=subtractSourcePosition(expectedOrigin,scaleSourcePosition(step,offset,1));
    const auto referenceClock=SourceFrameMap::processingGrid(referenceOrigin,scaleSourcePosition(step,std::uint64_t(e.data().outputFrames+offset),1),e.data().outputFrames+offset);
    PreparedClockedProtectedWarp reference(originalGeometry,referenceClock,e.data().outputFrames+offset,1,ledger,[]{});const auto full=render(reference,raw,ledger);save(root/("candidate-"+std::to_string(layers)+".f32le"),pcm);save(root/("reference-"+std::to_string(layers)+".f32le"),full);
    for(std::size_t i=0;i<pcm.size();++i){check(std::bit_cast<std::uint32_t>(pcm[i])==std::bit_cast<std::uint32_t>(full[i+std::size_t(offset)]),"Cropped lineage altered original gap audio");++compared;}
    const auto wanted=211*(double(baked.frame)+double(baked.fraction)/double(baked.denominator));const auto measured=frequency(pcm,48000);maximumPitchError=std::max(maximumPitchError,std::abs(measured-wanted));check(std::abs(measured-wanted)<wanted*.01,"Clocked history changed baked pitch");
    check(s==snapshot,"Preparation/render changed project");rows.push_back({{"rawRate",rate},{"layers",layers},{"outsideAnchorsRetained",missing},{"clockOrigin",pos(clock.at(0))},{"clockStep",pos(step)},{"bakedPitch",pos(baked)},{"frames",pcm.size()},{"wantedHz",wanted},{"measuredHz",measured}});
   }
   check(!ledger.usage().reservedBytes,"Lineage/render retained credits");
  }
  auto owned=[&]{auto e=envelope(original,1,17,ledger);return prepareClockedProtectedCompSpan(e,ledger,[]{});}();auto moved=std::move(owned);refuses([&]{(void)owned.sampling();});check(moved.geometry().spans().size()==2,"Plan borrowed destroyed envelope");
 }
 check(!ledger.usage().reservedBytes,"Final adapter credits leaked");
 const Json report{{"rawRate",rate},{"checks",checks},{"cropComparedSamples",compared},{"outsideAnchorsRetained",outside},{"maximumPitchErrorHz",maximumPitchError},{"rows",rows},{"allCreditsReleased",true},{"actualCandidateRendering",true},{"historyFixturesAreMetadataOnly",true},{"workerIntegrated",false},{"qualityAdmission",false}};
 {std::ofstream out(root/"report.json");out<<report.dump(2)<<'\n';out.flush();check(bool(out),"Evidence report write failed");}
 std::cout<<report.dump()<<'\n';return 0;
}catch(const std::exception &e){std::cerr<<e.what()<<'\n';return 1;}}
