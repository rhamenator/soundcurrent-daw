// SPDX-License-Identifier: GPL-3.0-only
#include <soundcurrent/playback_timeline.hpp>
#include <soundcurrent/manual_recording.hpp>
#include <soundcurrent/export.hpp>
#include <soundcurrent/wave_validation.hpp>
#include "rt_audit.hpp"
#include <algorithm>
#include <array>
#include <chrono>
#include <iostream>
#include <limits>
#include <thread>
using namespace soundcurrent::daw;
namespace {
unsigned checks=0;
void check(bool ok,const char *why){++checks;if(!ok)throw std::runtime_error(why);}
template<class F>void refusal(F fn){try{fn();}catch(const ProjectError&){++checks;return;}throw std::runtime_error("Invalid loop accepted");}
void timing() {
    for(Frame length:{1,2,7,53,256})for(Frame origin:{0,137}) {
        PlaybackTimeline clock(origin,origin+length*5+length/2,PlaybackLoopRegion{102,102+length});
        Frame stream=origin;
        for(unsigned block:{1u,13u,256u}) {
            stream=origin;
            while(stream<origin+length*5+length/2) {
                PlaybackTimelineSegment segment;
                {rt_audit::Guard guard;check(clock.segment(stream,block,segment),"Loop segment unavailable");}
                check(segment.frames>0&&segment.frames<=block,"Empty/oversized loop segment");
                for(unsigned f=0;f<segment.frames;++f)
                    check(segment.projectFrame+f==102+(stream-origin+f)%length,"Independent frame enumeration differs");
                check(segment.pass==std::uint64_t((stream-origin)/length)&&segment.wraps==(segment.projectFrame+segment.frames==102+length),"Loop pass/wrap differs");
                stream+=segment.frames;
            }
        }
        for(unsigned pass=0;pass<5+(length>1);++pass) {
            Frame a=-1,b=-1;check(clock.passRange(pass,a,b)&&a==origin+pass*length&&b==std::min(origin+length*5+length/2,a+length),"Loop pass range differs");
        }
        Frame a=111,b=222;check(!clock.passRange(6,a,b)&&a==111&&b==222,"Refused pass changed outputs");
    }
    const auto max=std::numeric_limits<Frame>::max();
    PlaybackTimeline edge(max-11,max,PlaybackLoopRegion{max-2,max});
    PlaybackTimelineSegment v;check(edge.segment(max-1,256,v)&&v.projectFrame==max-2&&v.frames==1,"Near-limit mapping overflow");
    Frame a=0,b=0;std::uint64_t pass=0;check(edge.position(max,a,pass)&&a==max-1&&pass==5,"Terminal position overflow");
    check(!edge.passRange(UINT64_MAX,a,b),"Pass overflow accepted");
    refusal([]{PlaybackTimeline c(-1,10);});refusal([]{PlaybackTimeline c(0,10,PlaybackLoopRegion{7,7});});
    refusal([]{PlaybackTimeline c(0,10,PlaybackLoopRegion{-1,7});});
}
struct Directory {
    std::filesystem::path root=std::filesystem::temp_directory_path()/utf8Path("sc-loop-Україна-"+Id::generate().str());
    Directory(){std::filesystem::create_directories(root/"exports");}
    ~Directory(){if(std::uncaught_exceptions()){std::cerr<<"preserved_loop_project="<<root<<'\n';return;}std::error_code e;std::filesystem::remove_all(root,e);}
};
float source(Frame f){return float((f%31-15)*.125);}
float captured(Frame f,unsigned lane){return float((f%73-36)*.03125+lane*.25);}
Session original(const std::filesystem::path &root,bool nonflat=false,unsigned physicalRate=48000) {
    auto s=makeOneTrackSession("Loop — Ελληνικά","Backing",physicalRate);s.tracks.push_back(makeAudioTrack("Second",{},s.sampleRate));
    for(auto &t:s.tracks){t.eq.enabled=nonflat;t.eq.bands.resize(1);t.eq.bands[0].frequencyHz=733;t.eq.bands[0].gainDb=5;}
    CaptureConfig c;c.sampleRate=physicalRate;c.maximumCallbackFrames=256;c.slabFrames=256;CapturePipe pipe(c);
    RecordingSpec spec;spec.projectId=s.id;spec.trackId=s.tracks[0].id;spec.capture=pipe.config();CaptureWriter writer(root,spec);
    std::array<float,256> data{};const float *ptr=data.data();
    for(Frame at=0;at<512;at+=256){for(unsigned f=0;f<256;++f)data[f]=source(at+f);check(pipe.push({&ptr,1},256,at).acceptedFrames==256,"Backing capture failed");while(writer.drainOne(pipe)){}}
    pipe.finish();while(writer.drainOne(pipe)){}attachRecording(s,writer.finalize(pipe));ProjectStore(root).save(s);return s;
}
MixPlan plan(const Session &s){return {{LayoutKind::Stereo,2},{{s.tracks[0].id,{{0,0,1}}},{s.tracks[1].id,{{0,1,1}}}}};}
MixPlaybackConfig config(Frame period=53){MixPlaybackConfig c;c.graph.startFrame=200;c.graph.maximumFrames=256;c.graph.generation=43;c.endFrame=200+5*period+29;c.slabFrames=256;c.loop=PlaybackLoopRegion{102,102+period};return c;}
std::vector<float> render(const std::filesystem::path &root,const Session &s,unsigned partition,Frame period=53) {
    auto c=config(period);MixPlayback mix(s,plan(s),c);MixReader reader(mix,root,s);
    PreparedMixGraph oracle(s,plan(s),c.graph);
    auto changed=s;changed.tracks[0].eq.bands[0].gainDb=-3;
    const ParameterAddress address{s.tracks[0].id,s.tracks[0].eq.id,s.tracks[0].eq.bands[0].id,BandParameter::GainDb};
    if(s.tracks[0].eq.enabled){check(mix.graph().submit(mix.graph().parameterEvent(changed,address,249))==SubmitStatus::Accepted&&oracle.submit(oracle.parameterEvent(changed,address,249))==SubmitStatus::Accepted,"Wrap-spanning EQ event refused");}
    std::array<std::array<float,256>,2> out{},input{},expected{};std::array<float*,2> pointers{out[0].data(),out[1].data()},ep{expected[0].data(),expected[1].data()};
    std::array<const float*,2> ip{input[0].data(),input[1].data()};std::array<MixInput,2> planes{{{&ip[0],1},{&ip[1],1}}};std::vector<float> result;
    for(Frame at=c.graph.startFrame;at<c.endFrame;) {
        while(reader.fillRound()){}const auto n=unsigned(std::min<Frame>(partition,c.endFrame-at));
        for(unsigned f=0;f<n;++f){input[0][f]=source(102+(at-200+f)%period);input[1][f]=0;}
        MixPlaybackReport r;MixReport e;
        {rt_audit::Guard guard;r=mix.process(pointers,n);e=oracle.process(planes,ep,n);}
        check(r.startFrame==at&&r.projectStartFrame==102+(at-200)%period&&r.loopPass==std::uint64_t((at-200)/period)&&r.timelineFrames==n&&!r.missingTrackFrames&&!r.staleTrackFrames&&e.status==ProcessStatus::Ok,"Loop clock/prefetch lost data");
        for(unsigned f=0;f<n;++f)for(unsigned ch=0;ch<2;++ch){check(out[ch][f]==expected[ch][f],"Loop reset DSP or changed independent source samples");result.push_back(out[ch][f]);}at+=n;
    }
    return result;
}
std::vector<float> exported(const std::filesystem::path &root,const Session &s) {
    MixExportSpec spec(plan(s));spec.startFrame=200;spec.endFrame=config().endFrame;spec.blockFrames=37;spec.loop=config().loop;
    auto result=exportMixWav(root,s,root/"exports"/(Id::generate().str()+".wav"),spec);
    check(result.processingStartFrame==200&&result.frames==294,"Loop export changed stream origin/length");
    ApprovedMediaRoot approved(root,ResourceLedger(8*1024*1024,"Loop export audit"));auto file=approved.open("exports/"+result.destination.filename().string(),1024*1024);std::vector<float> values;
    validateApprovedWave(file,{}, {},[&](std::uint64_t,std::span<const double> block){for(auto f:block)values.push_back(float(f));});return values;
}
void playback() {
    Directory d;auto s=original(d.root);const auto hash=hashMediaFile(d.root/utf8Path(s.assets[0].relativePath));
    const auto first=render(d.root,s,13);check(first==render(d.root,s,256)&&first==exported(d.root,s),"Loop partitions/export differ");
    for(Frame period:{1,2,7,256})check(render(d.root,s,17,period)==render(d.root,s,256,period),"Short/inblock wraps differ");
    auto changed=s;changed.tracks[0].eq.enabled=true;check(render(d.root,changed,13)==render(d.root,changed,256),"Continuous EQ/ramp differs at loop wrap");
    auto cfg=config();cfg.graph.memoryBudgetBytes=1;refusal([&]{MixPlayback rejected(s,plan(s),cfg);});
    check(hashMediaFile(d.root/utf8Path(s.assets[0].relativePath))==hash,"Loop playback changed backing file");
}
// A distant stream origin must admit project clips, including fractional
// physical-rate reads and overlapping fades; compare against one linear lap.
std::vector<float> readRange(const std::filesystem::path &root,const Session &s,
                             MixPlaybackConfig cfg,unsigned block) {
    MixPlayback mix(s,plan(s),cfg);MixReader reader(mix,root,s);
    std::array<std::array<float,256>,2> output{};
    std::array<float*,2> ptr{output[0].data(),output[1].data()};
    std::vector<float> samples;
    while(mix.position()<cfg.endFrame) {
        while(reader.fillRound()){}
        const auto n=unsigned(std::min<Frame>(block,cfg.endFrame-mix.position()));
        MixPlaybackReport r;{rt_audit::Guard guard;r=mix.process(ptr,n);}
        check(!r.missingTrackFrames&&!r.staleTrackFrames&&r.timelineFrames==n,
              "Mixed-rate loop read-ahead lost a segment");
        for(unsigned f=0;f<n;++f)for(unsigned ch=0;ch<2;++ch)samples.push_back(output[ch][f]);
    }
    return samples;
}
void clipGeometry() {
    Directory d;auto s=original(d.root,false,44100);s.sampleRate=48000;
    auto &clip=s.tracks[0].clips[0];clip.sourceFrame=17;clip.sourceTiming={1,7};
    clip.processing.gainDb=3;clip.processing.fadeIn={95,135,ClipFadeCurve::Linear,1};
    clip.processing.fadeOut={137,173,ClipFadeCurve::Linear,1};
    auto overlap=clip;overlap.id=Id::generate();overlap.startFrame=117;
    overlap.lengthFrames=40;overlap.processing.gainDb=-2;
    overlap.processing.fadeIn={0,20,ClipFadeCurve::Linear,1};
    overlap.processing.fadeOut={20,40,ClipFadeCurve::Linear,1};
    s.tracks[0].clips.push_back(overlap);validate(s);
    MixPlaybackConfig linear;linear.graph.startFrame=102;linear.graph.maximumFrames=256;
    linear.endFrame=155;linear.slabFrames=256;
    const auto lap=readRange(d.root,s,linear,13);
    auto loop=config();loop.graph.startFrame=50000;loop.endFrame=50294;
    const auto result=readRange(d.root,s,loop,17);
    check(result==readRange(d.root,s,loop,256),"Fractional/fade loop partitions differ");
    for(unsigned n=0;n<result.size();++n)
        check(result[n]==lap[n%lap.size()],"Loop did not repeat original fractional/fade geometry");
    MixExportSpec spec(plan(s));spec.startFrame=50000;spec.endFrame=50294;
    spec.blockFrames=37;spec.loop=loop.loop;
    const auto exported=exportMixWav(d.root,s,d.root/"exports"/"fractional.wav",spec);
    ApprovedMediaRoot root(d.root,ResourceLedger(8*1024*1024,"Fractional loop export"));
    auto file=root.open("exports/fractional.wav",1024*1024);std::vector<float> pcm;
    validateApprovedWave(file,{}, {},[&](std::uint64_t,std::span<const double> block){for(auto v:block)pcm.push_back(float(v));});
    check(pcm==result&&exported.frames==294,"Fractional/fade loop export differs");
}
void record() {
    Directory d;auto s=original(d.root);const auto initial=s;auto cfg=config();
    ManualRecordingOptions o;o.playback=cfg;o.capture.maximumCallbackFrames=256;o.capture.slabFrames=256;o.nativeInputs=2;o.backend=CaptureBackend::Synthetic;
    std::vector<ManualRecordingArm> arms;
    for(unsigned lane=0;lane<2;++lane)arms.push_back({{s.tracks[lane].id,{lane},lane?29:7,RecordingMonitor::Off},{}});
    ManualRecordingRun run(d.root,s,plan(s),arms,o);std::array<std::uint64_t,5> ids{};
    for(unsigned pass=0;pass<5;++pass){ids[pass]=run.prepareTake();check(run.submit({ManualPunchAction::In,200+pass*53,43,pass*2+1,ids[pass]})==ManualPunchSubmit::Accepted&&run.submit({ManualPunchAction::Out,200+(pass+1)*53,43,pass*2+2,0})==ManualPunchSubmit::Accepted,"Loop pass scheduling refused");}
    std::array<std::array<float,256>,2> inputs{},outputs{};std::array<const float*,2> in{inputs[0].data(),inputs[1].data()};std::array<float*,2> out{outputs[0].data(),outputs[1].data()};
    DeviceBlockClock clock{1000000,17,20000000000ULL,17,1,1,48000,777};unsigned groups=0,replies=0;std::vector<std::string> hashes;
    for(Frame at=200;at<cfg.endFrame;) {
        const auto n=unsigned(std::min<Frame>(17,cfg.endFrame-at));clock.duration=n;
        for(unsigned f=0;f<n;++f)for(unsigned lane=0;lane<2;++lane)inputs[lane][f]=captured(at+f-(lane?29:7),lane);
        DuplexStatus status;{rt_audit::Guard guard;status=run.process(clock,in,out,256);}
        check(status==DuplexStatus::Running||status==DuplexStatus::Complete,"Loop capture failed");
        for(unsigned f=0;f<n;++f)check(outputs[0][f]==source(102+(at-200+f)%53)&&outputs[1][f]==0,"Backing loop differs during recording");
        run.service();ManualPunchReceipt receipt;while(run.acknowledgement(receipt)){check(receipt.result==ManualPunchResult::Applied,"Loop boundary not applied");++replies;}
        at+=n;clock.position+=n;clock.monotonicNs+=std::uint64_t(n)*1000000000ULL/s.sampleRate;++clock.cycle;
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    run.stop();run.checkError();run.checkReader();ManualPunchReceipt receipt;while(run.acknowledgement(receipt)){check(receipt.result==ManualPunchResult::Applied,"Terminal loop command failed");++replies;}
    ManualRecordedGroup group;
    while(run.takeGroup(group)) {
        auto it=std::find(ids.begin(),ids.end(),group.take);check(it!=ids.end()&&group.complete()&&group.lanes.size()==2,"Loop complete group lost");const auto pass=unsigned(it-ids.begin());
        check(group.beginFrame==200+pass*53&&group.endFrame==200+(pass+1)*53,"Loop journal origin became project time");
        for(unsigned lane=0;lane<2;++lane){const auto &r=group.lanes[lane];check(r.origin&&r.origin->devicePosition==1000000+std::uint64_t(group.beginFrame-200+(lane?29:7)),"Device origin wrapped");
            ApprovedMediaRoot approved(d.root,ResourceLedger(8*1024*1024,"Loop raw audit"));auto file=approved.open(r.result->asset.relativePath,1024*1024);std::vector<float> raw;
            validateApprovedWave(file,{}, {},[&](std::uint64_t,std::span<const double> block){for(auto f:block)raw.push_back(float(f));});check(raw.size()==53,"Loop raw length differs");for(unsigned f=0;f<raw.size();++f)check(raw[f]==captured(group.beginFrame+f,lane),"Delayed loop raw sample duplicated/lost");}
        s=withManualTakeLanes(s,group,"Pass "+std::to_string(pass+1),102);++groups;
    }
    check(groups==5&&replies==10&&!run.occupiedSlots()&&s.tracks[0].takeLanes.size()==5,"Loop groups/replies/retirement differ");
    for(const auto &a:s.assets)hashes.push_back(hashMediaFile(d.root/utf8Path(a.relativePath)));
    EditHistory history(s);history.structural({SelectTakeRegion{s.tracks[0].id,s.tracks[0].takeLanes[2].id,102,155},SelectTakeRegion{s.tracks[1].id,s.tracks[1].takeLanes[2].id,102,155}});const auto comp=s;
    check(history.undo()&&history.redo()&&s==comp,"Loop comp undo differs");ProjectStore(d.root).save(s);check(ProjectStore(d.root).load()==s,"Loop take save/reopen differs");
    for(unsigned n=0;n<s.assets.size();++n)check(hashMediaFile(d.root/utf8Path(s.assets[n].relativePath))==hashes[n],"Loop comp changed raw bytes");
    check(initial.assets.size()==1,"Prepared session mutated by recording");
}
void interrupted(bool cancel) {
    Directory d;const auto s=original(d.root);auto cfg=config();
    ManualRecordingOptions o;o.playback=cfg;o.capture.maximumCallbackFrames=256;o.capture.slabFrames=256;o.nativeInputs=2;o.backend=CaptureBackend::Synthetic;
    std::vector<ManualRecordingArm> arms;
    for(unsigned lane=0;lane<2;++lane)arms.push_back({{s.tracks[lane].id,{lane},lane?29:7,RecordingMonitor::Off},{}});
    ManualRecordingRun run(d.root,s,plan(s),arms,o);
    for(unsigned pass=0;pass<5;++pass){const auto id=run.prepareTake();check(run.submit({ManualPunchAction::In,200+pass*53,43,pass*2+1,id})==ManualPunchSubmit::Accepted&&run.submit({ManualPunchAction::Out,200+(pass+1)*53,43,pass*2+2,0})==ManualPunchSubmit::Accepted,"Interrupted loop scheduling refused");}
    std::array<std::array<float,256>,2> inputs{},outputs{};std::array<const float*,2> in{inputs[0].data(),inputs[1].data()};std::array<float*,2> out{outputs[0].data(),outputs[1].data()};
    DeviceBlockClock clock{1000000,17,20000000000ULL,17,1,1,48000,777};unsigned replies=0;
    for(Frame at=200;at<370;) {
        const auto n=unsigned(std::min<Frame>(17,370-at));clock.duration=n;
        for(unsigned f=0;f<n;++f)for(unsigned lane=0;lane<2;++lane)inputs[lane][f]=captured(at+f-(lane?29:7),lane);
        DuplexStatus status;{rt_audit::Guard guard;status=run.process(clock,in,out,256);}check(status==DuplexStatus::Running,"Interrupted loop lost running status");
        run.service();ManualPunchReceipt r;while(run.acknowledgement(r)){check(r.result==ManualPunchResult::Applied,"Earlier loop command refused");++replies;}
        at+=n;clock.position+=n;clock.monotonicNs+=std::uint64_t(n)*1000000000ULL/s.sampleRate;++clock.cycle;std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    if(cancel) {
        run.cancel();
        // A canceled writer reports its retained checkpoint as the initiating
        // error; already joined writers need not report an error.
        try { run.checkError(); }
        catch(const ProjectError &e) {
            check(std::string(e.what())=="Recording canceled; checkpoint retained",
                  "Loop cancellation hid an unrelated initiating error");
        }
    } else { run.stop();run.checkError(); }
    run.checkReader();
    ManualPunchReceipt r;while(run.acknowledgement(r)){check(r.result==ManualPunchResult::Applied||r.result==ManualPunchResult::TransportStopped,"Stopped loop command lost terminal receipt");++replies;}
    unsigned groups=0,complete=0,partial=0;ManualRecordedGroup group;
    while(run.takeGroup(group)) {
        ++groups;complete+=group.complete();partial+=!group.complete();
        if(!group.complete())refusal([&]{withManualTakeLanes(s,group,"Refused partial",102);});
        check(group.canceled==cancel,"Loop cancellation identity changed");
        for(unsigned lane=0;lane<2;++lane) {
            const auto &v=group.lanes[lane];
            if(v.checkpoint) {
                const auto bytes=hashMediaFile(v.checkpoint->source);
                auto verified=inspectRecording(*v.job,{},true);
                check(verified.committedFrames==v.checkpoint->committedFrames&&hashMediaFile(verified.source)==bytes,"Interrupted loop checkpoint changed while inspecting");
                const auto expected=std::max<Frame>(0,std::min(group.endFrame+(lane?29:7),Frame(370))-(group.beginFrame+(lane?29:7)));
                check(v.capturedFrames==expected&&v.checkpoint->committedFrames<=expected,"Loop interruption fabricated delayed capture");
            } else check(v.capturedFrames==0,"Interrupted loop lost its capture checkpoint");
        }
    }
    check(groups==4&&complete==(cancel?0:2)&&partial==(cancel?4:2)&&replies==10&&!run.occupiedSlots(),"Loop stop/cancel lost partial group or pending command credit");
    check(ProjectStore(d.root).load()==s,"Loop interruption silently saved/adopted partial take");
}

}
int main(){try{timing();playback();clipGeometry();record();interrupted(false);interrupted(true);const auto &a=rt_audit::counts;check(a.cppAllocate+a.cppFree+a.cAllocate+a.cFree+a.blockingLock==0,"Loop callback audit violation");std::cout<<"loop_stream_checks="<<checks<<" callback_audit=0 actual_passes=5 delayed_lanes=2 raw_unchanged=true live_export=true save_reopen=true\n";}catch(const std::exception&e){std::cerr<<e.what()<<'\n';return 1;}}
