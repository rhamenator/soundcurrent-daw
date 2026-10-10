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
ApplyClipStretch result(const Session &s,const Id &clip,StretchSettings settings,std::optional<StretchContext> context={},const std::optional<WarpSettings> &warp={}) {
    const auto plan=prepareClipStretch(s,s.tracks[0].id,clip,settings,context,warp);
    Asset rendered=plan.source;rendered.id=Id::generate();
    rendered.relativePath="media/derived/"+rendered.id.str()+"/audio.wav";
    rendered.sha256=std::string(64,'b');rendered.frames=stretchGeometry(plan.anchor,plan.source.frames).outputFrames;
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
    auto j=nlohmann::json::parse(encodeProject(s));check(j["schemaMinor"]==15,"Stretch schema did not advance");
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
        if(mode==19) bad["schemaMinor"]=16;
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
    json["tracks"][0]["clips"][0]["stretch"].erase("context");
    json["tracks"][0]["clips"][0]["stretch"].erase("warp");
    check(decodeProject(json.dump())==legacy,"Legacy R3 unity asset was silently reidentified");
    json["tracks"][0]["clips"][0]["stretch"]["processor"]=unityStretchProcessorId;
    refuses([&]{decodeProject(json.dump());});
    check(prepareClipStretch(legacy,legacy.tracks[0].id,legacy.tracks[0].clips[0].id,{1,1,0,false}).anchor.processor==unityStretchProcessorId,
          "Explicit legacy rerender did not select the new unity algorithm");
}
void regions() {
    auto s=source();auto &raw=s.tracks[0].clips[0];raw.startFrame=0;raw.sourceFrame=4096;raw.sourceTiming={1,2};raw.lengthFrames=128;
    raw.processing={};const auto original=s;
    const auto command=result(s,raw.id,{3,2,0,true},StretchContext{4095,4096});
    const auto g=stretchGeometry(command.value,command.source.frames);
    check(g.inputOrigin==SourcePosition{1,1,2} && g.inputFrames==8319 && g.outputFrames==12479 &&
          g.visibleBegin==SourcePosition{6142,1,2} && g.visibleEnd==SourcePosition{6334,1,2},"Odd context geometry lost requested crop phase");
    EditHistory history(s);history.structural({command});const auto adopted=s;
    check(s.tracks[0].clips[0].lengthFrames==192 && s.tracks[0].clips[0].sourceFrame==6142 &&
          s.tracks[0].clips[0].sourceTiming==ClipSourceTiming{1,2},"Full-region rounding changed192-frame visible duration");
    check(s.assets.back().frames==12479 && decodeProject(encodeProject(s))==s,"Full region/visible crop did not persist separately");
    check(history.undo() && s==original && history.redo() && s==adopted,"Context adoption lost semantic Undo/Redo");
    const auto right=Id::generate();history.structural({SplitClip{s.tracks[0].id,s.tracks[0].clips[0].id,right,96}});
    const auto split=s;const auto rerender=result(s,right,{1,1,0,true},StretchContext{2,3});history.structural({rerender});
    const auto &c=s.tracks[0].clips[1];check(c.sourceFrame==66 && c.sourceTiming==ClipSourceTiming{} && c.lengthFrames==64 &&
          c.stretch->sourceOrigin==SourcePosition{4096,1,2} && c.stretch->context==std::optional(StretchContext{2,3}),
          "Split rerender recursed into derivative or lost exact raw-relative crop");
    check(s.tracks[0].clips[0]==split.tracks[0].clips[0] && history.undo() && s==split,"Context rerender changed sibling or Undo media");
    for(auto bad:{StretchContext{-1,0},StretchContext{4097,0},StretchContext{0,60000},StretchContext{1000000000,1000000000}})
        refuses([&]{prepareClipStretch(original,original.tracks[0].id,original.tracks[0].clips[0].id,{3,2,0,false},bad);});
    auto malformed=adopted;malformed.tracks[0].clips[0].sourceFrame=0;refuses([&]{validate(malformed);});
    auto json=nlohmann::json::parse(encodeProject(adopted));
    for(unsigned mode=0;mode<8;++mode) {
        auto bad=json;auto &p=bad["tracks"][0]["clips"][0]["stretch"];
        if(mode==0)p["context"]["before"]=-1;
        if(mode==1)p["context"]["after"]=true;
        if(mode==2)p["context"]["unknown"]=1;
        if(mode==3)p["context"].erase("after");
        if(mode==4)p["processor"]=stretchProcessorId;
        if(mode==5)p["context"]=nullptr;
        if(mode==6)bad["schemaMinor"]=13;
        if(mode==7)p.erase("context");
        refuses([&]{decodeProject(bad.dump());});
    }
    auto old=adopted;old.tracks[0].clips[0].stretch->context.reset();old.tracks[0].clips[0].stretch->processor=stretchProcessorId;
    old.assets.back().frames=192;old.tracks[0].clips[0].sourceFrame=0;old.tracks[0].clips[0].sourceTiming={};
    json=nlohmann::json::parse(encodeProject(old));json["schemaMinor"]=13;json["tracks"][0]["clips"][0]["stretch"].erase("context");
    json["tracks"][0]["clips"][0]["stretch"].erase("warp");
    check(decodeProject(json.dump())==old,"Schema1.13 migration changed legacy rounded maps or keys");
}
void firstWarpEndpoint() {
    // Independent rational oracles for an 8,193-frame visible interval. The
    // prepared raw buffer rounds upward; that last partial frame is not visible.
    struct Case {std::uint32_t rate;ClipPlaybackRate speed;Frame span,doubledLength;};
    const Case cases[]={{44100,{1,1},7528,16385},{48000,{3,2},12290,16386},{44100,{5,4},9410,16386}};
    for(const auto &test:cases)for(const Frame ratio:{1,2}) {
        auto s=source();s.assets[0].sampleRate=test.rate;
        auto &clip=s.tracks[0].clips[0];clip.sourceTiming={};clip.lengthFrames=8193;clip.playbackRate=test.speed;
        const auto original=s;const auto id=clip.id;
        WarpSettings warp;warp.markers.push_back({Id::generate(),{4096,0,1},{4096*ratio,0,1}});
        auto command=result(s,id,{std::uint32_t(ratio),1,0,true},{},warp);
        check(command.value.sourceFrames==test.span,"First warp did not retain its rounded preparation span");
        EditHistory history(s);history.structural({command});const auto adopted=s;
        const auto &actual=s.tracks[0].clips[0];
        check(actual.lengthFrames==(ratio==1?8193:test.doubledLength),"First warp exposed a rounded partial source frame");
        check(actual.sourceFrame==0 && actual.sourceTiming==ClipSourceTiming{} && actual.playbackRate==test.speed,"First warp folded source/project timing domains");
        if(ratio==1)check(actual.processing==original.tracks[0].clips[0].processing,"Identity first warp moved clip fades");
        check(decodeProject(encodeProject(s))==s,"Fractional-end first warp did not roundtrip");
        check(history.undo() && s==original && history.redo() && s==adopted,"Fractional-end first warp lost exact Undo/Redo");
    }
}
void warpWorkflow() {
    auto s=source();auto &raw=s.tracks[0].clips[0];raw.sourceTiming={};raw.lengthFrames=32768;
    const auto original=s;const auto track=s.tracks[0].id,clip=raw.id;
    WarpSettings uniform;const Frame events[]={4096,12288,20480,28672};
    for(auto frame:events)uniform.markers.push_back({Id::generate(),{frame,0,1},{frame*3/2,0,1}});
    auto command=result(s,clip,{3,2,0,true},{},uniform);
    check(command.value.processor==protectedWarpProcessorId && command.value.sourceOrigin==SourcePosition{17,0,1},"Warp lost original source origin/mode");
    EditHistory history(s);history.structural({command});const auto adopted=s;
    check(s.tracks[0].clips[0].lengthFrames==49152 && s.tracks[0].clips[0].stretch->warp==uniform,"Warp duration/marker IDs changed");
    check(s.tracks[0].clips[0].processing.fadeIn.startFrame==-50 && s.tracks[0].clips[0].processing.fadeIn.endFrame==150,"Warp affine fade policy changed signed anchors");
    check(stretchSourceToOutput(command.value,{8192,0,1})==SourcePosition{11664,16,23},"Independent 39/23 protected-gap forward oracle failed");
    check(stretchOutputToSource(command.value,{12288,0,1})==SourcePosition{8559,23,39},"Independent 23/39 protected-gap inverse oracle failed");
    auto json=nlohmann::json::parse(encodeProject(s));check(decodeProject(json.dump())==s,"Warp original IDs/boundaries did not roundtrip");
    const auto &w=json["tracks"][0]["clips"][0]["stretch"]["warp"];
    check(w["spans"][0]["owner"]==uniform.markers[0].id.str() && w["spans"][0]["sourceBegin"]==nlohmann::json::array({3840,0,1}) && w["points"].size()==14,"Persisted normalized warp roles differ");
    for(unsigned mode=0;mode<16;++mode){auto bad=json;auto &v=bad["tracks"][0]["clips"][0]["stretch"];auto &b=v["warp"];
        if(mode==0)b["spans"][0]["sourceBegin"][0]=3841;
        if(mode==1)b["spans"][0]["owner"]=Id::generate().str();
        if(mode==2)b["points"][0]["source"][0]=0.0;
        if(mode==3)b["markers"][0]["source"][1]=true;
        if(mode==4)b["markers"][1]["id"]=b["markers"][0]["id"];
        if(mode==5)b["markers"][1]["source"]=b["markers"][0]["source"];
        if(mode==6)b["markers"][0]["source"][2]=0;
        if(mode==7)b["markers"][0]["output"][0]=1;
        if(mode==8)b["halo"]=0;
        if(mode==9)b["mode"]="unknown";
        if(mode==10)b["chunkFrames"]=256;
        if(mode==11)b["spans"].erase(b["spans"].begin());
        if(mode==12)b["points"][0]["id"]=Id::generate().str();
        if(mode==13)v["settings"]["pitchMilliCents"]=1;
        if(mode==14)v["context"]={{"before",0},{"after",0}};
        if(mode==15)bad["schemaMinor"]=14;
        refuses([&]{decodeProject(bad.dump());});
    }
    check(history.undo() && s==original && history.redo() && s==adopted,"Warp was not one exact Undo/Redo operation");
    history.structural({CropClip{track,clip,100,12288,4096}});const auto cropped=s;
    auto nonlinear=uniform;const Frame targets[]={6144,16384,32768,43008};for(unsigned i=0;i<4;++i)nonlinear.markers[i].output={targets[i],0,1};
    auto changed=result(s,clip,{3,2,0,true},{},nonlinear);history.structural({changed});const auto &c=s.tracks[0].clips[0];
    check(c.sourceFrame==11447 && c.sourceTiming==ClipSourceTiming{31,39} && c.lengthFrames==3256,"Warp crop rerender did not map old output through raw into new output");
    check(c.stretch->sourceOrigin==SourcePosition{17,0,1} && c.stretch->sourceFrames==32768 && s.assets.size()==3,"Warp rerender recursively selected a derivative");
    check(history.undo() && s==cropped,"Warp rerender Undo lost old derivative/crop");
    auto constant=result(s,clip,{1,1,0,true});history.structural({constant});
    check(s.tracks[0].clips[0].sourceFrame==8559 && s.tracks[0].clips[0].sourceTiming==ClipSourceTiming{23,39} && s.tracks[0].clips[0].lengthFrames==2416,"Warp-to-constant crop conversion lost raw interval");
    auto invalid=command.value;invalid.sourceFrames=INT64_MAX;invalid.context=StretchContext{INT64_MAX,INT64_MAX};invalid.warp.reset();invalid.processor=std::string(regionStretchProcessorId);
    refuses([&]{stretchSourceToOutput(invalid,{0,0,1});});refuses([&]{stretchOutputToSource(invalid,{0,0,1});});
    auto legacy=source();auto legacyEdit=result(legacy,legacy.tracks[0].clips[0].id,{3,2,0,true});applySessionEdits(legacy,{legacyEdit});
    auto old=nlohmann::json::parse(encodeProject(legacy));old["schemaMinor"]=14;old["tracks"][0]["clips"][0]["stretch"].erase("warp");
    check(decodeProject(old.dump())==legacy,"Schema1.14 context/constant migration changed legacy state");
}
int geometryProbe() {
    std::string line;unsigned count=0;
    while(std::getline(std::cin,line)) {
        if(++count>4096 || line.size()>4096)return 1;
        auto j=nlohmann::json::parse(line);nlohmann::json out;
        try {
            ClipStretchAnchor p;p.sourceOrigin=scaleSourcePosition({j["first"].get<Frame>(),j["fraction"].get<std::uint64_t>(),j["fractionDenominator"].get<std::uint64_t>()},1,1);
            p.sourceFrames=j["frames"].get<Frame>();p.settings=canonicalStretchSettings({j["n"].get<std::uint32_t>(),j["d"].get<std::uint32_t>(),j["pitch"].get<std::int32_t>(),true});
            if(j["context"].get<bool>())p.context=StretchContext{j["before"].get<Frame>(),j["after"].get<Frame>()};
            p.processor=stretchProcessorFor(p.settings,p.context);const auto g=stretchGeometry(p,j["available"].get<Frame>());
            auto position=[](SourcePosition v){return nlohmann::json::array({v.frame,v.fraction,v.denominator});};
            out={{"accepted",true},{"inputOrigin",position(g.inputOrigin)},{"inputFrames",g.inputFrames},{"outputFrames",g.outputFrames},
                {"visibleBegin",position(g.visibleBegin)},{"visibleEnd",position(g.visibleEnd)},{"map",nlohmann::json::array({g.mapNumerator,g.mapDenominator})}};
        }catch(const ProjectError &){out={{"accepted",false}};}
        std::cout<<out.dump()<<'\n';
    }return 0;
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
    if(argc==2 && std::string_view(argv[1])=="--geometry-probe") return geometryProbe();
    workflow();persistence();unityIdentity();regions();firstWarpEndpoint();warpWorkflow();std::cout<<"stretch_state_checks="<<checks<<" raw_anchor_retained=true split_crop_rerender=true undo_redo=true unity_identity=true legacy_r3_preserved=true nominal_region_map=true schema=1.15\n";return 0;
}catch(const std::exception &e){std::cerr<<e.what()<<'\n';return 1;}}
