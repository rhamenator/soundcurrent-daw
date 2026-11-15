// SPDX-License-Identifier: GPL-3.0-only
#include <soundcurrent/comp_handle_plan.hpp>
#include <soundcurrent/stretch.hpp>
#include <soundcurrent/clip_timing.hpp>
#include <iostream>
#include <limits>
#include <type_traits>
#include <utility>
using namespace soundcurrent::daw;
const CompHandlePlanContext context{42,"workspace/é-日本"};
static_assert(!std::is_copy_constructible_v<CompHandleEnvelope>);
static_assert(!std::is_move_assignable_v<CompHandleEnvelope>);
static_assert(std::is_same_v<decltype(std::declval<CompHandleEnvelope &>().data()),const CompHandleEnvelopeData &>);
static_assert(!std::is_copy_constructible_v<CompTransitionHandlePlan>);
static_assert(!std::is_move_assignable_v<CompTransitionHandlePlan>);
unsigned checks=0;
void check(bool v,const char *s){++checks;if(!v)throw std::runtime_error(s);}
template<class F>void refuses(F f){try{f();}catch(const ProjectError &){++checks;return;}throw std::runtime_error("Invalid envelope accepted");}
Session fixture(unsigned mode){
 auto s=makeOneTrackSession("Envelope","Audio");auto &t=s.tracks[0];
 Asset raw;raw.relativePath="media/raw.wav";raw.frames=4096;raw.sha256=std::string(64,'a');s.assets={raw};
 TakeLane lane;lane.captureEndFrame=4096;Clip retained;retained.assetId=raw.id;retained.lengthFrames=4096;lane.clips={retained};t.takeLanes={lane};
 ClipStretchAnchor anchor;anchor.sourceAssetId=raw.id;anchor.sourceSha256=raw.sha256;anchor.sourceOrigin={512,1,7};anchor.sourceFrames=mode==3?257:256;anchor.settings={3,2,0,true};anchor.renderKey=std::string(64,'b');
 if(mode==1)anchor.context=StretchContext{32,48};
 if(mode==2){WarpSettings w;w.protection={8,16,2,8};w.markers.push_back({Id::generate(),{128,0,1},{192,0,1}});anchor.warp=w;}
 anchor.processor=stretchProcessorFor(anchor.settings,anchor.context,anchor.warp);auto g=stretchGeometry(anchor,raw.frames);
 Asset base=raw;base.id=Id::generate();base.relativePath="media/render.wav";base.frames=g.outputFrames;base.sha256=std::string(64,'c');s.assets.push_back(base);
 Clip c;c.startFrame=100;c.lengthFrames=mode==3?386:384;c.sourceFrame=g.visibleBegin.frame;c.sourceTiming={g.visibleBegin.fraction,g.visibleBegin.denominator};c.takeOrigin=ClipTakeOrigin{t.id,lane.id,retained.id};c.assetId=base.id;c.stretch=anchor;
 if(mode==4){c.sourceTiming={1,3};c.lengthFrames=383;}
 t.clips={c};validate(s);return s;
}
int main(){try{
 for(unsigned mode=0;mode<5;++mode){auto s=fixture(mode);auto before=s;auto &t=s.tracks[0];auto &c=t.clips[0];ResourceLedger ledger(4*1024*1024,"Envelope test");
  {auto p=prepareCompHandleEnvelope(s,t.id,c.id,74,mode==3?512:510,context,ledger);
   check(p.data().expected==c && p.data().retained==t.takeLanes[0].clips[0] && s==before,"Envelope changed source snapshot");
   verifyCompHandleEnvelopeCurrent(s,context,p);
   for(unsigned bad=0;bad<5;++bad){auto changed=s;
    if(bad==0)changed.id=Id::generate();
    if(bad==1)changed.sampleRate=44100;
    if(bad==2)changed.tracks[0].clips[0].processing.gainDb=1;
    if(bad==3)changed.assets[0].sha256=std::string(64,'f');
    if(bad==4)changed.tracks[0].takeLanes[0].clips[0].lengthFrames--;
    refuses([&]{verifyCompHandleEnvelopeCurrent(changed,context,p);});
   }

   check(p.data().expected.stretch==c.stretch && p.data().raw==s.assets[0] && p.data().base==s.assets[1],"Envelope changed settings/context/markers/assets");
   check(p.data().prefixFrames==(mode==1?0:26) && p.data().suffixFrames==(mode==1?0:26),"Envelope integer extent differs");
   check(p.data().needsNominalExposure && p.data().needsNewSpan==(mode!=1),"Context exposure confused with missing data");
   if(mode==0 || mode==1)check(p.data().requestedRawFirst==SourcePosition{494,17,21} && p.data().requestedRawEnd==SourcePosition{785,10,21},"Independent Fraction legacy/context positions differ");
   if(mode==2)check(p.data().requestedRawEnd==SourcePosition{784,53,77},"Independent Fraction warp extension differs");
   if(mode==4)check(p.data().requestedRawFirst==SourcePosition{495,2,63} && p.data().requestedRawEnd==SourcePosition{785,44,63} && p.data().clipOutputOrigin==SourcePosition{0,1,3},"Fractional envelope origin differs");
   for(std::size_t i=1;i<p.data().map.size();++i)check(sourcePositionLess(p.data().map[i-1].raw,p.data().map[i].raw) && sourcePositionLess(p.data().map[i-1].output,p.data().map[i].output),"Envelope map not monotone");
   check(p.data().map.front().output==SourcePosition{0,0,1} && p.data().map.back().output==SourcePosition{p.data().outputFrames,0,1},"Envelope map endpoints differ");
  }check(ledger.usage().reservedBytes==0,"Envelope retained memory after destruction");
  auto limit=CompHandlePlanLimits{};limit.maximumOutputFrames=1;refuses([&]{prepareCompHandleEnvelope(s,t.id,c.id,74,510,context,ledger,limit);});check(ledger.usage().reservedBytes==0 && s==before,"Frame refusal leaked or changed source");
  limit={};limit.maximumOutputBytes=8;refuses([&]{prepareCompHandleEnvelope(s,t.id,c.id,74,510,context,ledger,limit);});check(ledger.usage().reservedBytes==0,"Byte refusal leaked");
  ResourceLedger tiny(1,"Tiny");refuses([&]{prepareCompHandleEnvelope(s,t.id,c.id,74,510,context,tiny);});check(tiny.usage().reservedBytes==0,"Credit refusal leaked");
 }

 for(bool mixed:{false,true}){
  auto pair=fixture(0);auto &track=pair.tracks[0];
  auto incoming=track.clips[0];incoming.id=Id::generate();incoming.startFrame=484;
  if(mixed){incoming.assetId=pair.assets[0].id;incoming.stretch.reset();incoming.sourceFrame=900;}
  auto second=track.takeLanes[0];second.id=Id::generate();second.groupId=Id::generate();second.clips[0].id=Id::generate();
  incoming.takeOrigin=ClipTakeOrigin{track.id,second.id,second.clips[0].id};track.takeLanes.push_back(second);
  track.clips.push_back(incoming);validate(pair);const auto original=pair;
  ResourceLedger ledger(4*1024*1024,"Pair");
  {PrepareCompTransition edit{track.id,track.clips[0].id,incoming.id,458,510,ClipFadeCurve::EqualPower};
   auto plan=prepareCompTransitionHandles(pair,edit,context,ledger);verifyCompTransitionHandlesCurrent(pair,context,plan);
   check(plan.outgoing().data().suffixFrames==26 && plan.incoming().data().prefixFrames==(mixed?0:26),"Paired envelope extent differs");
   check(!plan.incoming().data().needsNewSpan || !mixed,"Raw fragment incorrectly needs synthetic handles");
   check(pair==original,"Paired planning mutated project");
   auto changed=pair;changed.tracks[0].clips[1].processing.gainDb=2;refuses([&]{verifyCompTransitionHandlesCurrent(changed,context,plan);});
  }check(ledger.usage().reservedBytes==0,"Pair retained credit");
  auto bad=PrepareCompTransition{track.id,track.clips[0].id,incoming.id,458,510,ClipFadeCurve::Linear};
  auto &limited=pair.tracks[0].takeLanes[1].clips[0];limited.sourceFrame=mixed?900:512;limited.lengthFrames=384;
  refuses([&]{prepareCompTransitionHandles(pair,bad,context,ledger);});check(ledger.usage().reservedBytes==0,"Last-fragment refusal retained first plan credit");
 }

 for(const auto rate:{44100u,48000u,96000u})for(const auto speed:{ClipPlaybackRate{1,1},ClipPlaybackRate{3,2}}) {
  auto routed=fixture(0);routed.assets[0].sampleRate=rate;routed.assets[1].sampleRate=rate;
  auto &track=routed.tracks[0];auto &clip=track.clips[0];clip.playbackRate=speed;
  clip.lengthFrames=SourceFrameMap(rate,48000,0,speed).projectFramesForSource(routed.assets[1].frames);
  while(SourceFrameMap(rate,48000,0,speed).at(clip.lengthFrames-1).frame>=routed.assets[1].frames)--clip.lengthFrames;
  track.takeLanes[0].clips[0].lengthFrames=SourceFrameMap(rate,48000).projectFramesForSource(routed.assets[0].frames);
  while(SourceFrameMap(rate,48000).at(track.takeLanes[0].clips[0].lengthFrames-1).frame>=routed.assets[0].frames)--track.takeLanes[0].clips[0].lengthFrames;
  track.takeLanes[0].captureEndFrame=track.takeLanes[0].clips[0].lengthFrames;validate(routed);
  ResourceLedger ledger(4*1024*1024,"Rate");auto plan=prepareCompHandleEnvelope(routed,track.id,clip.id,90,clip.startFrame+clip.lengthFrames+10,context,ledger);
  // Fraction(-10 * rate/48000 * speed * 256/384) plus 512+1/7.
  const auto delta=scaleSourcePosition({10,0,1},std::uint64_t(rate)*speed.numerator*2,std::uint64_t(48000)*speed.denominator*3);
  check(plan.data().requestedRawFirst==subtractSourcePosition({512,1,7},delta),"Cross-rate raw handle position differs");
  check(plan.data().expected.playbackRate==speed && plan.data().projectRate==48000 && plan.data().base.sampleRate==rate,"Cross-rate settings changed");
 }

 {
  auto nested=fixture(0);auto &track=nested.tracks[0];auto &retained=track.takeLanes[0].clips[0];
  ClipStretchAnchor prior;prior.sourceAssetId=nested.assets[0].id;prior.sourceSha256=nested.assets[0].sha256;
  prior.sourceOrigin={0,0,1};prior.sourceFrames=4096;prior.settings={2,1,0,true};prior.renderKey=std::string(64,'d');prior.processor=stretchProcessorFor(prior.settings);
  auto asset=nested.assets[0];asset.id=Id::generate();asset.relativePath="media/prior.wav";asset.sha256=std::string(64,'e');asset.frames=8192;nested.assets.push_back(asset);
  retained.assetId=asset.id;retained.stretch=prior;retained.lengthFrames=8192;track.takeLanes[0].captureEndFrame=8192;validate(nested);
  auto plan=prepareCompHandleEnvelope(nested,track.id,track.clips[0].id,74,510,context,ResourceLedger{});
  check(plan.data().requestedRawFirst==SourcePosition{494,17,21} && plan.data().retainedAsset==asset,"Processed retained take raw mapping differs");
  verifyCompHandleEnvelopeCurrent(nested,context,plan);
 }
 auto s=fixture(0);auto &t=s.tracks[0];auto clip=t.clips[0].id;
 {
  ResourceLedger ledger(4*1024*1024,"Sealed plan");
  {
   auto prepared=prepareCompHandleEnvelope(s,t.id,clip,74,510,context,ledger);
   for(const auto &changed:{CompHandlePlanContext{43,context.rootIdentity},
                           CompHandlePlanContext{42,"other-workspace"}})
    refuses([&]{verifyCompHandleEnvelopeCurrent(s,changed,prepared);});
   const auto charged=ledger.usage().reservedBytes;
   auto moved=std::move(prepared);
   refuses([&]{(void)prepared.data();});
   check(ledger.usage().reservedBytes==charged,"Move retired live plan resource credit");
   verifyCompHandleEnvelopeCurrent(s,context,moved);
  }
  check(ledger.usage().reservedBytes==0,"Sealed plan retirement retained credit");
  for(const auto &invalid:{CompHandlePlanContext{0,context.rootIdentity},CompHandlePlanContext{42,""},
                         CompHandlePlanContext{42,std::string(4097,'x')},
                         CompHandlePlanContext{42,std::string("root\0other",10)}})
   refuses([&]{prepareCompHandleEnvelope(s,t.id,clip,74,510,invalid,ledger);});
  auto invalid=CompHandlePlanLimits{};invalid.maximumMapPoints=SIZE_MAX;
  refuses([&]{prepareCompHandleEnvelope(s,t.id,clip,74,510,context,ledger,invalid);});
  invalid={};invalid.maximumOutputBytes=UINT64_MAX;
  refuses([&]{prepareCompHandleEnvelope(s,t.id,clip,74,510,context,ledger,invalid);});
  check(ledger.usage().reservedBytes==0,"Invalid admission retained credit");
  auto invalidPair=s;
  auto incoming=invalidPair.tracks[0].clips[0];incoming.id=Id::generate();incoming.startFrame=484;
  invalidPair.tracks[0].clips.push_back(incoming);
  invalidPair.tracks[0].clips[0].lengthFrames=INT64_MAX;
  const PrepareCompTransition edit{t.id,clip,incoming.id,458,510,ClipFadeCurve::Linear};
  refuses([&]{prepareCompTransitionHandles(invalidPair,edit,context,ledger);});
  check(ledger.usage().reservedBytes==0,"Malformed pair leaked validation credit");
 }
 {
  const auto ratio=sourceIntervalRatio({3,1,2},{2,1,3});
  check(ratio.numerator==3 && ratio.denominator==2,"Independent interval Fraction ratio differs");
  const auto equal=sourceIntervalRatio({INT64_MAX,1,3},{INT64_MAX,2,6});
  check(equal.numerator==1 && equal.denominator==1,"Equal large intervals overflowed unnecessarily");
  refuses([&]{sourceIntervalRatio({0,0,1},{0,0,1});});
  refuses([&]{sourceIntervalRatio({1,0,0},{1,0,1});});
  refuses([&]{sourceIntervalRatio({-1,0,1},{1,0,1});});
  refuses([&]{sourceIntervalRatio({INT64_MAX,1,3},{1,0,1});});
 }
 refuses([&]{prepareCompHandleEnvelope(s,t.id,clip,-1,510,context,ResourceLedger{});});
 refuses([&]{prepareCompHandleEnvelope(s,t.id,clip,101,510,context,ResourceLedger{});});
 refuses([&]{prepareCompHandleEnvelope(s,t.id,clip,74,483,context,ResourceLedger{});});
 t.takeLanes[0].clips[0].sourceFrame=512;t.takeLanes[0].clips[0].lengthFrames=256;
 refuses([&]{prepareCompHandleEnvelope(s,t.id,clip,74,510,context,ResourceLedger{});});
 std::cout<<"comp_handle_plan_checks="<<checks<<" immutable=true credit_released=true actual_render=false\n";return 0;
}catch(const std::exception &e){std::cerr<<e.what()<<'\n';return 1;}}
