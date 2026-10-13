// SPDX-License-Identifier: GPL-3.0-only
#include <soundcurrent/clip_timing.hpp>
#include <soundcurrent/export.hpp>
#include <soundcurrent/mix_reader.hpp>
#include <soundcurrent/recording.hpp>
#include <soundcurrent/wave_validation.hpp>
#include <nlohmann/json.hpp>
#include "project_schema_fixture.hpp"
#include "rt_audit.hpp"
#include <algorithm>
#include <array>
#include <iostream>
#include <limits>
#include <cmath>
#include <numbers>
using namespace soundcurrent::daw;
using Json = nlohmann::json;
namespace {
unsigned checks = 0;
void check(bool value, const char *why) { ++checks; if (!value) throw std::runtime_error(why); }
template<class F> void refuses(F fn) {
    try { fn(); } catch (const ProjectError &) { ++checks; return; }
    throw std::runtime_error("Invalid take/comp operation accepted");
}
struct Directory {
    std::filesystem::path root = std::filesystem::temp_directory_path()/utf8Path("sc-takes-Україна-"+Id::generate().str());
    Directory() { std::filesystem::create_directories(root/"exports"); }
    ~Directory() { std::error_code e; std::filesystem::remove_all(root,e); }
};
float source(Frame f, unsigned take) {
    if(take==0)return float(double(f%23-11)*.125);
    if(take==1)return float(double(f%17-8)*-.0625+.375);
    return float(double(f%Frame(13+2*take)-Frame(6+take))*.125+double(take)*.25);
}
TakeLane record(Session &s, const std::filesystem::path &root, unsigned take) {
    CaptureConfig config; config.slabFrames=256; config.maximumCallbackFrames=256;
    CapturePipe pipe(config); RecordingSpec spec;
    spec.projectId=s.id; spec.trackId=s.tracks[0].id; spec.capture=pipe.config();
    CaptureWriter writer(root,spec); std::array<float,256> data{}; const float *pointer=data.data();
    for (Frame at=0;at<512;at+=256) {
        for (Frame f=0;f<256;++f) data[std::size_t(f)]=source(at+f,take);
        check(pipe.push({&pointer,1},256,at).acceptedFrames==256,"Original capture lost samples");
        while (writer.drainOne(pipe)) {}
    }
    pipe.finish(); while (writer.drainOne(pipe)) {}
    attachRecording(s,writer.finalize(pipe));
    TakeLane lane; lane.name=take==0 ? "Take A — Ελληνικά" : "Take B — Українська";
    lane.captureStartFrame=1000+Frame(take)*1000; lane.captureEndFrame=lane.captureStartFrame+512;
    lane.clips.push_back(std::move(s.tracks[0].clips.back())); s.tracks[0].clips.pop_back();
    return lane;
}
std::vector<float> live(const std::filesystem::path &root, const Session &s, const MixPlan &plan,
                        unsigned partition) {
    MixPlaybackConfig config; config.endFrame=512; config.slabFrames=256;
    config.graph.maximumFrames=partition;
    MixPlayback mix(s,plan,config); MixReader reader(mix,root,s);
    const auto channels=plan.output.channels;
    std::vector<float> buffer(std::size_t(partition)*channels), output(512*channels);
    std::vector<float *> pointers;
    for (unsigned c=0;c<channels;++c) pointers.push_back(buffer.data()+c*partition);
    for (Frame at=0;at<512;) {
        while (reader.fillRound()) {}
        const auto count=unsigned(std::min<Frame>(partition,512-at)); MixPlaybackReport report;
        { rt_audit::Guard guard; report=mix.process(pointers,count); }
        check(report.timelineFrames==count && !report.missingTrackFrames && !report.staleTrackFrames &&
                  report.mix.status==ProcessStatus::Ok,"Comp playback lost frames");
        for (unsigned f=0;f<count;++f) for (unsigned c=0;c<channels;++c)
            output[std::size_t(at+f)*channels+c]=pointers[c][f];
        at+=count;
    }
    check(reader.sanitizedSamples()==0,"Comp playback sanitized finite source");
    return output;
}
std::vector<float> exported(const std::filesystem::path &root, const Session &s, const MixPlan &plan) {
    MixExportSpec spec(plan); spec.endFrame=512; spec.blockFrames=37;
    const auto result=exportMixWav(root,s,root/"exports"/utf8Path(Id::generate().str()+".wav"),spec);
    ApprovedMediaRoot approved(root,ResourceLedger(8*1024*1024,"Take export oracle")); auto file=approved.open("exports/"+result.destination.filename().string(),1024*1024);
    std::vector<float> output;
    validateApprovedWave(file,{}, {},[&](std::uint64_t,std::span<const double> samples) {
        for (auto value:samples) output.push_back(float(value));
    });
    check(result.frames==512,"Comp export duration differs"); return output;
}
void workflow() {
    Directory d; auto s=makeOneTrackSession("Retained takes","Voice"); s.tracks[0].eq.enabled=false;
    ProjectStore(d.root).save(s);
    auto a=record(s,d.root,0), b=record(s,d.root,1); const auto track=s.tracks[0].id;
    EditHistory history(s);
    check(history.structural({InsertTakeLane{track,a,{}},InsertTakeLane{track,b,{}}}),"Retained lane insertion missing");
    const auto inactive=s;
    MixPlan plan{{LayoutKind::Mono,1},{{track,{{0,0,1}}}}};
    const auto silence=live(d.root,s,plan,37);
    check(std::all_of(silence.begin(),silence.end(),[](float v){return v==0;}),"Inactive takes entered playback");
    check(exported(d.root,s,plan)==silence,"Inactive takes entered export");
    const auto hashA=hashMediaFile(d.root/utf8Path(s.assets[0].relativePath));
    const auto hashB=hashMediaFile(d.root/utf8Path(s.assets[1].relativePath));
    check(history.structural({SelectTakeRegion{track,a.id,0,256},SelectTakeRegion{track,b.id,256,512}}),"Comp transaction missing");
    const auto selected=s;
    check(s.tracks[0].clips.size()==2 && s.tracks[0].takeLanes==inactive.tracks[0].takeLanes &&
              s.tracks[0].clips[1].takeOrigin==std::optional<ClipTakeOrigin>({track,b.id,b.clips[0].id}),
          "Comp lost original lanes/provenance");
    check(history.undo() && s==inactive && history.redo() && s==selected,"Comp was not one reversible transaction");
    const auto beforeNoop=history.resources();
    check(!history.structural({SelectTakeRegion{track,a.id,0,256}}) && s==selected &&
              history.resources()==beforeNoop,"Repeated comp selection changed IDs/history");
    const auto first=live(d.root,s,plan,13);
    check(first==live(d.root,s,plan,256) && first==exported(d.root,s,plan),"Live partitions/export differ");
    for (Frame f=0;f<512;++f) check(first[std::size_t(f)]==source(f,f<256?0:1),"Independent A/B comp sample differs");
    ProjectStore(d.root).save(s); check(ProjectStore(d.root).load()==s,"Comp save/reopen lost lanes or selection");
    check(hashMediaFile(d.root/utf8Path(s.assets[0].relativePath))==hashA &&
              hashMediaFile(d.root/utf8Path(s.assets[1].relativePath))==hashB,"Comp changed raw takes");
    auto underlying=inactive;
    auto originalClip=a.clips[0]; originalClip.id=Id::generate();
    originalClip.processing.fadeIn={0,65,ClipFadeCurve::Linear,1};
    underlying.tracks[0].clips={originalClip};
    EditHistory replacement(underlying);
    check(replacement.structural({SelectTakeRegion{track,b.id,17,53}}),"Interior comp replacement missing");
    const auto replaced=live(d.root,underlying,plan,37);
    for (Frame f=0;f<512;++f) {
        const auto expected=f>=17 && f<53 ? source(f,1) :
            float(double(source(f,0))*(f<=64 ? double(f)/64 : 1));
        check(replaced[std::size_t(f)]==expected,"Comp replacement changed retained outside source/fade");
    }
    check(underlying.tracks[0].clips.size()==3 && replacement.undo() &&
              underlying.tracks[0].clips==std::vector<Clip>{originalClip},
          "Interior comp undo lost underlying arrangement");
    const auto before=s; const auto resources=history.resources();
    refuses([&]{history.structural({RenameTrack{track,"Rejected"},SelectTakeRegion{track,a.id,0,513}});});
    check(s==before && history.resources()==resources,"Uncovered comp mutated state/history");
    refuses([&]{history.structural({RemoveTakeLane{track,b.id}});});
    check(s==before,"Referenced lane deletion discarded comp source");
    check(history.structural({MoveTakeLane{track,b.id,a.id},RenameTakeLane{track,b.id,"Second"}}),"Lane reorder/rename missing");
    check(history.undo() && s==before,"Lane order/name undo lost comp");
    auto extra=makeAudioTrack("Destination",{},s.sampleRate); extra.eq.enabled=false;
    check(history.structural({InsertTrack{extra,{}},MoveClip{track,extra.id,s.tracks[0].clips[1].id,256,{}}}),"Comp move between tracks missing");
    check(s.tracks[1].clips[0].takeOrigin->trackId==track && history.undo() && s==before,"Comp move lost retained source provenance");
    check(decodeProject(encodeProject(s))==s,"Comp canonical codec differs");
    auto legacy=Json::parse(encodeProject(inactive)); legacy["schemaMinor"]=15;
    refuses([&]{decodeProject(legacy.dump());}); // Old schemas refuse new take keys.
    legacy["tracks"][0]["takeLanes"]=Json::array(); sc_test::eraseSchema16FixtureKeys(legacy);
    auto expected=inactive; expected.tracks[0].takeLanes.clear();
    check(decodeProject(legacy.dump())==expected,"Neutral schema1.15 migration invented takes");
    for (unsigned mode=0;mode<12;++mode) {
        auto bad=Json::parse(encodeProject(before)); auto &lane=bad["tracks"][0]["takeLanes"][0];
        if (mode==0) lane["captureEndFrame"]=lane["captureStartFrame"];
        if (mode==1) lane["captureStartFrame"]=-1;
        if (mode==2) lane["captureEndFrame"]=true;
        if (mode==3) lane["id"]=bad["tracks"][0]["id"];
        if (mode==4) lane["groupId"]=bad["assets"][0]["id"];
        if (mode==5) lane["clips"][0]["assetId"]=Id::generate().str();
        if (mode==6) lane["clips"][0]["takeOrigin"]=bad["tracks"][0]["clips"][0]["takeOrigin"];
        if (mode==7) lane["clips"]=Json::array();
        if (mode==8) bad["tracks"][0]["clips"][0]["takeOrigin"]["clipId"]=Id::generate().str();
        if (mode==9) lane["extra"]=false;
        if (mode==10) lane["clips"].push_back(lane["clips"][0]);
        if (mode==11) lane.erase("captureEndFrame");
        refuses([&]{decodeProject(bad.dump());});
    }
    EditHistory tiny(s,{},HistoryBudget{32*1024*1024,1,256});
    refuses([&]{tiny.structural({SelectTakeRegion{track,a.id,17,111}});});
    check(s==before && !tiny.resources().undoCommands,"Bounded comp refusal changed state/history");
}
void fiveTakeTransitions() {
    for(const auto curve:{ClipFadeCurve::Linear,ClipFadeCurve::EqualPower,ClipFadeCurve::Smoothstep}) {
        Directory d;auto s=makeOneTrackSession("Five takes","Voice");s.tracks[0].eq.enabled=false;
        ProjectStore(d.root).save(s);const auto track=s.tracks[0].id;
        std::vector<TakeLane> takes;std::vector<SessionEdit> retain;
        for(unsigned n=0;n<5;++n) {auto lane=record(s,d.root,n);lane.name="Take "+std::to_string(n+1);takes.push_back(lane);retain.push_back(InsertTakeLane{track,lane,{}});}
        applySessionEdits(s,retain);const auto retained=s.tracks[0].takeLanes;
        std::vector<std::string> raw;for(const auto &asset:s.assets)raw.push_back(hashMediaFile(d.root/utf8Path(asset.relativePath)));
        std::vector<SessionEdit> select;
        for(unsigned n=0;n<5;++n)select.push_back(SelectTakeRegion{track,takes[n].id,Frame(n)*100,n==4?512:Frame(n+1)*100});
        EditHistory history(s);check(history.structural(select),"Five-take comp selection missing");
        const auto hardCuts=s;std::vector<SessionEdit> transitions;
        for(unsigned n=0;n<4;++n) {
            const auto cut=Frame(n+1)*100;
            transitions.push_back(PrepareCompTransition{track,s.tracks[0].clips[n].id,s.tracks[0].clips[n+1].id,cut-16,cut+16,curve});
        }
        check(history.structural(transitions),"Five-take transition batch missing");const auto selected=s;
        const MixPlan plan{{LayoutKind::Mono,1},{{track,{{0,0,1}}}}};
        const auto output=live(d.root,s,plan,13);
        check(output==live(d.root,s,plan,127) && output==exported(d.root,s,plan),"Five-take live/export partitions differ");
        for(Frame f=0;f<512;++f) {
            double expected=source(f,std::min(4u,unsigned(f/100)));
            for(unsigned n=0;n<4;++n) {
                const auto cut=Frame(n+1)*100;
                if(f>=cut-16 && f<cut+16) {
                    const auto phase=double(f-(cut-16))/31.;
                    const auto in=curve==ClipFadeCurve::Linear?phase:curve==ClipFadeCurve::Smoothstep?phase*phase*(3-2*phase):std::sin(phase*std::numbers::pi/2);
                    const auto out=curve==ClipFadeCurve::EqualPower?std::cos(phase*std::numbers::pi/2):1-in;
                    expected=source(f,n)*out+source(f,n+1)*in;
                }
            }
            check(std::abs(double(output[std::size_t(f)])-expected)<3e-7,"Independent five-take transition PCM differs");
        }
        check(std::any_of(output.begin(),output.end(),[](float x){return std::abs(x)>1;}),"Five-take headroom was normalized");
        check(s.tracks[0].takeLanes==retained,"Transitions changed retained source lanes");
        ProjectStore(d.root).save(s);check(ProjectStore(d.root).load()==s && live(d.root,ProjectStore(d.root).load(),plan,37)==output,"Five-take reopen changes audio/state");
        check(history.undo() && s==hardCuts && history.redo() && s==selected,"Whole transition batch Undo/Redo differs");
        check(history.undo() && history.undo() && s.tracks[0].clips.empty(),"Whole comp cannot be undone to inactive takes");
        for(std::size_t n=0;n<raw.size();++n)check(hashMediaFile(d.root/utf8Path(s.assets[n].relativePath))==raw[n],"Transition changed original recording bytes");
    }
}
void groupedTransitionOffsets() {
    Directory d;auto s=makeOneTrackSession("Group transitions","Mic");s.tracks[0].eq.enabled=false;
    ProjectStore(d.root).save(s);auto a=record(s,d.root,0),b=record(s,d.root,1);s.tracks.clear();
    MixPlan plan{{LayoutKind::Discrete,8},{}};std::vector<SessionEdit> selection;
    for(unsigned n=0;n<8;++n) {
        auto t=makeAudioTrack("Linked",{},s.sampleRate);t.eq.enabled=false;
        auto x=a,y=b;x.id=Id::generate();y.id=Id::generate();
        for(auto *lane:{&x,&y}){lane->clips[0].id=Id::generate();lane->clips[0].startFrame=n;lane->clips[0].lengthFrames=512-n;}
        t.takeLanes={x,y};selection.push_back(SelectTakeRegion{t.id,x.id,17,256});selection.push_back(SelectTakeRegion{t.id,y.id,256,500});
        plan.tracks.push_back({t.id,{{0,n,1}}});s.tracks.push_back(t);
    }
    applySessionEdits(s,selection);const auto original=s;EditHistory h(s);std::vector<SessionEdit> edits;
    for(const auto &t:s.tracks)edits.push_back(PrepareCompTransition{t.id,t.clips[0].id,t.clips[1].id,230,282,ClipFadeCurve::Linear});
    check(h.structural(edits),"Linked transition batch missing");const auto selected=s;
    const auto output=live(d.root,s,plan,113);check(output==exported(d.root,s,plan),"Linked transition export differs");
    for(Frame f=0;f<512;++f)for(unsigned n=0;n<8;++n) {
        double expected=0;
        if(f>=17 && f<500) {
            if(f<230)expected=source(f-n,0);
            else if(f>=282)expected=source(f-n,1);
            else {const auto phase=double(f-230)/51.;expected=source(f-n,0)*(1-phase)+source(f-n,1)*phase;}
        }
        check(std::abs(double(output[std::size_t(f)*8+n])-expected)<2e-7,"Transition changed linked raw offsets");
    }
    check(h.undo() && s==original && h.redo() && s==selected,"Linked transition Undo not atomic");
}
void groupsAndTiming() {
    Directory d; auto s=makeOneTrackSession("Linked comp","Mic"); s.tracks[0].eq.enabled=false;
    ProjectStore(d.root).save(s); auto a=record(s,d.root,0), b=record(s,d.root,1);
    s.tracks.clear(); MixPlan plan{{LayoutKind::Discrete,8},{}};
    std::vector<SessionEdit> edits;
    for (unsigned n=0;n<8;++n) {
        auto t=makeAudioTrack("Linked "+std::to_string(n),{},s.sampleRate); t.eq.enabled=false;
        auto x=a,y=b; x.id=Id::generate(); y.id=Id::generate();
        x.clips[0].id=Id::generate(); y.clips[0].id=Id::generate();
        for (auto *lane:{&x,&y}) { lane->clips[0].startFrame=n; lane->clips[0].lengthFrames=512-n; }
        t.takeLanes={x,y}; plan.tracks.push_back({t.id,{{0,n,1}}});
        edits.push_back(SelectTakeRegion{t.id,x.id,17,256});
        edits.push_back(SelectTakeRegion{t.id,y.id,256,500}); s.tracks.push_back(t);
    }
    const auto original=s; EditHistory history(s); check(history.structural(edits),"Linked comp transaction missing");
    const auto selected=s; check(history.undo() && s==original && history.redo() && s==selected,"Linked eight-track comp undo differs");
    const auto output=live(d.root,s,plan,127);
    for (Frame f=0;f<512;++f) for (unsigned n=0;n<8;++n) {
        const auto expected=f>=17 && f<500 ? source(f-n,f<256?0:1) : 0;
        check(output[std::size_t(f)*8+n]==expected,"Comp changed linked source offsets");
    }
    check(output==exported(d.root,s,plan),"Linked comp export differs");
    auto bad=s; ++bad.tracks[3].takeLanes[0].captureEndFrame; refuses([&]{validate(bad);});
    check(decodeProject(encodeProject(s))==s,"Linked comp portable state differs");
    auto fractional=s; auto &t=fractional.tracks[0]; const auto lane=t.takeLanes[0].id;
    fractional.assets[0].sampleRate=44100;
    t.clips.clear(); // Remove active references before changing declared source timing.
    for (auto &laneValue:t.takeLanes) if (laneValue.clips[0].assetId==fractional.assets[0].id) {
        laneValue.clips[0].sourceTiming={1,7};
        laneValue.clips[0].processing.fadeIn={0,65,ClipFadeCurve::Linear,1};
    }
    validate(fractional); const auto retained=t.takeLanes[0].clips[0];
    // Independent exact arithmetic: 1/7 + 53*(44100/48000) = 48 + 937/1120.
    const SourcePosition expected{48,937,1120};
    applySessionEdits(fractional,{SelectTakeRegion{t.id,lane,53,200}});
    const auto &comp=fractional.tracks[0].clips[0];
    const auto actual=clipSourceMap(comp,44100,48000).at(0);
    check(actual==expected && comp.processing.fadeIn.startFrame==-53 &&
              fractional.tracks[0].takeLanes[0].clips[0]==retained,"Fractional comp crop restarted source/fade");
    auto many=original; many.tracks.resize(1);
    for (unsigned n=0;n<65;++n) {
        auto lane=a; lane.id=Id::generate(); lane.groupId=Id::generate(); lane.clips[0].id=Id::generate();
        many.tracks[0].takeLanes.push_back(lane);
    }
    check(decodeProject(encodeProject(many))==many,"Take count acquired a fixed small cap");
    const auto admitted=sessionPayloadBytes(many);
    refuses([&]{validate(many,StateBudget{admitted-1});});
}
}
int main() {
    try { workflow(); groupsAndTiming(); fiveTakeTransitions(); groupedTransitionOffsets(); check((rt_audit::counts.cppAllocate+rt_audit::counts.cppFree+rt_audit::counts.cAllocate+rt_audit::counts.cFree+rt_audit::counts.blockingLock)==0,"Comp callback allocated/freed/locked");
        std::cout<<"retained_take_comp_checks="<<checks<<" callback_audit="<<(rt_audit::counts.cppAllocate+rt_audit::counts.cppFree+rt_audit::counts.cAllocate+rt_audit::counts.cFree+rt_audit::counts.blockingLock)<<" raw_unchanged=true save_reopen=true grouped_offsets=true\n";
    } catch(const std::exception &e) { std::cerr<<e.what()<<'\n'; return 1; }
}
