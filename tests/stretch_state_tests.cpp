// SPDX-License-Identifier: GPL-3.0-only
#include <soundcurrent/stretch.hpp>
#include <soundcurrent/clip_timing.hpp>
#include <soundcurrent/project_store.hpp>
#include <nlohmann/json.hpp>
#include <iostream>
#include <limits>
#include <algorithm>
using namespace soundcurrent::daw;
namespace {
unsigned checks=0;
void check(bool v,const char *message) {++checks;if(!v) throw std::runtime_error(message);}
template<class F> void refuses(F f) {
    try {f();} catch(const ProjectError &) {++checks;return;}
    throw std::runtime_error("Invalid stretch state/edit accepted");
}
Session source() {
    auto s=makeOneTrackSession("Stretch — Українська","Raw");
    Asset a;a.relativePath="media/raw.wav";a.sha256=std::string(64,'a');a.frames=60000;s.assets={a};
    Clip c;c.assetId=a.id;c.startFrame=100;c.sourceFrame=17;c.sourceTiming={1,3};c.lengthFrames=8192;
    c.processing.gainDb=4;c.processing.fadeIn={-33,100,ClipFadeCurve::EqualPower,.7};
    c.processing.fadeOut={5120,8000,ClipFadeCurve::Smoothstep,1.2};s.tracks[0].clips={c};validate(s);return s;
}
ApplyClipStretch result(const Session &s,const Id &clip,StretchSettings settings) {
    const auto plan=prepareClipStretch(s,s.tracks[0].id,clip,settings);
    Asset rendered=plan.source;rendered.id=Id::generate();
    rendered.relativePath="media/derived/"+rendered.id.str()+"/audio.wav";
    rendered.sha256=std::string(64,'b');rendered.frames=stretchOutputFrames(plan.anchor.sourceFrames,plan.anchor.settings);
    auto anchor=plan.anchor;anchor.renderKey=std::string(64,'c');
    return {plan.trackId,plan.expectedClip.id,plan.expectedClip,plan.source,rendered,std::move(anchor)};
}
void workflow() {
    auto s=source();const auto original=s;const auto track=s.tracks[0].id,clip=s.tracks[0].clips[0].id;
    auto command=result(s,clip,{3,2,700000,true});
    check(command.value.sourceOrigin==SourcePosition{17,1,3} && command.value.sourceFrames==8192,
          "Raw fractional anchor or selected span lost");
    EditHistory history(s);check(history.structural({command}),"Stretch adoption made no edit");
    const auto stretched=s;const auto c=s.tracks[0].clips[0];
    check(s.assets.size()==2 && s.assets[0]==original.assets[0] && c.stretch==command.value,
          "Stretch replaced raw asset or lost state");
    check(c.sourceFrame==0 && c.sourceTiming==ClipSourceTiming{} && c.lengthFrames==12288 && c.startFrame==100,
          "Initial render origin/duration changed");
    check(c.processing.fadeIn.startFrame==-50 && c.processing.fadeIn.endFrame==150 &&
          c.processing.fadeOut.startFrame==7680 && c.processing.fadeOut.endFrame==12000,
          "Signed stretch fade anchors did not use exact floor/ceiling");
    check(decodeProject(encodeProject(s))==s,"Stretch state roundtrip changed identities/settings");
    check(history.undo() && s==original && history.redo() && s==stretched,"Stretch was not one exact Undo/Redo operation");
    const auto right=Id::generate();history.structural({SplitClip{track,clip,right,4197}});
    check(s.tracks[0].clips[1].sourceFrame==4097 && s.tracks[0].clips[1].stretch==c.stretch,
          "Split restarted the raw stretch anchor");
    history.structural({CropClip{track,right,4200,37,8000}});
    const auto cropped=s;auto change=result(s,right,{1,2,-1200000,false});
    check(change.source==original.assets[0] && change.value.sourceOrigin==command.value.sourceOrigin &&
          change.value.sourceFrames==8192,"Reprocessing selected rendered audio instead of raw anchor");
    history.structural({change});const auto rerendered=s;
    const auto &r=s.tracks[0].clips[1];
    check(r.sourceFrame==1378 && r.sourceTiming==ClipSourceTiming{} && r.lengthFrames==2667 &&
          r.assetId==change.rendered.id && r.stretch==change.value,"Cropped derivative did not retime from full anchor");
    check(s.tracks[0].clips[0].assetId==command.rendered.id && s.assets.size()==3,
          "Re-render disturbed the sibling clip or retired Undo media");
    check(history.undo() && s==cropped && history.redo() && s==rerendered,"Re-render Undo did not retain old/new assets");
    auto stale=command;const auto prior=s;refuses([&]{applySessionEdits(s,{stale});});check(s==prior,"Stale render partially mutated Session");
    s=original;s.tracks[0].name="Renamed during render";s.tracks[0].eq.bands[0].gainDb=2;
    applySessionEdits(s,{command});check(s.tracks[0].name=="Renamed during render" && s.tracks[0].eq.bands[0].gainDb==2,
                                       "Render overwrote unrelated edits");
    for(unsigned mode=0;mode<8;++mode) {
        auto value=command;auto base=original;
        if(mode==0) value.value.sourceSha256=std::string(64,'f');
        if(mode==1) value.value.sourceOrigin.frame++;
        if(mode==2) value.value.sourceFrames--;
        if(mode==3) value.rendered.frames++;
        if(mode==4) value.rendered.sampleRate=44100;
        if(mode==5) value.value.renderKey="invalid";
        if(mode==6) base.assets[0].sha256=std::string(64,'e');
        if(mode==7) {base.assets.push_back(value.rendered);base.assets.back().sha256=std::string(64,'d');}
        const auto unchanged=base;refuses([&]{applySessionEdits(base,{value});});check(base==unchanged,"Bad render adoption changed project");
    }
    // Non-unit speed and mixed physical/project rates retain their distinct domains.
    s=original;s.sampleRate=44100;s.tracks[0].clips[0].playbackRate={3,2};validate(s);
    auto mixed=result(s,clip,{4,3,0,false});applySessionEdits(s,{mixed});validate(s);
    check(s.tracks[0].clips[0].playbackRate==ClipPlaybackRate{3,2} && s.assets.back().sampleRate==48000,
          "Stretch folded source rate or linked playback speed into its processor");
}
void persistence() {
    auto plain=source();auto s=plain;auto c=result(s,s.tracks[0].clips[0].id,{3,2,1200000,false});applySessionEdits(s,{c});
    auto j=nlohmann::json::parse(encodeProject(s));check(j["schemaMinor"]==13,"Stretch schema did not advance");
    check(j["tracks"][0]["clips"][0]["stretch"]["processor"].get<std::string>()==stretchProcessorId,"Processor identity lost");
    for(unsigned mode=0;mode<22;++mode) {
        auto bad=j;auto &v=bad["tracks"][0]["clips"][0]["stretch"];auto &p=v["settings"];
        if(mode==0) v["processor"]="unknown";
        if(mode==1) v["version"]=2;
        if(mode==2) v["sourceAssetId"]=Id::generate().str();
        if(mode==3) v["sourceSha256"]=std::string(64,'f');
        if(mode==4) v["sourceFrames"]=0;
        if(mode==5) v["sourceOrigin"]["frame"]=-1;
        if(mode==6) v["sourceOrigin"]["fraction"]=3;
        if(mode==7) v["sourceOrigin"]["denominator"]=0;
        if(mode==8) v["sourceOrigin"]["fraction"]=true;
        if(mode==9) v["sourceOrigin"]["algorithm"]="unknown";
        if(mode==10) p["timeNumerator"]=1.5;
        if(mode==11) p["timeDenominator"]=false;
        if(mode==12) p["timeNumerator"]=1000001;
        if(mode==13) p["pitchMilliCents"]=UINT64_MAX;
        if(mode==14) p["formantPreserved"]=1;
        if(mode==15) v["renderKey"]="bad";
        if(mode==16) v["unknown"]=0;
        if(mode==17) v.erase("settings");
        if(mode==18) bad["tracks"][0]["clips"][0].erase("stretch");
        if(mode==19) bad["schemaMinor"]=14;
        if(mode==20) p["timeNumerator"]=6; // Noncanonical 6/4.
        if(mode==20) p["timeDenominator"]=4;
        if(mode==21) v["sourceOrigin"]["fraction"]=2;
        if(mode==21) v["sourceOrigin"]["denominator"]=6;
        refuses([&]{decodeProject(bad.dump());});
    }
    const auto raw=nlohmann::json::parse(encodeProject(plain));
    for(unsigned minor=9;minor<=11;++minor) {
        auto old=raw;old["schemaMinor"]=minor;auto &clip=old["tracks"][0]["clips"][0];clip.erase("stretch");
        if(minor<11) clip.erase("playbackRate");
        if(minor<10) clip.erase("sourceTiming");
        auto expected=plain;if(minor<10) expected.tracks[0].clips[0].sourceTiming={};
        check(decodeProject(old.dump())==expected,"Legacy migration invented a stretch anchor");
    }
    const auto bytes=sessionPayloadBytes(s);refuses([&]{validate(s,StateBudget{bytes-1});});
    check(bytes>sessionPayloadBytes(plain),"Raw anchor/asset payload was not charged");
}
void unityIdentity() {
    auto s=source();const auto original=s;
    auto command=result(s,s.tracks[0].clips[0].id,{1,1,0,true});
    check(command.value.processor==unityStretchProcessorId,"Unity settings selected the phase processor");
    EditHistory history(s);history.structural({command});const auto adopted=s;
    check(s.tracks[0].clips[0].lengthFrames==8192 && decodeProject(encodeProject(s))==s,
          "Unity state lost exact duration or persisted processor identity");
    check(history.undo() && s==original && history.redo() && s==adopted,
          "Unity adoption did not retain raw/derived Undo state");
    auto invalid=command.value;invalid.settings.pitchMilliCents=1;
    refuses([&]{validateClipStretch(invalid,command.source,command.rendered);});
    // Existing 1.12 R3 unity assets must remain R3 and keep their original keys.
    auto legacy=adopted;legacy.tracks[0].clips[0].stretch->processor=stretchProcessorId;
    auto json=nlohmann::json::parse(encodeProject(legacy));json["schemaMinor"]=12;
    check(decodeProject(json.dump())==legacy,"Legacy R3 unity asset was silently reidentified");
    json["tracks"][0]["clips"][0]["stretch"]["processor"]=unityStretchProcessorId;
    refuses([&]{decodeProject(json.dump());});
    check(prepareClipStretch(legacy,legacy.tracks[0].id,legacy.tracks[0].clips[0].id,{1,1,0,false}).anchor.processor==unityStretchProcessorId,
          "Explicit legacy rerender did not select the new unity algorithm");
}
int probe() {
    std::string line;unsigned count=0;
    while(std::getline(std::cin,line)) {
        if(++count>4096 || line.size()>4096) return 1;
        auto j=nlohmann::json::parse(line);nlohmann::json out;
        try {
            if(j["kind"]=="position") {
                auto p=scaleSourcePosition({j["frame"].get<Frame>(),j["fraction"].get<std::uint64_t>(),j["denominator"].get<std::uint64_t>()},
                                           j["n"].get<std::uint64_t>(),j["d"].get<std::uint64_t>());
                out={{"accepted",true},{"frame",p.frame},{"fraction",p.fraction},{"denominator",p.denominator}};
            } else out={{"accepted",true},{"frame",scaleStretchFrame(j["frame"].get<Frame>(),j["n"].get<std::uint64_t>(),j["d"].get<std::uint64_t>(),j["ceiling"].get<bool>())}};
        } catch(const ProjectError &) {out={{"accepted",false}};}
        std::cout<<out.dump()<<'\n';
    }
    return 0;
}
}
int main(int argc,char **argv) {try {
    if(argc==2 && std::string_view(argv[1])=="--arithmetic-probe") return probe();
    workflow();persistence();unityIdentity();std::cout<<"stretch_state_checks="<<checks<<" raw_anchor_retained=true split_crop_rerender=true undo_redo=true unity_identity=true legacy_r3_preserved=true schema=1.13\n";return 0;
}catch(const std::exception &e){std::cerr<<e.what()<<'\n';return 1;}}
