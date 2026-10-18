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
#include <fstream>
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
void recordedTakeAudition() {
    Directory d;auto s=makeOneTrackSession("Audition takes","Voice");s.tracks[0].eq.enabled=false;
    ProjectStore(d.root).save(s);const auto track=s.tracks[0].id;
    std::vector<TakeLane> takes;std::vector<SessionEdit> edits;
    for(unsigned n=0;n<5;++n){auto lane=record(s,d.root,n);takes.push_back(lane);edits.push_back(InsertTakeLane{track,lane,{}});}
    applySessionEdits(s,edits);applySessionEdits(s,{SelectTakeRegion{track,takes[0].id,100,200},SelectTakeRegion{track,takes[1].id,200,300}});
    s.playheadFrame=123;s.loop={true,100,200};ProjectStore(d.root).save(s);const auto canonical=s;
    const MixPlan plan{{LayoutKind::Mono,1},{{track,{{0,0,1}}}}};const auto arrangement=exported(d.root,s,plan);
    std::vector<std::string> raw;for(const auto &asset:s.assets)raw.push_back(hashMediaFile(d.root/utf8Path(asset.relativePath)));
    for(unsigned n=0;n<5;++n) {
        const auto preview=makeTakeAudition(s,{track,takes[n].id});const auto output=live(d.root,preview,plan,37);
        for(Frame f=0;f<512;++f)check(output[std::size_t(f)]==source(f,n),"Audition heard arrangement or wrong take");
        check(preview.tracks[0].takeLanes==s.tracks[0].takeLanes && s==canonical && ProjectStore(d.root).load()==s,"Audition mutated canonical state/save");
    }
    check(exported(d.root,s,plan)==arrangement,"Audition entered canonical export");
    for(std::size_t n=0;n<raw.size();++n)check(hashMediaFile(d.root/utf8Path(s.assets[n].relativePath))==raw[n],"Audition changed raw takes");
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
void trackVersions() {
    Directory d; auto s=makeOneTrackSession("Versioned arrangements", "Voice");
    s.tracks[0].eq.enabled=false;
    ProjectStore store(d.root); store.save(s);
    auto lane=record(s,d.root,0); const auto track=s.tracks[0].id;
    EditHistory history(s);
    check(history.structural({InsertTakeLane{track,lane,{}},SelectTakeRegion{track,lane.id,0,512}}), "Version source comp missing");
    const auto initial=s;
    MixPlan plan{{LayoutKind::Mono,1},{{track,{{0,0,1}}}}};
    const auto originalAudio=live(d.root,s,plan,37);
    const auto rawHash=hashMediaFile(d.root/utf8Path(s.assets[0].relativePath));
    check(history.structural({CreateTrackVersion{track,"Alternative — Україна","Original — Ελληνικά",true}}), "Duplicate version missing");
    const auto duplicated=s;
    const auto original=s.tracks[0].inactiveVersions[0].identity.id;
    const auto alternate=s.tracks[0].activeVersion->id;
    check(s.tracks[0].clips[0].id!=initial.tracks[0].clips[0].id &&
          s.tracks[0].clips[0].takeOrigin==initial.tracks[0].clips[0].takeOrigin &&
          s.tracks[0].takeLanes==initial.tracks[0].takeLanes, "Version duplicated raw take or lost origin");
    check(live(d.root,s,plan,127)==originalAudio, "Duplicate changed active audio or summed inactive audio");
    check(history.undo() && s==initial && history.redo() && s==duplicated, "Duplicate Undo/Redo differs");
    auto processing=s.tracks[0].clips[0].processing; processing.muted=true;
    check(history.structural({SetClipProcessing{track,s.tracks[0].clips[0].id,processing}}), "Alternate edit missing");
    const auto muted=s;
    const auto silence=live(d.root,s,plan,37);
    check(std::all_of(silence.begin(),silence.end(),[](float v){return v==0;}), "Inactive arrangement entered playback");
    check(exported(d.root,s,plan)==silence, "Inactive arrangement entered export");
    check(history.structural({SwitchTrackVersion{track,original}}), "Switch missing");
    check(live(d.root,s,plan,127)==originalAudio && exported(d.root,s,plan)==originalAudio,
          "Switched playback/export differ from original");
    check(s.tracks[0].clips==initial.tracks[0].clips && s.tracks[0].eq==initial.tracks[0].eq,
          "Switch changed original clips or processor");
    const auto switched=s;
    check(history.undo() && s==muted && history.redo() && s==switched, "Switch Undo/Redo differs");
    check(!history.structural({SwitchTrackVersion{track,original}}), "Current switch should be no-op");
    check(history.structural({RenameTrackVersion{track,original,"Main"},RenameTrackVersion{track,alternate,"Muted"}}), "Version rename missing");
    store.save(s); check(store.load()==s && decodeProject(encodeProject(s))==s, "Versions lost on reopen");
    check(Json::parse(encodeProject(s)).at("schemaMinor")==24, "Version schema not negotiated");
    auto bad=Json::parse(encodeProject(s)); bad["schemaMinor"]=23;
    refuses([&]{decodeProject(bad.dump());});
    const auto protectedState=s;
    refuses([&]{history.structural({RemoveTakeLane{track,lane.id}});});
    refuses([&]{history.structural({RemoveTrackVersion{track,original}});});
    refuses([&]{history.structural({RenameTrackVersion{track,alternate,""}});});
    refuses([&]{history.structural({SwitchTrackVersion{track,Id::generate()}});});
    check(s==protectedState, "Rejected version edit changed project");
    check(history.structural({CreateTrackVersion{track,"Blank","unused",false}}), "Empty version creation missing");
    const auto empty=s;
    check(s.tracks[0].clips.empty() && live(d.root,s,plan,37)==silence, "New empty version not empty/silent");
    // Inactive comp origins and media remain required, even with no active clips.
    refuses([&]{auto invalid=s; invalid.tracks[0].takeLanes.clear(); validate(invalid);});
    refuses([&]{auto invalid=s; invalid.assets.clear(); validate(invalid);});
    refuses([&]{auto invalid=s; invalid.tracks[0].activeVersion.reset(); validate(invalid);});
    refuses([&]{auto invalid=s; invalid.tracks[0].inactiveVersions[0].identity.id=track; validate(invalid);});
    refuses([&]{auto invalid=s; invalid.tracks[0].inactiveVersions[0].clips[0].lengthFrames=513; validate(invalid);});
    const auto media=d.root/utf8Path(s.assets[0].relativePath);
    // Preserve the native Unicode path; .string() uses the Windows code page.
    auto held=media; held += ".held";
    std::filesystem::rename(media,held);
    refuses([&]{store.verifyMedia(s);});
    std::filesystem::rename(held,media);
    check(history.structural({RemoveTrackVersion{track,alternate}}), "Inactive version deletion missing");
    check(history.undo() && s==empty, "Version deletion Undo lost arrangement");
    const auto usage=sessionPayloadBytes(s);
    refuses([&]{validate(s,StateBudget{usage-1});});
    refuses([&]{applySessionEdits(s,{CreateTrackVersion{track,"Too much","unused",true}},StateBudget{usage});});
    check(s==empty, "Resource refusal changed versions");
    auto invalid=Json::parse(encodeProject(s));
    invalid["tracks"][0]["inactiveVersions"][0]["clips"][0]["unknown"]=1;
    refuses([&]{decodeProject(invalid.dump());});
    store.save(s); check(store.load()==s, "Empty active version lost inactive edits on disk");
    check(hashMediaFile(media)==rawHash, "Version operation modified raw recording");
    check(encodeProject(initial)==encodeProject(decodeProject(encodeProject(initial))), "Unversioned compatibility changed");
    // Recovery must preserve inactive arrangements, not just the empty active one.
    auto unpublished=s;unpublished.name="Canceled version snapshot";
    refuses([&]{store.save(unpublished,{[]{throw ProjectError(ErrorCode::Canceled,"Owned save cancellation");}});});
    check(store.load()==s && store.loadPrevious()==s,"Canceled save lost retained versions");
    for(const auto &entry:std::filesystem::directory_iterator(d.root))
        check(entry.path().extension()!=".partial","Canceled version save leaked temporary file");
    auto moved=d.root;moved+="-relocated";std::filesystem::rename(d.root,moved);d.root=moved;
    ProjectStore relocated(d.root);
    check(relocated.load()==s && relocated.loadPrevious()==s,"Relocation lost inactive versions or previous snapshot");
    {
        std::ofstream damaged(d.root/"project.json",std::ios::binary|std::ios::trunc);
        damaged<<"{broken";damaged.close();check(bool(damaged),"Could not corrupt owned project fixture");
    }
    refuses([&]{relocated.load();});
    const auto raw=d.root/utf8Path(s.assets[0].relativePath);auto missing=raw;missing+=".held";
    std::filesystem::rename(raw,missing);
    refuses([&]{relocated.loadPrevious();});
    std::filesystem::rename(missing,raw);
    auto recovered=relocated.loadPrevious();
    check(recovered==s && recovered.tracks[0].clips.empty(),"Explicit recovery changed empty active or inactive versions");
    EditHistory recoveredHistory(recovered);
    check(recoveredHistory.structural({SwitchTrackVersion{track,original}}),"Recovered original version cannot activate");
    check(live(d.root,recovered,plan,37)==originalAudio && exported(d.root,recovered,plan)==originalAudio,
          "Recovered inactive arrangement changed original samples");
    check(recoveredHistory.undo() && recovered==s,"Recovered version switch Undo lost snapshot");
    check(hashMediaFile(raw)==rawHash,"Snapshot recovery modified original media");

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
    std::vector<SessionEdit> createVersions, switchVersions;
    for (const auto &linked : selected.tracks)
        createVersions.push_back(CreateTrackVersion{linked.id,"Alternate","Original",true});
    auto versioned=selected; EditHistory versionHistory(versioned);
    check(versionHistory.structural(createVersions), "Grouped version creation missing");
    for (const auto &linked : versioned.tracks) {
        switchVersions.push_back(SwitchTrackVersion{linked.id,linked.inactiveVersions[0].identity.id});
        check(linked.takeLanes==std::find_if(selected.tracks.begin(),selected.tracks.end(),
            [&](const auto &old){return old.id==linked.id;})->takeLanes, "Version creation changed recorded group membership");
    }
    const auto alternatives=versioned;
    check(versionHistory.structural(switchVersions), "Grouped version switch missing");
    check(live(d.root,versioned,plan,127)==output, "Grouped version switch changed offsets");
    const auto originals=versioned;
    check(versionHistory.undo() && versioned==alternatives && versionHistory.redo() && versioned==originals,
          "Grouped version switch Undo was not atomic");
    auto linkedVersions=selected; EditHistory linkedHistory(linkedVersions);
    std::vector<Id> trackIds;for(const auto &t:selected.tracks)trackIds.push_back(t.id);
    check(linkedHistory.structural({CreateTrackVersions{trackIds,"Alt — Ελληνικά","Original",true}}), "Shared version creation missing");
    const auto group=*linkedVersions.tracks[0].activeVersion->groupId;
    const auto originalGroup=*linkedVersions.tracks[0].inactiveVersions[0].identity.groupId;
    for(const auto &t:linkedVersions.tracks) {
        check(t.activeVersion->groupId==group && t.inactiveVersions[0].identity.groupId==originalGroup,
              "Versions created together did not share identities");
        check(t.takeLanes==selected.tracks[&t-linkedVersions.tracks.data()].takeLanes, "Version group changed take groups");
    }
    const auto linked=linkedVersions;
    check(linkedHistory.undo() && linkedVersions==selected && linkedHistory.redo() && linkedVersions==linked,"Shared creation Undo/Redo differs");
    check(live(d.root,linkedVersions,plan,127)==output,"Grouped duplicate changed audible samples");
    std::vector<SessionEdit> mute;
    for(const auto &t:linkedVersions.tracks)for(const auto &c:t.clips) {
        auto p=c.processing;p.muted=true;mute.push_back(SetClipProcessing{t.id,c.id,p});
    }
    check(linkedHistory.structural(mute),"Grouped alternate edits missing");
    const auto mutedVersions=linkedVersions;
    check(linkedHistory.structural({EditTrackVersionGroup{trackIds,originalGroup,TrackVersionGroupAction::Activate,{}}}),"Grouped activation missing");
    check(live(d.root,linkedVersions,plan,127)==output && exported(d.root,linkedVersions,plan)==output,
          "Shared group did not activate original audio on every track");
    const auto activated=linkedVersions;
    check(linkedHistory.undo() && linkedVersions==mutedVersions && linkedHistory.redo() && linkedVersions==activated,
          "Shared activation Undo/Redo was not atomic");
    check(linkedHistory.structural({EditTrackVersionGroup{trackIds,group,TrackVersionGroupAction::Rename,"Muted — Україна"}}),"Shared rename missing");
    for(const auto &t:linkedVersions.tracks)check(t.inactiveVersions[0].identity.name=="Muted — Україна","Shared rename skipped a track");
    ProjectStore(d.root).save(linkedVersions);check(ProjectStore(d.root).load()==linkedVersions,"Grouped versions lost on disk");
    auto document=Json::parse(encodeProject(linkedVersions));check(document.at("schemaMinor")==25,"Grouped schema not negotiated");
    document["schemaMinor"]=24;refuses([&]{decodeProject(document.dump());});
    const auto beforeRefusal=linkedVersions;
    auto missing=trackIds;missing.push_back(Id::generate());
    refuses([&]{linkedHistory.structural({EditTrackVersionGroup{missing,group,TrackVersionGroupAction::Activate,{}}});});
    auto repeated=trackIds;repeated.push_back(trackIds[0]);
    refuses([&]{linkedHistory.structural({CreateTrackVersions{repeated,"Bad","Bad",true}});});
    refuses([&]{linkedHistory.structural({EditTrackVersionGroup{trackIds,originalGroup,TrackVersionGroupAction::Remove,{}}});});
    refuses([&]{linkedHistory.structural({EditTrackVersionGroup{trackIds,group,TrackVersionGroupAction(99),{}}});});
    check(linkedVersions==beforeRefusal,"Refused grouped edit partially changed tracks");
    auto invalid=linkedVersions;invalid.tracks[0].activeVersion->groupId=group;
    refuses([&]{validate(invalid);}); // Same group cannot select two versions of a track.
    invalid=linkedVersions;invalid.tracks[0].activeVersion->groupId=invalid.tracks[0].id;
    refuses([&]{validate(invalid);});
    invalid=linkedVersions;invalid.tracks[0].activeVersion->groupId=invalid.tracks[0].takeLanes[0].groupId;
    refuses([&]{validate(invalid);});
    check(linkedHistory.structural({EditTrackVersionGroup{trackIds,group,TrackVersionGroupAction::Remove,{}}}),"Grouped inactive deletion missing");
    check(linkedHistory.undo() && linkedVersions==beforeRefusal,"Grouped deletion Undo lost edits");
    std::vector<Id> subset{trackIds[5],trackIds[6],trackIds[7]};
    check(linkedHistory.structural({LinkActiveTrackVersions{subset,"Original"}}),"Linking active arrangements missing");
    const auto relinked=linkedVersions;const auto newGroup=*linkedVersions.tracks[5].activeVersion->groupId;
    check(newGroup!=originalGroup && linkedVersions.tracks[6].activeVersion->groupId==newGroup &&
          linkedVersions.tracks[3].activeVersion->groupId==originalGroup,"Linking changed unselected tracks");
    refuses([&]{linkedHistory.structural({EditTrackVersionGroup{trackIds,originalGroup,TrackVersionGroupAction::Activate,{}}});});
    refuses([&]{linkedHistory.structural({EditTrackVersionGroup{trackIds,originalGroup,TrackVersionGroupAction::Rename,"Must roll back earlier peers"}});});
    check(linkedVersions==relinked && linkedHistory.undo() && linkedVersions==beforeRefusal,"Missing peer refusal or relink Undo was partial");
    check(live(d.root,linkedVersions,plan,127)==output,"Linking version metadata changed audio");
    auto wide=makeOneTrackSession("Large group","Track");std::vector<Id> wideIds{wide.tracks[0].id};
    for(unsigned i=1;i<129;++i){wide.tracks.push_back(makeAudioTrack("Track",{},48000));wideIds.push_back(wide.tracks.back().id);}
    const auto wideOriginal=wide;EditHistory wideHistory(wide);
    check(wideHistory.structural({CreateTrackVersions{wideIds,"Alternate","Original",false}}),"Version group inherited edit batch track cap");
    const auto wideAlternate=wide;const auto wideGroup=wide.tracks[0].activeVersion->groupId;
    const auto wideOriginalGroup=*wide.tracks[0].inactiveVersions[0].identity.groupId;
    for(const auto &t:wide.tracks)check(t.activeVersion && t.activeVersion->groupId==wideGroup &&
        t.inactiveVersions.size()==1 && t.inactiveVersions[0].identity.groupId==wideOriginalGroup,
        "Large group skipped a selected track");
    check(decodeProject(encodeProject(wide))==wide,"Large group persistence differs");
    check(wideHistory.structural({EditTrackVersionGroup{wideIds,wideOriginalGroup,TrackVersionGroupAction::Activate,{}}}),
        "Large group activation missing");
    for(const auto &t:wide.tracks)check(t.activeVersion->groupId==wideOriginalGroup,"Large activation skipped a track");
    check(wideHistory.undo() && wide==wideAlternate && wideHistory.undo() && wide==wideOriginal,
        "Large group creation/switch Undo was not atomic");
    const auto admittedGroup=sessionPayloadBytes(wide);
    refuses([&]{applySessionEdits(wide,{CreateTrackVersions{wideIds,"No budget","Original",true}},StateBudget{admittedGroup});});
    check(wide==wideOriginal,"Group budget refusal changed project");
    auto budgeted=makeOneTrackSession("Many versions","Track");
    for (unsigned n=0;n<65;++n)
        applySessionEdits(budgeted,{CreateTrackVersion{budgeted.tracks[0].id,"Version "+std::to_string(n),"Original",false}});
    check(decodeProject(encodeProject(budgeted))==budgeted, "Version count acquired a fixed small cap");
    check(decodeProject(encodeProject(many))==many,"Take count acquired a fixed small cap");
    const auto admitted=sessionPayloadBytes(many);
    refuses([&]{validate(many,StateBudget{admitted-1});});
}
}
int main() {
    try { trackVersions(); workflow(); groupsAndTiming(); fiveTakeTransitions(); groupedTransitionOffsets(); recordedTakeAudition(); check((rt_audit::counts.cppAllocate+rt_audit::counts.cppFree+rt_audit::counts.cAllocate+rt_audit::counts.cFree+rt_audit::counts.blockingLock)==0,"Comp callback allocated/freed/locked");
        std::cout<<"retained_take_comp_checks="<<checks<<" callback_audit="<<(rt_audit::counts.cppAllocate+rt_audit::counts.cppFree+rt_audit::counts.cAllocate+rt_audit::counts.cFree+rt_audit::counts.blockingLock)<<" raw_unchanged=true save_reopen=true grouped_offsets=true\n";
    } catch(const std::exception &e) { std::cerr<<e.what()<<'\n'; return 1; }
}
