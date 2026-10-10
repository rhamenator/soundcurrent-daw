// SPDX-License-Identifier: GPL-3.0-only
#include <soundcurrent/positioned_resampling.hpp>
#include <soundcurrent/resampling.hpp>
#include <soundcurrent/mix_reader.hpp>
#include <soundcurrent/export.hpp>
#include <soundcurrent/recording.hpp>
#include <soundcurrent/wave_validation.hpp>
#include <nlohmann/json.hpp>
#include "rt_audit.hpp"
#include <algorithm>
#include <charconv>
#include <cmath>
#include <iostream>
#include <numbers>
#include <chrono>
#include <array>
#include <limits>
using namespace soundcurrent::daw;
namespace {
std::uint64_t checks=0;
void check(bool v,const char *m){++checks;if(!v)throw std::runtime_error(m);}
template<class F> void refuses(F f){bool seen=false;try{f();}catch(const ProjectError &){seen=true;}check(seen,"Expected timing/window/state refusal");}
constexpr std::uint32_t rates[]={8000,11025,16000,22050,32000,44100,48000,88200,96000,192000,384000};
std::vector<float> stream(const std::vector<float>&in,std::uint32_t source,std::uint32_t project,std::uint32_t channels){
 ResamplingConfig c;c.sourceRate=source;c.projectRate=project;c.channels=channels;c.maximumInputFrames=65536;c.maximumOutputFrames=65536;
 PreparedResampler r(c);r.push(in,true);std::vector<float> result,tmp(std::size_t(65536)*channels);
 while(!r.statistics().drained){auto n=r.pull(tmp);result.insert(result.end(),tmp.begin(),tmp.begin()+std::size_t(n)*channels);}return result;
}
std::vector<float> positioned(const std::vector<float>&in,std::uint32_t source,std::uint32_t project,std::uint32_t channels,
 const SourceFrameMap &map,Frame first,Frame frames,std::uint32_t block){
 (void)source;(void)project;PreparedPositionedResampling kernel(map,channels);std::vector<float> result(std::size_t(frames)*channels);
 for(Frame at=0;at<frames;){const auto n=std::uint32_t(std::min<Frame>(block,frames-at));const auto range=kernel.sourceRange(map,first+at,n,Frame(in.size()/channels));
  check(range.frames<=kernel.maximumSourceWindowFrames(block),"Source context exceeds declared window");
  const auto src=std::span<const float>(in).subspan(std::size_t(range.first)*channels,range.frames*channels);
  kernel.process(map,first+at,Frame(in.size()/channels),range.first,src,{result.data()+std::size_t(at)*channels,std::size_t(n)*channels});at+=n;}
 return result;
}
void kernels(){double maximum=0;std::uint64_t samples=0;
 for(auto source:rates)for(auto project:rates){
  std::vector<float> in(source/32);for(std::size_t f=0;f<in.size();++f)in[f]=float(2.5*std::sin(2*std::numbers::pi*std::min(source,project)/16.*double(f)/source));
  SourceFrameMap map(source,project);const auto frames=map.projectFramesForSource(Frame(in.size()));
  const auto oracle=stream(in,source,project,1);const auto actual=positioned(in,source,project,1,map,0,frames,211);
  check(actual.size()==oracle.size(),"Positioned/streaming output extent differs");
  for(std::size_t f=0;f<actual.size();++f){const auto error=std::abs(double(actual[f])-oracle[f]);maximum=std::max(maximum,error);check(error<2e-6,"Positioned/pinned converter sample differs");}samples+=actual.size();
  check(positioned(in,source,project,1,map,0,frames,37)==actual,"Positioned partitions change samples");
  const auto split=frames/3;if(split){const auto right=positioned(in,source,project,1,map.advanced(split),0,frames-split,127);
   check(std::equal(right.begin(),right.end(),actual.begin()+split),"Fractional split changed source samples");}
 }
 for(auto channels:{1u,2u,3u,4u,6u,8u,32u,128u,256u}){
  std::vector<float> input(std::size_t(1024)*channels);for(std::size_t f=0;f<1024;++f)for(std::uint32_t c=0;c<channels;++c)input[f*channels+c]=float(2.5*std::sin(2*std::numbers::pi*(100+c)*double(f)/48000));
  SourceFrameMap map(48000,44100);auto frames=map.projectFramesForSource(1024);
  const auto expected=stream(input,48000,44100,channels),actual=positioned(input,48000,44100,channels,map,0,frames,127);
  for(std::size_t n=0;n<actual.size();++n)check(std::abs(actual[n]-expected[n])<2e-6,"Positioned channel isolation differs");
 }
 std::vector<float> tone(48000);for(std::size_t f=0;f<tone.size();++f)tone[f]=float(2.5*std::sin(2*std::numbers::pi*1000*double(f)/48000));
 SourceFrameMap fraction(48000,44100,SourcePosition{4096,1,7});const auto actual=positioned(tone,48000,44100,1,fraction,0,8192,113);
 double analytic=0;for(std::size_t n=0;n<actual.size();++n){const auto at=4096.+1./7+double(n)*48000/44100;analytic=std::max(analytic,std::abs(double(actual[n])-2.5*std::sin(2*std::numbers::pi*1000*at/48000)));}
 check(analytic<2e-6,"Arbitrary fraction differs from independent sine phase");
 PreparedPositionedResampling same(48000,48000,1);SourceFrameMap almostOne(48000,48000,SourcePosition{4096,UINT64_MAX-1,UINT64_MAX});
 const auto near=positioned(tone,48000,48000,1,almostOne,0,128,37);
 for(std::size_t f=0;f<near.size();++f)check(std::abs(near[f]-tone[4097+f])<2e-6,"Near-one fraction dropped the center tap");
 auto bad=tone;bad[4096]=std::numeric_limits<float>::quiet_NaN();std::vector<float> out(1);
 refuses([&]{same.process(SourceFrameMap(48000,48000),4096,48000,0,bad,out);});
 refuses([&]{same.process(SourceFrameMap(48000,48000,SourcePosition{4096,1,2}),0,48000,4096,std::span<const float>(tone).subspan(4096,1),out);});
 const auto untouched=tone;
 refuses([&]{same.process(SourceFrameMap(48000,48000),4096,48000,0,tone,
                         std::span<float>(tone).subspan(4096,1));});
 check(tone==untouched,"Overlapping neutral storage was changed before refusal");
 refuses([&]{same.process(SourceFrameMap(48000,48000,SourcePosition{4096,1,2}),0,
                         48000,0,tone,std::span<float>(tone).subspan(4096,1));});
 check(tone==untouched,"Overlapping FIR storage was changed before refusal");
 std::cout<<"positioned_pinned_samples="<<samples<<" maximum_difference="<<maximum<<" arbitrary_fraction_sine_error="<<analytic<<" layouts=1,2,3,4,6,8,32,128,256\n";
}
void state(){
 auto s=makeOneTrackSession("Fractional source","Track",44100);Asset a;a.sampleRate=48000;a.frames=100000;a.relativePath="media/owned.wav";a.sha256=std::string(64,'a');s.assets.push_back(a);
 Clip c;c.assetId=a.id;c.startFrame=17;c.sourceFrame=101;c.lengthFrames=1000;c.sourceTiming={1,7};c.processing.fadeIn={-3,700,ClipFadeCurve::Linear,1};s.tracks[0].clips.push_back(c);
 const auto original=s;auto json=nlohmann::json::parse(encodeProject(s));check(json["schemaMinor"]==14 && decodeProject(json.dump())==s,"Fractional project roundtrip differs");
 for(unsigned mode=0;mode<11;++mode){auto bad=json;auto &timing=bad["tracks"][0]["clips"][0]["sourceTiming"];
  if(mode==0)timing["fraction"]=-1;
  if(mode==1)timing["denominator"]=0;
  if(mode==2)timing["fraction"]=7;
  if(mode==3)timing["fraction"]=true;
  if(mode==4)timing["denominator"]=1.5;
  if(mode==5)timing["algorithm"]="unknown";
  if(mode==6)timing.erase("fraction");
  if(mode==7)timing["unknown"]=1;
  if(mode==8)bad["tracks"][0]["clips"][0].erase("sourceTiming");
  if(mode==9)timing["denominator"]=UINT64_MAX;
  if(mode==10)timing["fraction"]="1";
  refuses([&]{decodeProject(bad.dump());});}
 EditHistory history(s);const auto right=Id::generate();check(history.structural({SplitClip{s.tracks[0].id,c.id,right,130}}),"Fractional split missing");
 const auto map=clipSourceMap(c,48000,44100),split=clipSourceMap(s.tracks[0].clips[1],48000,44100);
 for(Frame f=0;f<500;++f)check(split.at(f)==map.at(f+113),"Stored split lost fraction");
 check(s.tracks[0].clips[1].processing.fadeIn.startFrame==-116,"Fade not in project domain");
 check(history.undo() && s==original && history.redo(),"Fractional split history differs");check(history.undo(),"Split restore missing");
 check(history.structural({CropClip{s.tracks[0].id,c.id,54,37,900}}),"Project crop missing");
 check(clipSourceMap(s.tracks[0].clips[0],48000,44100).at(0)==map.at(37),"Project crop source differs");
 auto inverse=s;applySessionEdits(inverse,{CropClip{s.tracks[0].id,c.id,17,-37,1000}});
 check(inverse==original,"Inverse project crop did not restore canonical fraction/fades");
 check(history.undo() && s==original,"Project crop undo differs");
 refuses([&]{history.structural({SetClipRange{s.tracks[0].id,c.id,17,102,900}});});check(s==original,"Off-grid trim partly changed state");
 auto neutral=original;neutral.sampleRate=48000;neutral.tracks[0].clips[0].sourceTiming={};neutral.tracks[0].clips[0].processing={};
 auto old=nlohmann::json::parse(encodeProject(neutral));old["schemaMinor"]=9;old["tracks"][0]["clips"][0].erase("sourceTiming");old["tracks"][0]["clips"][0].erase("playbackRate");old["tracks"][0]["clips"][0].erase("stretch");check(decodeProject(old.dump())==neutral,"Schema1.9 neutral migration differs");
 old["sampleRate"]=44100;
  refuses([&]{decodeProject(old.dump());});
}
void rateKernels(){
 std::vector<float> tone(48000);for(std::size_t i=0;i<tone.size();++i)tone[i]=float(1.7*std::sin(2*std::numbers::pi*1000*double(i)/48000));
 double maximum=0;
 for(auto rate:{ClipPlaybackRate{1,4},ClipPlaybackRate{1,2},ClipPlaybackRate{1333,1000},ClipPlaybackRate{4,1}}){
  SourceFrameMap map(48000,48000,SourcePosition{4096,1,7},rate);
  auto out=positioned(tone,48000,48000,1,map,0,8192,113);
  check(positioned(tone,48000,48000,1,map,0,8192,37)==out,"Rate slab partition changed waveform");
  for(std::size_t i=0;i<out.size();++i){auto p=map.at(Frame(i));auto expected=1.7*std::sin(2*std::numbers::pi*1000*(double(p.frame)+double(p.fraction)/double(p.denominator))/48000);maximum=std::max(maximum,std::abs(out[i]-expected));}
 }
 check(maximum<2e-5,"Linked-rate sine amplitude/phase error");
 for(std::size_t i=0;i<tone.size();++i)tone[i]=float(std::sin(2*std::numbers::pi*12000*double(i)/48000));
 SourceFrameMap fast(48000,48000,4096,{4,1});auto rejected=positioned(tone,48000,48000,1,fast,0,4096,127);
 double alias=0;for(float x:rejected)alias=std::max(alias,std::abs(double(x)));
 check(alias<1e-5,"Fast playback did not suppress out-of-band alias");
 SourceFrameMap canceled(48000,96000,19,{2,1});PreparedPositionedResampling copy(canceled,1);
 check(copy.exactCopy(canceled),"Speed/rate cancellation missed exact copy");
 auto neutral=positioned(tone,48000,96000,1,canceled,0,200,37);
 check(std::equal(neutral.begin(),neutral.end(),tone.begin()+19),"Canceled speed changed exact samples");
 refuses([&]{copy.exactCopy(fast);});
 SourceFrameMap maximumStep(384000,8000,0,{4,1});PreparedPositionedResampling large(maximumStep,256);
 check(large.sourceContextFrames()<30000 && large.maximumSourceWindowFrames(65536)>65536,"Extreme effective ratio/context incorrect");
 std::cout<<"rate_sine_max_error="<<maximum<<" rejected_alias_peak="<<alias<<" effective_step_max=192 channels_max=256\n";
}
void rateState(){
 auto s=makeOneTrackSession("Linked speed","Track",44100);Asset a;a.sampleRate=48000;a.frames=50000;a.relativePath="media/owned.wav";a.sha256=std::string(64,'a');s.assets.push_back(a);
 Clip c;c.assetId=a.id;c.sourceFrame=101;c.sourceTiming={1,7};c.startFrame=17;c.lengthFrames=1001;c.processing.fadeIn={-3,701,ClipFadeCurve::EqualPower,1};s.tracks[0].clips.push_back(c);
 const auto original=s;EditHistory history(s);check(history.structural({SetClipPlaybackRate{s.tracks[0].id,c.id,{2,1}}}),"Rate command missing");
 const auto changed=s;const auto &now=s.tracks[0].clips[0];check(now.lengthFrames==501 && now.startFrame==17 && now.sourceTiming==c.sourceTiming && now.sourceFrame==101,"Speed changed origin/start or failed to change duration");
 check(now.processing.fadeIn.startFrame==-2 && now.processing.fadeIn.endFrame==351,"Signed fade anchors not rounded outward");
 check(history.undo() && s==original && history.redo() && s==changed,"Rate undo/redo not exact");
 auto trimmed=changed;applySessionEdits(trimmed,{SetClipRange{s.tracks[0].id,c.id,17,421,200}});
 check(clipSourceMap(trimmed.tracks[0].clips[0],48000,44100).at(0)==clipSourceMap(changed.tracks[0].clips[0],48000,44100).at(147) && trimmed.tracks[0].clips[0].processing.fadeIn.startFrame==-149,"Integer trim ignored linked rate");
 refuses([&]{auto bad=changed;applySessionEdits(bad,{SetClipRange{s.tracks[0].id,c.id,17,102,200}});});
 check(decodeProject(encodeProject(s))==changed,"Nonneutral rate state roundtrip differs");
 auto json=nlohmann::json::parse(encodeProject(s));
 for(unsigned mode=0;mode<12;++mode){auto bad=json;auto &rate=bad["tracks"][0]["clips"][0]["playbackRate"];
  if(mode==0)rate["numerator"]=0;
  if(mode==1)rate["denominator"]=0;
  if(mode==2)rate["numerator"]=4001;
  if(mode==3)rate["denominator"]=UINT64_MAX;
  if(mode==4)rate["numerator"]=true;
  if(mode==5)rate["denominator"]=1.5;
  if(mode==6)rate["numerator"]=-1;
  if(mode==7)rate["mode"]="stretch";
  if(mode==8)rate["unknown"]=1;
  if(mode==9)rate.erase("numerator");
  if(mode==10)rate["numerator"]="2";
  if(mode==11)rate["numerator"]=5;
  refuses([&]{decodeProject(bad.dump());});}
 check(history.undo(),"Rate restore missing");
 auto old=nlohmann::json::parse(encodeProject(s));old["schemaMinor"]=10;old["tracks"][0]["clips"][0].erase("playbackRate");old["tracks"][0]["clips"][0].erase("stretch");
 check(decodeProject(old.dump())==original,"Schema1.10 neutral rate migration differs");
 refuses([&]{history.structural({SetClipPlaybackRate{s.tracks[0].id,c.id,{1,2}},SetClipPlaybackRate{s.tracks[0].id,c.id,{5,1}}});});
 check(s==original,"Invalid grouped rate edit mutated project");
 s.tracks[0].clips[0].sourceFrame=a.frames-1;s.tracks[0].clips[0].sourceTiming={};s.tracks[0].clips[0].lengthFrames=1;
 applySessionEdits(s,{SetClipPlaybackRate{s.tracks[0].id,c.id,{1,4}}});
 check(s.tracks[0].clips[0].lengthFrames==4,"Slower endpoint did not stop at complete asset boundary");
 s.sampleRate=48000;s.assets[0].sampleRate=96000;s.tracks[0].clips[0].playbackRate={};s.tracks[0].clips[0].lengthFrames=1;
 applySessionEdits(s,{SetClipPlaybackRate{s.tracks[0].id,c.id,{1,4}}});
 check(s.tracks[0].clips[0].lengthFrames==2,"Slow playback invented samples past asset extent");
 auto duration=original;duration.sampleRate=48000;duration.assets[0].frames=INT64_MAX;
 auto &longClip=duration.tracks[0].clips[0];longClip.startFrame=0;longClip.sourceFrame=0;longClip.sourceTiming={};longClip.lengthFrames=INT64_MAX;longClip.processing={};
 const auto durationBefore=duration;refuses([&]{applySessionEdits(duration,{SetClipPlaybackRate{duration.tracks[0].id,c.id,{1,4}}});});check(duration==durationBefore,"Overflowing duration retime changed state");
 auto extreme=original;extreme.tracks[0].clips[0].processing.fadeIn={INT64_MIN+10,0,ClipFadeCurve::Linear,1};const auto untouched=extreme;
 refuses([&]{applySessionEdits(extreme,{SetClipPlaybackRate{s.tracks[0].id,c.id,{1,4}}});});check(extreme==untouched,"Overflowing fade retime changed state");
}
struct Temp{std::filesystem::path root=std::filesystem::temp_directory_path()/utf8Path("sc-rate-Κиїв-"+Id::generate().str());Temp(){std::filesystem::create_directory(root);}~Temp(){std::error_code e;std::filesystem::remove_all(root,e);}};
Session recorded(const std::filesystem::path &root,std::uint32_t source,std::uint32_t project,std::uint32_t channels,ClipPlaybackRate rate={}){
 auto s=makeOneTrackSession("Owned mixed rate","Track",source);auto &t=s.tracks[0];t.layout={channels==1?LayoutKind::Mono:channels==2?LayoutKind::Stereo:LayoutKind::Discrete,channels};t.eq.enabled=false;t.eq.bands.clear();
 CaptureConfig c;c.sampleRate=source;c.layout=t.layout;c.slabFrames=256;c.maximumCallbackFrames=256;CapturePipe pipe(c);RecordingSpec spec;spec.projectId=s.id;spec.trackId=t.id;spec.capture=pipe.config();
 CaptureWriter writer(root,spec);std::vector<float> samples(std::size_t(256)*channels);std::array<const float*,256> ptr{};for(std::uint32_t ch=0;ch<channels;++ch)ptr[ch]=samples.data()+std::size_t(ch)*256;
 for(Frame at=0;at<2048;at+=256){for(std::uint32_t ch=0;ch<channels;++ch)for(Frame f=0;f<256;++f)samples[std::size_t(ch)*256+std::size_t(f)]=float(2.5*std::sin(2*std::numbers::pi*(100+ch)*double(at+f)/source));
  check(pipe.push({ptr.data(),channels},256,at).acceptedFrames==256,"Owned rate capture lost frames");while(writer.drainOne(pipe)){} }
 pipe.finish();while(writer.drainOne(pipe)){}attachRecording(s,writer.finalize(pipe));s.sampleRate=project;
 auto &clip=s.tracks[0].clips[0];clip.sourceFrame=17;clip.sourceTiming={1,7};clip.startFrame=11;clip.lengthFrames=Frame(std::uint64_t(1800)*project/source);clip.processing.fadeIn={0,37,ClipFadeCurve::Linear,1};clip.processing.gainDb=3;
 applySessionEdits(s,{SetClipPlaybackRate{s.tracks[0].id,clip.id,rate}});
 s.exportEndFrame=s.tracks[0].clips[0].startFrame+s.tracks[0].clips[0].lengthFrames+19;ProjectStore(root).save(s);check(ProjectStore(root).load()==s,"Mixed-rate Save/reopen differs");return s;
}
std::vector<float> live(const std::filesystem::path &root,const Session&s,Frame first,Frame end,std::uint32_t block,std::uint32_t slab,ResourceLedger ledger){
 const auto channels=s.tracks[0].layout.channels;MixPlan plan;plan.output=s.tracks[0].layout;TrackMix lane{s.tracks[0].id,{}};for(std::uint32_t ch=0;ch<channels;++ch)lane.channels.push_back({ch,ch,1});plan.tracks.push_back(lane);
 MixPlaybackConfig c;c.graph.startFrame=first;c.endFrame=end;c.slabFrames=slab;c.graph.maximumFrames=block;c.graph.resources=ledger;MixPlayback mix(s,plan,c);ReadAheadOptions options;options.resources=ledger;options.cache.resources=ledger;MixReader reader(mix,root,s,options);
 std::vector<float> output(std::size_t(block)*channels),result(std::size_t(end-first)*channels);std::array<float*,256> ptr{};for(std::uint32_t ch=0;ch<channels;++ch)ptr[ch]=output.data()+std::size_t(ch)*block;
 for(Frame at=first;at<end;){while(reader.fillRound()){}const auto n=std::uint32_t(std::min<Frame>(block,end-at));MixPlaybackReport report;{rt_audit::Guard g;report=mix.process({ptr.data(),channels},n);}
  check(!report.missingTrackFrames && !report.staleTrackFrames && report.timelineFrames==n && report.mix.status==ProcessStatus::Ok,"Positioned live reader lost frames");
  for(std::uint32_t f=0;f<n;++f)for(std::uint32_t ch=0;ch<channels;++ch)result[std::size_t(at-first+f)*channels+ch]=ptr[ch][f];
  at+=n;}
 check(!reader.sanitizedSamples(),"Positioned reader sanitized finite samples");return result;
}
void workflow(std::pair<std::uint32_t,std::uint32_t> pair,std::uint32_t channels,ClipPlaybackRate rate={}){
 std::cout<<"positioned_workflow="<<pair.first<<":"<<pair.second<<" channels="<<channels<<std::endl;
 Temp temp;auto s=recorded(temp.root,pair.first,pair.second,channels,rate);auto root=temp.root;const auto hash=hashMediaFile(root/utf8Path(s.assets[0].relativePath));ResourceLedger ledger(128*1024*1024,"Positioned workflow");
 PlaybackConfig config;config.sampleRate=s.sampleRate;config.layout=s.tracks[0].layout;
 config.slabFrames=256;config.maximumCallbackFrames=127;config.endFrame=s.exportEndFrame;
 PlaybackPipe pipe(config);const ValidatedSession validated(s);
 const auto bytes=trackReaderPayloadBytes(validated,s.tracks[0].id,pipe.config());
 ResourceLedger shortGrant(bytes-1,"Positioned reader short grant");bool touched=false;
 ReadAheadOptions shortOptions;shortOptions.resources=shortGrant;
 shortOptions.beforeAdmissionRead=[&]{touched=true;};
 refuses([&]{TrackReader denied(pipe,root,s,s.tracks[0].id,shortOptions);});
 check(!touched && !shortGrant.usage().reservedBytes,"Reader short grant read media or retained payload");
 const auto end=s.exportEndFrame;auto expected=live(root,s,0,end,37,256,ledger);check(ledger.usage().reservedBytes==0,"Reader grant not retired");
 check(live(root,s,0,end,511,1024,ledger)==expected,"Callback/slab partitions changed positioned waveform");
 auto split=s;applySessionEdits(split,{SplitClip{s.tracks[0].id,s.tracks[0].clips[0].id,Id::generate(),130}});
 check(live(root,split,0,end,113,512,ledger)==expected,"Actual split changed waveform");
 const Frame seek=257;const auto suffix=live(root,s,seek,end,127,256,ledger);check(std::equal(suffix.begin(),suffix.end(),expected.begin()+seek*channels),"Prepared seek lacks correct phase/history");
 auto crop=s;applySessionEdits(crop,{CropClip{s.tracks[0].id,s.tracks[0].clips[0].id,48,37,s.tracks[0].clips[0].lengthFrames-37}});
 auto cropped=live(root,crop,48,end,113,256,ledger);check(std::equal(cropped.begin(),cropped.end(),expected.begin()+48*channels),"Crop changed source/envelope waveform");
 std::filesystem::create_directory(root/"exports");ExportSpec spec(s.tracks[0].id);spec.endFrame=end;spec.blockFrames=127;ExportOptions o;o.resources=ledger;
 auto result=exportTrackWav(root,s,root/"exports"/"rate.wav",spec,o);ApprovedMediaRoot approved(root,ledger);auto file=approved.open("exports/rate.wav",16*1024*1024);std::vector<float> decoded;
 validateApprovedWave(file,{}, {},[&](std::uint64_t,std::span<const double> values){for(auto x:values)decoded.push_back(float(x));});
 check(decoded==expected && result.frames==end && result.peak>1,"WAV export differs from live rate/headroom");
 check(hashMediaFile(root/utf8Path(s.assets[0].relativePath))==hash && ProjectStore(root).load()==s,"Timing changed raw bytes/original project");
 }
void workflows(){for(auto pair:{std::pair{48000u,44100u},std::pair{44100u,48000u},std::pair{96000u,48000u},std::pair{48000u,48000u}})
 for(auto channels:{1u,2u,8u,32u,256u})workflow(pair,channels);}
void rateWorkflows(){for(auto pair:{std::pair{48000u,44100u},std::pair{44100u,48000u}})
 for(auto channels:{1u,2u,8u})for(auto rate:{ClipPlaybackRate{1,4},ClipPlaybackRate{1333,1000},ClipPlaybackRate{4,1}}){
 std::cout<<"clip_speed="<<rate.numerator<<"/"<<rate.denominator<<std::endl;workflow(pair,channels,rate);}}
}
int main(int argc,char**argv){try{
 if((argc==9 || argc==11) && std::string_view(argv[1])=="--fraction-probe"){
  auto read=[]<class T>(const char*s,T &n){std::string_view t(s);auto p=std::from_chars(t.data(),t.data()+t.size(),n);if(p.ec!=std::errc{} || p.ptr!=t.data()+t.size())throw std::runtime_error("Invalid fraction probe");};
  std::uint32_t source,project;Frame frame,offset,translated;std::uint64_t fraction,denominator;
  read(argv[2],source);read(argv[3],project);read(argv[4],frame);read(argv[5],fraction);read(argv[6],denominator);read(argv[7],offset);read(argv[8],translated);
  ClipPlaybackRate rate;if(argc==11){read(argv[9],rate.numerator);read(argv[10],rate.denominator);}
  auto p=SourceFrameMap(source,project,SourcePosition{frame,fraction,denominator},rate).translated(translated).at(offset);
  std::cout<<"{\"frame\":"<<p.frame<<",\"fraction\":"<<p.fraction<<",\"denominator\":"<<p.denominator<<"}\n";return 0;}
 if(argc==2 && std::string_view(argv[1])=="--rate-only") {rt_audit::reset();rateKernels();rateState();rateWorkflows();auto c=rt_audit::counts;check(!c.cppAllocate && !c.cppFree && !c.cAllocate && !c.cFree && !c.blockingLock,"Rate callback allocated/freed/locked");std::cout<<"PASS: "<<checks<<" linked clip playback-rate checks; independent pitch/stretch unqualified\n";return 0;}
 const bool kernelOnly=argc==2 && std::string_view(argv[1])=="--kernel-state-only";
 const bool workflowOnly=argc==2 && std::string_view(argv[1])=="--workflows-only";
 if(argc!=1 && !kernelOnly && !workflowOnly)throw std::runtime_error("Unknown positioned arguments");
 rt_audit::reset();
 const auto began=std::chrono::steady_clock::now();
 auto progress=[&](const char *stage){std::cout<<"positioned_stage="<<stage<<" elapsed_ms="
   <<std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now()-began).count()<<std::endl;};
 progress("start");
 if(!workflowOnly){kernels();progress("kernels");state();progress("state");}
 if(!kernelOnly){workflows();progress("workflows");}
 auto c=rt_audit::counts;check(!c.cppAllocate && !c.cppFree && !c.cAllocate && !c.cFree && !c.blockingLock,"Live callback allocated/freed/locked");
 std::cout<<"PASS: "<<checks<<" positioned "<<(kernelOnly ? "kernel/state" : workflowOnly ? "owned-WAV live/export" : "complete kernel/state/owned-WAV")
          <<" checks. Native audio, quality parity, rate automation and independent pitch/stretch remain unqualified\n";return 0;
 }catch(const std::exception&e){std::cerr<<e.what()<<'\n';return 1;}}
