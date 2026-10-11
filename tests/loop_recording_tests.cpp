// SPDX-License-Identifier: GPL-3.0-only
#include <soundcurrent/loop_recording.hpp>
#include <soundcurrent/export.hpp>
#include <soundcurrent/wave_validation.hpp>
#include "rt_audit.hpp"
#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <iostream>
#include <fstream>
#include <limits>
#include <thread>
using namespace soundcurrent::daw;
namespace {
unsigned checks=0;
void check(bool v,const char *why) { ++checks;if(!v)throw std::runtime_error(why); }
template<class F>void refuse(F fn) { try { fn(); }catch(const ProjectError&){++checks;return;}throw std::runtime_error("Invalid loop recording operation accepted"); }
struct Directory {
    std::filesystem::path root=std::filesystem::temp_directory_path()/utf8Path("sc-continuous-Україна-"+Id::generate().str());
    Directory(){std::filesystem::create_directories(root/"exports");}
    ~Directory(){std::error_code ec;std::filesystem::remove_all(root,ec);}
};
float backing(Frame f){return float(double(f%101-50)*.04);}
float inputSample(Frame f,unsigned lane){return lane?float(double(f%23-11)*-.0625+.375):float(double(f%17-8)*.25);}
Session source(const std::filesystem::path &root) {
    auto s=makeOneTrackSession("Continuous loop", "Input left");
    CapturePipe pipe({});RecordingSpec spec;spec.projectId=s.id;spec.trackId=s.tracks[0].id;spec.capture=pipe.config();
    CaptureWriter writer(root,spec);std::array<float,256> block{};const float *in=block.data();
    for(Frame at=0;at<512;at+=256){for(unsigned f=0;f<256;++f)block[f]=backing(at+f);check(pipe.push({&in,1},256,at).acceptedFrames==256,"Backing capture lost frames");while(writer.drainOne(pipe)){}}
    pipe.finish();while(writer.drainOne(pipe)){}attachRecording(s,writer.finalize(pipe));
    s.tracks.push_back(makeAudioTrack("Input right",{},s.sampleRate));
    for(auto &t:s.tracks)t.eq.enabled=false;
    s.tracks[0].inputLatencyFrames=7;s.tracks[1].inputLatencyFrames=29;
    s.loop={true,102,155};ProjectStore(root).save(s);return s;
}
MixPlan plan(const Session &s){MixPlan p;p.output={LayoutKind::Stereo,2};for(unsigned n=0;n<s.tracks.size();++n)p.tracks.push_back({s.tracks[n].id,{{0,n,1}}});return p;}
LoopRecordingPlan prepared(const Session &s,std::optional<std::uint64_t> passes) {
    std::vector<DuplexRecordingLane> lanes;
    for(unsigned n=0;n<2;++n){DuplexRecordingLane l;l.spec.projectId=s.id;l.spec.trackId=s.tracks[n].id;l.spec.capture.sampleRate=s.sampleRate;l.spec.capture.maximumCallbackFrames=256;l.spec.capture.slabFrames=256;l.spec.inputLatencyFrames=s.tracks[n].inputLatencyFrames;l.inputChannels={n};l.writer.checkpointFrames=64;lanes.push_back(std::move(l));}
    DuplexRecordingOptions o;o.backend=CaptureBackend::Synthetic;o.nativeInputs=2;
    o.playback.graph.startFrame=200;o.playback.graph.generation=43;o.playback.graph.maximumFrames=256;o.playback.slabFrames=256;
    return prepareLoopRecording(s,std::move(lanes),o,passes);
}
std::vector<float> pcm(const std::filesystem::path &root,const std::string &name) {
    ApprovedMediaRoot approved(root,ResourceLedger(16*1024*1024,"Loop raw audit"));auto file=approved.open(name,1024*1024);std::vector<float> values;
    validateApprovedWave(file,{}, {},[&](std::uint64_t,std::span<const double> v){for(auto f:v)values.push_back(float(f));});return values;
}
void pump(DuplexRecordingRun &run,const Session &s,Frame until) {
    std::array<std::array<float,256>,2> input{},output{};
    std::array<const float *,2> in{input[0].data(),input[1].data()};std::array<float *,2> out{output[0].data(),output[1].data()};
    DeviceBlockClock clock{1000000,17,20000000000ULL,17,1,1,48000,777};
    for(Frame at=200;at<until;) {
        const auto n=unsigned(std::min<Frame>(17,until-at));clock.duration=n;
        for(unsigned f=0;f<n;++f)for(unsigned lane=0;lane<2;++lane)input[lane][f]=inputSample(at+f-(lane?29:7),lane);
        DuplexStatus status;{rt_audit::Guard guard;status=run.process(clock,in,out,256);}
        check(status==DuplexStatus::Running||status==DuplexStatus::Complete,"Continuous native-clock capture failed");
        const auto length=s.loop.endFrame-s.loop.startFrame;
        for(unsigned f=0;f<n;++f)check(output[0][f]==backing(102+(at-200+f)%length)&&output[1][f]==0,"Looped backing differs from independent cursor");
        check(!run.missingTrackFrames(),"Loop recording backing underflow");
        at+=n;clock.position+=n;clock.monotonicNs+=std::uint64_t(n)*1000000000ULL/s.sampleRate;++clock.cycle;
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
}
std::vector<LoopRecordedSource> receipts(DuplexRecordingRun &run) {
    std::vector<LoopRecordedSource> out;
    for(unsigned lane=0;lane<run.lanes();++lane){const auto snapshot=run.capture(lane);check(snapshot.origin&&snapshot.writerComplete,"Joined loop source has no clock origin/worker receipt");out.push_back({run.result(lane),snapshot.endReason,*snapshot.origin});}
    return out;
}
void actual(Frame period,bool untilStop) {
    Directory d;auto s=source(d.root);s.loop.endFrame=s.loop.startFrame+period;ProjectStore(d.root).save(s);const auto initial=s;
    const auto backingHash=hashMediaFile(d.root/utf8Path(s.assets[0].relativePath));
    auto p=prepared(s,untilStop?std::optional<std::uint64_t>{}:std::optional<std::uint64_t>{13});
    check(p.intent.tracks.size()==2&&p.options.musicalPunch&&p.options.playback.loop==std::optional(PlaybackLoopRegion{102,102+period}),"Automatic loop preparation lost mapping/arms");
    check(untilStop?(p.options.playback.endFrame==std::numeric_limits<Frame>::max()-29):(p.options.playback.endFrame==200+13*period),"Prepared recording gained a pass ceiling or wrong end");
    DuplexRecordingRun run(d.root,s,plan(s),p.lanes,p.options);
    check(run.lanes()==2&&std::filesystem::directory_iterator(d.root/"media")!=std::filesystem::directory_iterator{},"Continuous owner shape differs");
    run.startWriters();const auto context=loopRecordingContext(p);
    const auto encoded=encodeLoopRecordingContext(context);
    check(decodeLoopRecordingContext(encoded)==context,"Original loop context round trip differs");
    persistLoopRecordingContext(d.root,context);persistLoopRecordingContext(d.root,context);
    for(unsigned lane=0;lane<2;++lane)check(inspectLoopRecordingContext(*run.jobDirectory(lane))==std::optional(context),"Pre-activation loop intent not durable for every arm");
    auto wrong=context;wrong.intent.projectLoop.endFrame++;
    refuse([&]{persistLoopRecordingContext(d.root,wrong);});run.checkActivation();
    const Frame wanted=untilStop?200+4*period+17+29:run.playbackEnd();
    pump(run,s,wanted);run.stop();run.checkReader();const auto raw=receipts(run);
    unsigned captureFiles=0;
    for(const auto &entry:std::filesystem::directory_iterator(d.root/"media"))
        if(std::filesystem::exists(entry.path()/"take.wav"))++captureFiles;
    check(captureFiles==3,"Per-pass recording created extra raw files instead of two continuous sources plus backing");
    const auto extent=loopRecordingExtent(p.intent,raw);
    persistLoopRecordingContext(d.root,context); // Exact idempotent retry never overwrites.
    check(extent.completePasses==(untilStop?4u:13u)&&extent.partialFrames==(untilStop?17:0),"Common delayed prefix/partial extent differs");
    std::vector<std::string> hashes;
    for(unsigned lane=0;lane<2;++lane){const auto &r=raw[lane];const auto bytes=pcm(d.root,r.recording.asset.relativePath);check(r.origin.devicePosition==1000000+(lane?29:7)&&r.origin.generation==43,"Continuous source origin became wrapped project time");check(bytes.size()==std::size_t(r.recording.asset.frames),"Full raw source shape differs");for(unsigned f=0;f<bytes.size();++f)check(bytes[f]==inputSample(200+f,lane),"Continuous raw sample lost/duplicated at loop wrap");hashes.push_back(hashMediaFile(d.root/utf8Path(r.recording.asset.relativePath)));const auto job=*run.jobDirectory(lane);const auto journal=inspectRecording(job,{},true);check(journal.finalized&&journal.committedFrames==r.recording.asset.frames&&journal.timingOrigin==std::optional(r.origin),"Original finalized raw journal/origin changed");}
    auto reversed=raw;std::reverse(reversed.begin(),reversed.end());check(loopRecordingExtent(p.intent,reversed).commonFrames==extent.commonFrames,"Source ordering retargeted arms");
    for(std::uint64_t pass=0;pass<extent.windows();++pass){const auto w=loopRecordingWindow(p.intent,raw,pass);check(w&&w->sources.size()==2&&w->streamBegin==200+Frame(pass)*period&&w->projectBegin==102,"Requested pass clock mapping differs");for(unsigned lane=0;lane<2;++lane)check(w->sources[lane].sourceFrame==Frame(pass)*period&&w->sources[lane].rawOrigin==raw[lane].origin,"Pass view fabricated a new origin or moved raw offset");}
    check(!loopRecordingWindow(p.intent,raw,extent.windows()),"Empty future pass fabricated");
    refuse([&]{withLoopTakeLanes(s,p.intent,raw,extent.windows(),1,"Bad ");});
    refuse([&]{withLoopTakeLanes(s,p.intent,raw,0,1,"Budget ",false,{1});});
    auto damaged=raw;damaged[1].recording.spec.capture.startFrame++;refuse([&]{loopRecordingExtent(p.intent,damaged);});
    damaged=raw;damaged[1].origin.generation++;refuse([&]{loopRecordingExtent(p.intent,damaged);});
    damaged=raw;damaged.pop_back();refuse([&]{loopRecordingExtent(p.intent,damaged);});
    damaged=raw;damaged[1].origin.rateDenominator++;refuse([&]{loopRecordingExtent(p.intent,damaged);});
    damaged=raw;damaged[1].recording.captureStatus=CaptureStatus::Running;refuse([&]{loopRecordingExtent(p.intent,damaged);});
    damaged=raw;damaged[1].recording.asset.sha256="invalid";refuse([&]{loopRecordingExtent(p.intent,damaged);});
    if(!untilStop){damaged=raw;damaged[1].recording.asset.frames++;refuse([&]{loopRecordingExtent(p.intent,damaged);});}
    for(unsigned lane=0;lane<2;++lane) {
        const auto job=*run.jobDirectory(lane);const auto journal=inspectRecording(job,{},true);
        validateLoopRecordingCheckpoint(context,journal);
        auto bad=journal;bad.spec.inputLatencyFrames++;refuse([&]{validateLoopRecordingCheckpoint(context,bad);});
        bad=journal;bad.timingOrigin->generation++;refuse([&]{validateLoopRecordingCheckpoint(context,bad);});
        check(inspectLoopRecordingContext(job)==std::optional(context),"Finalization changed original loop intent");
    }
    if(untilStop){refuse([&]{withLoopTakeLanes(s,p.intent,raw,4,1,"Partial ");});const auto partial=withLoopTakeLanes(s,p.intent,raw,4,1,"Partial ",true);check(partial.tracks[0].takeLanes[0].clips[0].lengthFrames==17&&partial.tracks[1].takeLanes[0].captureEndFrame==200+4*period+17,"Explicit partial view fabricated a full pass");}
    else {
        // Two pages, thirteen actual passes and only two continuous capture files.
        const auto page=withLoopTakeLanes(s,p.intent,raw,0,5,"Прохід ");
        auto all=withLoopTakeLanes(page,p.intent,raw,5,8,"Прохід ");
        check(all.assets.size()==3&&all.tracks[0].takeLanes.size()==13&&all.tracks[1].takeLanes.size()==13&&all.tracks[0].clips==s.tracks[0].clips,"Continuous pages duplicated media or became audible");
        EditHistory history(s);check(history.adopt(all)&&history.undo()&&s==initial&&history.redo()&&s==all,"Loop retention semantic history differs");
        history.structural({SelectTakeRegion{s.tracks[0].id,s.tracks[0].takeLanes[9].id,102,102+period},SelectTakeRegion{s.tracks[1].id,s.tracks[1].takeLanes[9].id,102,102+period}});
        const auto comp=s;check(history.undo()&&history.redo()&&s==comp,"Windowed group comp history differs");
        MixExportSpec exportSpec(plan(s));exportSpec.startFrame=102;exportSpec.endFrame=102+period;exportSpec.blockFrames=7;
        const auto rendered=exportMixWav(d.root,s,d.root/"exports/comp.wav",exportSpec);const auto result=pcm(d.root,"exports/comp.wav");check(rendered.frames==period&&result.size()==std::size_t(period*2),"Comp export extent differs");
        for(Frame f=0;f<period;++f)for(unsigned lane=0;lane<2;++lane)check(result[std::size_t(f*2+lane)]==inputSample(200+9*period+f,lane),"Comp did not address the original raw pass window");
        ProjectStore(d.root).save(s);check(ProjectStore(d.root).load()==s,"Loop view Save/reopen differs");
    }
    for(unsigned lane=0;lane<2;++lane)check(hashMediaFile(d.root/utf8Path(raw[lane].recording.asset.relativePath))==hashes[lane],"Loop retention/comp rewrote full raw asset");
    check(hashMediaFile(d.root/utf8Path(initial.assets[0].relativePath))==backingHash,"Recording changed backing bytes");
}
void admission() {
    Directory d;const auto s=source(d.root);refuse([&]{prepared(s,0);});refuse([&]{prepared(s,UINT64_MAX);});
    auto no=s;no.loop.enabled=false;refuse([&]{prepared(no,13);});no=s;no.punch={true,0,1};refuse([&]{prepared(no,13);});
    auto p=prepared(s,13);p.lanes[1].spec.trackId=p.lanes[0].spec.trackId;p.lanes[1].spec.inputLatencyFrames=7;refuse([&]{prepareLoopRecording(s,p.lanes,{},13);});
    p=prepared(s,13);p.options.musicalPunch.reset();p.options.playback.graph.startFrame=std::numeric_limits<Frame>::max()-29;refuse([&]{prepareLoopRecording(s,p.lanes,p.options);});
}
std::string replaced(std::string bytes,const std::string &from,const std::string &to) {
    const auto at=bytes.find(from);check(at!=std::string::npos,"Malformed-context test did not target a field");
    bytes.replace(at,from.size(),to);return bytes;
}
void contextFailures() {
    Directory d;const auto s=source(d.root);const auto p=prepared(s,13);
    const auto c=loopRecordingContext(p);const auto bytes=encodeLoopRecordingContext(c);
    for(const auto &b:{replaced(bytes,"\"schemaMinor\": 0","\"schemaMinor\": 1"),
        replaced(bytes,"\"generation\": 43","\"generation\": true"),
        replaced(bytes,"\"streamBegin\": 200","\"streamBegin\": -1"),
        replaced(bytes,"\"passes\": 13","\"passes\": 18446744073709551615"),
        replaced(bytes,"\"sampleRate\": 48000","\"sampleRate\": 48000, \"sampleRate\": 48000"),
        replaced(bytes,"\"schemaMajor\": 1","\"extra\": 1, \"schemaMajor\": 1"),
        replaced(bytes,c.sources[1].track.str(),c.sources[0].track.str()),
        replaced(bytes,c.sources[1].asset.str(),c.sources[0].asset.str()),
        replaced(bytes,"\"channels\": 1","\"channels\": 0"),
        std::string(128*1024+1,' '),std::string("{"),
        std::string(32,'[')+"0"+std::string(32,']')}) refuse([&]{decodeLoopRecordingContext(b);});
    auto max=c;max.sources.clear();max.intent.tracks.clear();
    for(unsigned n=0;n<256;++n){LoopRecordingBinding b{Id::generate(),Id::generate(),{},200,0};max.sources.push_back(b);max.intent.tracks.push_back(b.track);}
    check(decodeLoopRecordingContext(encodeLoopRecordingContext(max))==max,"256-input context exceeds bounded codec");
    max.sources.push_back(max.sources.back());max.intent.tracks.push_back(max.intent.tracks.back());refuse([&]{encodeLoopRecordingContext(max);});
    DuplexRecordingRun run(d.root,s,plan(s),p.lanes,p.options);run.startWriters();
    const auto job0=*run.jobDirectory(0),job1=*run.jobDirectory(1);
    check(!inspectLoopRecordingContext(job0),"Non-loop job fabricated loop context");
    const auto hash0=hashMediaFile(job0/"journal.json"),hash1=hashMediaFile(job1/"journal.json");
    {std::ofstream file(job1/"loop-context.json");file<<"{broken";}
    refuse([&]{persistLoopRecordingContext(d.root,c);});
    check(!std::filesystem::exists(job0/"loop-context.json")&&hashMediaFile(job0/"journal.json")==hash0&&hashMediaFile(job1/"journal.json")==hash1,"Group preflight changed original jobs on failure");
    run.cancel();
    check(std::filesystem::exists(job0/"audio.partial.rf64")&&std::filesystem::exists(job1/"audio.partial.rf64"),"Failed context admission discarded raw jobs");
#ifndef _WIN32
    std::filesystem::remove(job1/"loop-context.json");std::filesystem::create_symlink(job0/"journal.json",job1/"loop-context.json");
    refuse([&]{inspectLoopRecordingContext(job1);});
#endif
}
template<class F>void waitForDisk(F ready,const char *why) {
    const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(5);
    while(!ready()&&std::chrono::steady_clock::now()<deadline)
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    check(ready(),why);
}
void interruptedContext(bool delayedWriter) {
    Directory d;auto s=source(d.root);auto p=prepared(s,{});
    std::array<std::atomic<bool>,2> entered{};
    std::atomic<bool> permitWrite{false};
    if(delayedWriter)for(unsigned lane=0;lane<2;++lane)
        p.lanes[lane].writer.boundary=[&,lane](RecordingBoundary boundary,Frame written) {
            if(boundary!=RecordingBoundary::BeforeAudioWrite||written!=0)return;
            entered[lane].store(true,std::memory_order_release);
            const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(5);
            while(!permitWrite.load(std::memory_order_acquire)) {
                if(std::chrono::steady_clock::now()>=deadline)
                    throw std::runtime_error("Delayed disk fixture was not released");
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
            }
        };
    const auto context=loopRecordingContext(p);DuplexRecordingRun run(d.root,s,plan(s),p.lanes,p.options);
    run.startWriters();persistLoopRecordingContext(d.root,context);run.checkActivation();
    pump(run,s,200+6*53+29);
    if(delayedWriter) {
        waitForDisk([&]{return entered[0].load(std::memory_order_acquire)&&entered[1].load(std::memory_order_acquire);},"Delayed disk owners did not reach the write gate");
        check(run.capture(0).written==0&&run.capture(1).written==0,"Delayed fixture wrote before release");
        permitWrite.store(true,std::memory_order_release);
    }
    // written is release-published by each disk owner after drainOne returns,
    // including its completed journal checkpoint. Callback progress alone does
    // not prove that an interrupted job has a durable recoverable prefix.
    waitForDisk([&]{return run.capture(0).written>=256&&run.capture(1).written>=256;},"Disk owners did not publish the first checkpoint before interruption");
    run.cancel();
    s.loop={false,0,0};s.playheadFrame=0;ProjectStore(d.root).save(s);
    for(unsigned lane=0;lane<2;++lane) {
        const auto job=*run.jobDirectory(lane);const auto original=inspectRecording(job,{},true);
        check(!original.finalized&&original.committedFrames>=256,"Interrupted test did not retain a verified prefix");
        validateLoopRecordingCheckpoint(context,original);
        check(inspectLoopRecordingContext(job)==std::optional(context),"Changed project loop retargeted interrupted capture intent");
        const auto hash=hashMediaFile(original.source);const auto recovered=recoverRecording(d.root,job);
        check(recovered.spec.recoveredFrom==std::optional(original.spec.assetId)&&recovered.asset.frames==original.committedFrames,"Original prefix recovery lost lineage/extent");
        const auto copy=inspectRecording(d.root/"media"/("capture-"+recovered.spec.assetId.str()),{},true);
        refuse([&]{validateLoopRecordingCheckpoint(context,copy);});
        check(hashMediaFile(original.source)==hash&&inspectLoopRecordingContext(job)==std::optional(context),"Recovery mutated original raw bytes/loop context");
    }
}
void lateContext() {
    Directory d;const auto s=source(d.root);const auto p=prepared(s,3);
    DuplexRecordingRun run(d.root,s,plan(s),p.lanes,p.options);run.startWriters();run.checkActivation();
    pump(run,s,run.playbackEnd());run.stop();
    refuse([&]{persistLoopRecordingContext(d.root,loopRecordingContext(p));});
    for(unsigned lane=0;lane<2;++lane)check(!inspectLoopRecordingContext(*run.jobDirectory(lane)),"Late context publication fabricated original intent");
}
}
int main(){try{admission();contextFailures();for(Frame n:{1,2,7,53})actual(n,false);actual(53,true);interruptedContext(false);interruptedContext(true);lateContext();const auto &a=rt_audit::counts;check(a.cppAllocate+a.cppFree+a.cAllocate+a.cFree+a.blockingLock==0,"Continuous callback allocation/free/lock");std::cout<<"continuous_loop_checks="<<checks<<" callback_audit=0 actual_passes=13 raw_files_per_run=2 short_periods=1,2,7,53 delayed_inputs=7,29 partial_explicit=true comp_export=true save_reopen=true original_context_durable=true interrupted_prefix=true delayed_disk_prefix=true bounded_context_inputs=256\n";}catch(const std::exception &e){std::cerr<<e.what()<<'\n';return 1;}}
