// SPDX-License-Identifier: GPL-3.0-only
#include <soundcurrent/clip_processing.hpp>
#include <soundcurrent/export.hpp>
#include <soundcurrent/mix_reader.hpp>
#include <soundcurrent/recording.hpp>
#include <soundcurrent/wave_validation.hpp>
#include <nlohmann/json.hpp>
#include "rt_audit.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <iostream>
#include <limits>
#include <numbers>
using namespace soundcurrent::daw;
namespace {
std::uint64_t checks = 0;
void check(bool ok,const char *why) {++checks;if(!ok) throw std::runtime_error(why);}
template<class F> void refuses(F f) {
    try {f();} catch(const ProjectError &) {++checks;return;}
    throw std::runtime_error("Invalid clip processing accepted");
}
void mathAndAnchors() {
    PreparedClipProcessing flat;
    check(flat.unity() && flat.gainAt(0)==1 && flat.gainAt(INT64_MAX)==1,"Neutral clip processing differs");
    const std::array<double,5> linear{0,.25,.5,.75,1};
    const std::array<double,5> smooth{0,.15625,.5,.84375,1};
    for(const auto curve:{ClipFadeCurve::Linear,ClipFadeCurve::EqualPower,ClipFadeCurve::Smoothstep}) {
        ClipProcessing in,out;in.fadeIn={7,12,curve,1};out.fadeOut=in.fadeIn;
        PreparedClipProcessing a(in),b(out);
        check(a.gainAt(6)==0 && a.gainAt(12)==1 && b.gainAt(6)==1 && b.gainAt(12)==0,"Fade support/endpoints differ");
        for(std::size_t j=0;j<linear.size();++j) {
            const auto expected=curve==ClipFadeCurve::Linear ? linear[j] :
                curve==ClipFadeCurve::Smoothstep ? smooth[j] : std::sin(std::numbers::pi*linear[j]/2);
            check(std::abs(a.gainAt(Frame(j)+7)-expected)<2e-15,"Independent fade knot differs");
        }
        for(const auto shape:{.25,.5,1.,2.,4.}) {
            in.fadeIn={0,257,curve,shape};out.fadeOut=in.fadeIn;
            PreparedClipProcessing entering(in),leaving(out);
            double prior=0;
            for(Frame f=0;f<257;++f) {
                double x,y;
                {rt_audit::Guard g;x=entering.gainAt(f);y=leaving.gainAt(f);}
                check(x>=prior && x<=1 && x>=0 && y>=0 && y<=1,"Fade monotonicity/bounds failed");
                prior=x;
                if(shape==1 && curve!=ClipFadeCurve::Smoothstep) {
                    const auto invariant=curve==ClipFadeCurve::Linear ? x+y : x*x+y*y;
                    check(std::abs(invariant-1)<4e-15,"Crossfade unity/power invariant failed");
                }
            }
        }
    }
    ClipProcessing singleton;singleton.fadeIn={3,4,ClipFadeCurve::Linear,1};
    check(PreparedClipProcessing(singleton).gainAt(3)==.5,"Single-frame fade midpoint differs");
    ClipProcessing p;p.gainDb=20*std::log10(2.5);p.polarityInverted=true;
    check(std::abs(PreparedClipProcessing(p).gainAt(20)+2.5)<1e-14,"Gain/polarity lost headroom");
    p.muted=true;check(PreparedClipProcessing(p).gainAt(20)==0,"Clip mute failed");p.muted=false;
    p.fadeIn={-13,67,ClipFadeCurve::EqualPower,.75};p.fadeOut={190,512,ClipFadeCurve::Smoothstep,1.5};
    PreparedClipProcessing original(p);shiftClipProcessing(p,113);PreparedClipProcessing right(p);
    for(Frame f=0;f<399;++f) check(right.gainAt(f)==original.gainAt(f+113),"Shifted fade changed sample gain");
    const auto stable=p;
    p.fadeIn={INT64_MIN,-1,ClipFadeCurve::Linear,1};validateClipProcessing(p);
    const auto extreme=p;refuses([&]{shiftClipProcessing(p,1);});check(p==extreme,"Anchor refusal partially changed processing");
    p=stable;
    for(unsigned mode=0;mode<10;++mode) {
        auto bad=p;
        if(mode==0) bad.gainDb=61;
        if(mode==1) bad.gainDb=-121;
        if(mode==2) bad.gainDb=std::numeric_limits<double>::quiet_NaN();
        if(mode==3) bad.gainDb=std::numeric_limits<double>::infinity();
        if(mode==4) bad.fadeIn.shape=.24;
        if(mode==5) bad.fadeOut.shape=4.01;
        if(mode==6) bad.fadeIn.curve=ClipFadeCurve(19);
        if(mode==7) bad.fadeIn={1,1,ClipFadeCurve::Linear,1};
        if(mode==8) bad.fadeIn={INT64_MIN,INT64_MAX,ClipFadeCurve::Linear,1};
        if(mode==9) bad.fadeIn.shape=std::numeric_limits<double>::quiet_NaN();
        refuses([&]{PreparedClipProcessing invalid(bad);});
    }
}
Session canonical() {
    auto s=makeOneTrackSession("Clip processing — Українська","Raw");
    Asset a;a.relativePath="media/owned.wav";a.sha256=std::string(64,'a');a.frames=1000;s.assets.push_back(a);
    Clip c;c.assetId=a.id;c.startFrame=17;c.sourceFrame=5;c.lengthFrames=512;
    c.processing.gainDb=.12345678901234567;
    c.processing.fadeIn={-13,67,ClipFadeCurve::EqualPower,.7123456789012345};
    c.processing.fadeOut={384,512,ClipFadeCurve::Smoothstep,1.23456789012345};
    s.tracks[0].clips.push_back(c);return s;
}
void stateAndEdits() {
    auto s=canonical();const auto original=s;
    auto j=nlohmann::json::parse(encodeProject(s));
    check(j["schemaMinor"]==11 && decodeProject(j.dump())==s,"Clip processing exact state roundtrip failed");
    for(unsigned mode=0;mode<14;++mode) {
        auto bad=j;auto &c=bad["tracks"][0]["clips"][0];auto &p=c["processing"];
        if(mode==0) p["gainDb"]=true;
        if(mode==1) p["gainDb"]=61;
        if(mode==2) p["muted"]=1;
        if(mode==3) p["polarityInverted"]="false";
        if(mode==4) p["fadeIn"]["startFrame"]=.5;
        if(mode==5) p["fadeOut"]["endFrame"]=UINT64_MAX;
        if(mode==6) p["fadeIn"]["shape"]=nullptr;
        if(mode==7) p["fadeIn"]["curve"]="vendor-unknown";
        if(mode==8) p["fadeOut"]["unknown"]=0;
        if(mode==9) p.erase("muted");
        if(mode==10) c.erase("processing");
        if(mode==11) p["fadeIn"]["shape"]=false;
        if(mode==12) p["fadeIn"]={};
        if(mode==13) p["futureProcessor"]=true;
        refuses([&]{decodeProject(bad.dump());});
    }
    for(unsigned minor=0;minor<9;++minor) {
        auto legacy=j;legacy["schemaMinor"]=minor;
        if(minor<8) legacy.erase("imports");
        if(minor<4) legacy.erase("punchRecording");
        if(minor<3) legacy.erase("master");
        for(auto &t:legacy["tracks"]) {
            for(auto &c:t["clips"]) {c.erase("processing");c.erase("sourceTiming");c.erase("playbackRate");}
            if(minor<5) t.erase("inputLatencyFrames");
            if(minor<2) t.erase("monitoringMode");
            if(minor==0) {t.erase("monitorIntent");t["inputIntent"].erase("ports");t["outputIntent"].erase("ports");}
        }
        auto expected=s;expected.tracks[0].clips[0].processing={};
        check(decodeProject(legacy.dump())==expected,"Old clip schema invented processing");
        legacy["tracks"][0]["clips"][0]["processing"]=j["tracks"][0]["clips"][0]["processing"];
        refuses([&]{decodeProject(legacy.dump());});
    }
    const auto track=s.tracks[0].id,clip=s.tracks[0].clips[0].id;
    EditHistory history(s);auto p=s.tracks[0].clips[0].processing;p.gainDb=6;p.muted=true;
    check(history.structural({SetClipProcessing{track,clip,p}}),"Clip processing edit missing");
    check(history.undo() && s==original && history.redo() && s.tracks[0].clips[0].processing==p,"Clip processing Undo/Redo differs");
    check(history.undo(),"Processing restore failed");
    auto bad=p;bad.fadeIn.shape=100;
    refuses([&]{history.structural({SetClipProcessing{track,clip,p},SetClipProcessing{track,clip,bad}});});
    check(s==original,"Refused grouped processing partly changed state");
    const PreparedClipProcessing envelope(s.tracks[0].clips[0].processing);
    const auto rightId=Id::generate();history.structural({SplitClip{track,clip,rightId,130}});
    const auto &left=s.tracks[0].clips[0];const auto &right=s.tracks[0].clips[1];
    check(left.lengthFrames==113 && right.sourceFrame==118 && right.processing.fadeIn.startFrame==-126,"Split anchors/extents differ");
    PreparedClipProcessing split(right.processing);
    for(Frame f=0;f<right.lengthFrames;++f) check(split.gainAt(f)==envelope.gainAt(f+113),"Split changed envelope samples");
    check(history.undo() && s==original,"Split processing Undo differs");
    history.structural({SetClipRange{track,clip,54,42,475}});
    PreparedClipProcessing trim(s.tracks[0].clips[0].processing);
    for(Frame f=0;f<475;++f) check(trim.gainAt(f)==envelope.gainAt(f+37),"Trim restarted fade");
    check(history.undo() && s==original,"Trim processing Undo differs");
}
struct Temp {
    std::filesystem::path root=std::filesystem::temp_directory_path()/utf8Path("sc-clip-Κиїв-"+Id::generate().str());
    Temp(){std::filesystem::create_directory(root);}
    ~Temp(){std::error_code e;std::filesystem::remove_all(root,e);}
};
float sample(Frame frame,std::uint32_t channel) {
    return float((int(frame%17)-8)*.25+double(channel%7)*.125);
}
Session recorded(const std::filesystem::path &root,std::uint32_t channels) {
    auto s=makeOneTrackSession("Owned clip render","Track");
    auto &t=s.tracks[0];t.layout={channels==1 ? LayoutKind::Mono : channels==2 ? LayoutKind::Stereo : LayoutKind::Discrete,channels};
    t.eq.enabled=false;
    CaptureConfig c;c.layout=t.layout;c.slabFrames=256;c.maximumCallbackFrames=256;
    CapturePipe pipe(c);RecordingSpec spec;spec.projectId=s.id;spec.trackId=t.id;spec.capture=pipe.config();
    CaptureWriter writer(root,spec);std::vector<float> input(std::size_t(channels)*256);
    std::array<const float *,256> ptr{};for(std::uint32_t ch=0;ch<channels;++ch) ptr[ch]=input.data()+std::size_t(ch)*256;
    for(Frame at=0;at<1024;at+=256) {
        for(std::uint32_t ch=0;ch<channels;++ch) for(Frame f=0;f<256;++f) input[std::size_t(ch)*256+std::size_t(f)]=sample(at+f,ch);
        check(pipe.push({ptr.data(),channels},256,at).acceptedFrames==256,"Owned capture fixture lost samples");
        while(writer.drainOne(pipe)){}
    }
    pipe.finish();while(writer.drainOne(pipe)){}
    attachRecording(s,writer.finalize(pipe));
    auto &first=s.tracks[0].clips[0];first.sourceFrame=11;first.lengthFrames=512;
    first.processing.gainDb=20*std::log10(2.);first.processing.fadeIn={0,65,ClipFadeCurve::Linear,1};
    first.processing.fadeOut={384,512,ClipFadeCurve::Linear,1};
    auto second=first;second.id=Id::generate();second.startFrame=97;second.sourceFrame=13;second.lengthFrames=400;
    second.processing.gainDb=20*std::log10(.5);second.processing.polarityInverted=true;
    second.processing.fadeIn={0,129,ClipFadeCurve::EqualPower,1};second.processing.fadeOut={};
    s.tracks[0].clips.push_back(second);s.exportEndFrame=600;
    ProjectStore(root).save(s);check(ProjectStore(root).load()==s,"Processed recording save/reopen differs");return s;
}
std::vector<float> live(const std::filesystem::path &root,const Session &s,std::uint32_t block,ResourceLedger memory) {
    const auto channels=s.tracks[0].layout.channels;
    MixPlan plan;plan.output=s.tracks[0].layout;TrackMix lane{s.tracks[0].id,{}};
    for(std::uint32_t ch=0;ch<channels;++ch)
        lane.channels.push_back({ch,ch,1});
    plan.tracks.push_back(lane);
    MixPlaybackConfig config;config.endFrame=600;config.slabFrames=256;config.graph.maximumFrames=block;config.graph.resources=memory;
    MixPlayback mix(s,plan,config);ReadAheadOptions o;o.resources=memory;o.cache.resources=memory;
    MixReader reader(mix,root,s,o);std::vector<float> output(std::size_t(channels)*block),result(std::size_t(channels)*600);
    std::array<float *,256> ptr{};for(std::uint32_t ch=0;ch<channels;++ch) ptr[ch]=output.data()+std::size_t(ch)*block;
    for(Frame at=0;at<600;) {
        while(reader.fillRound()){}
        const auto count=std::uint32_t(std::min<Frame>(block,600-at));MixPlaybackReport report;
        {rt_audit::Guard g;report=mix.process({ptr.data(),channels},count);}
        check(!report.missingTrackFrames && !report.staleTrackFrames && report.timelineFrames==count &&
              report.mix.status==ProcessStatus::Ok,"Shared reader/live processing lost frames");
        for(std::uint32_t f=0;f<count;++f) for(std::uint32_t ch=0;ch<channels;++ch)
            result[std::size_t(at+f)*channels+ch]=ptr[ch][f];
        at+=count;
    }
    check(reader.sanitizedSamples()==0,"Finite processed clips were sanitized");return result;
}
void renderWorkflow() {
    for(const auto channels:{1u,2u,8u,32u,256u}) {
        Temp temp;const auto s=recorded(temp.root,channels);const auto originalHash=hashMediaFile(temp.root/utf8Path(s.assets[0].relativePath));
        ResourceLedger memory(128*1024*1024,"Clip render fixture");
        const auto first=live(temp.root,s,37,memory),other=live(temp.root,s,511,memory);
        check(first==other,"Live callback partitions changed clip processing");
        for(Frame f=0;f<600;++f) for(std::uint32_t ch=0;ch<channels;++ch) {
            double expected=0;
            if(f<512) {
                const double in=f<=64 ? double(f)/64 : 1;
                const double out=f<384 ? 1 : double(511-f)/127;
                expected=double(sample(f+11,ch))*2*in*out;
            }
            if(f>=97 && f<497) {
                const auto local=f-97;
                const double in=local<=128 ? std::sin(std::numbers::pi*double(local)/256) : 1;
                expected-=double(sample(local+13,ch))*.5*in;
            }
            check(std::abs(double(first[std::size_t(f)*channels+ch])-expected)<5e-7,"Independent overlapped gain/fade render differs");
        }
        std::filesystem::create_directory(temp.root/"exports");ExportSpec spec(s.tracks[0].id);spec.endFrame=600;spec.blockFrames=127;
        ExportOptions options;options.resources=memory;
        const auto exported=exportTrackWav(temp.root,s,temp.root/"exports"/"processed.wav",spec,options);
        ApprovedMediaRoot approved(temp.root,memory);auto file=approved.open("exports/processed.wav",1024*1024);
        std::vector<float> samples;
        validateApprovedWave(file,{}, {},[&](std::uint64_t,std::span<const double> values){
            for(const auto x:values) samples.push_back(float(x));
        });
        check(samples==first && exported.peak>1 && exported.frames==600,"Offline WAV differs from live clip rendering/headroom");
        auto split=s;const auto id=s.tracks[0].clips[0].id;
        applySessionEdits(split,{SplitClip{s.tracks[0].id,id,Id::generate(),113}});
        check(live(temp.root,split,113,memory)==first,"Actual clip split changed audible samples");
        check(hashMediaFile(temp.root/utf8Path(s.assets[0].relativePath))==originalHash && ProjectStore(temp.root).load()==s,
              "Clip processing modified raw media/saved input");
    }
}
}
int main(){try {
    rt_audit::reset();mathAndAnchors();stateAndEdits();renderWorkflow();
    const auto counts=rt_audit::counts;
    check(!counts.cppAllocate && !counts.cppFree && !counts.cAllocate && !counts.cFree && !counts.blockingLock,
          "Prepared evaluation/live callback allocated, freed or blocked");
    std::cout<<"PASS: "<<checks<<" clip-processing checks; neutral/migrations, curves/crossfade invariants, split/trim exactness, Undo/Redo, owned 1/2/8/32/256-channel live/offline WAV/headroom; no audio device, rate/pitch or source-suite conversion qualification\n";
    return 0;
}catch(const std::exception &e){std::cerr<<e.what()<<'\n';return 1;}}
