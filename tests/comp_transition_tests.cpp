// SPDX-License-Identifier: GPL-3.0-only
#include <soundcurrent/session.hpp>
#include <soundcurrent/clip_timing.hpp>
#include <soundcurrent/stretch.hpp>
#include <soundcurrent/project_store.hpp>
#include <iostream>
#include <limits>
using namespace soundcurrent::daw;
namespace {
unsigned checks=0;
void check(bool value,const char *why) {++checks;if(!value)throw std::runtime_error(why);}
template<class F> void refuses(F fn) {try {fn();}catch(const ProjectError &){++checks;return;}throw std::runtime_error("Invalid comp transition accepted");}
struct Fixture {
    Session session=makeOneTrackSession("Comp handles","Track");
    Id track=session.tracks[0].id;
    Fixture(std::uint32_t rate=48000,ClipPlaybackRate speed={}) {
        auto &t=session.tracks[0];
        for(unsigned n=0;n<2;++n) {
            Asset asset;asset.relativePath="media/take-"+std::to_string(n)+".wav";
            asset.sha256=std::string(64,n?'b':'a');asset.frames=8192;asset.sampleRate=rate;
            TakeLane lane;lane.name="Take "+std::to_string(n);lane.captureEndFrame=512;
            Clip source;source.assetId=asset.id;source.sourceFrame=30;
            source.lengthFrames=512;source.sourceTiming={1,7};source.playbackRate=speed;
            source.processing.gainDb=n ? -2 : 3;
            source.processing.fadeIn={0,17,ClipFadeCurve::Smoothstep,1};
            source.processing.fadeOut={480,512,ClipFadeCurve::Linear,1};
            lane.clips.push_back(source);t.takeLanes.push_back(lane);session.assets.push_back(asset);
        }
        validate(session);
        applySessionEdits(session,{SelectTakeRegion{track,t.takeLanes[0].id,0,256},SelectTakeRegion{track,t.takeLanes[1].id,256,512}});
    }
    PrepareCompTransition edit() const {const auto &c=session.tracks[0].clips;return {track,c[0].id,c[1].id,230,282,ClipFadeCurve::Linear};}
};
void geometry() {
    for(const auto rate:{44100u,48000u,96000u}) for(const auto speed:{ClipPlaybackRate{1,1},ClipPlaybackRate{3,2},ClipPlaybackRate{2,3}}) {
        Fixture f(rate,speed);const auto original=f.session;EditHistory h(f.session);
        check(h.structural({f.edit()}),"Transition preparation missing");
        const auto changed=f.session;const auto &old=original.tracks[0].clips,&now=changed.tracks[0].clips;
        check(now[0].startFrame==0 && now[0].lengthFrames==282 && now[1].startFrame==230 && now[1].lengthFrames==282,
              "Prepared overlap geometry differs");
        check(changed.assets==original.assets && changed.tracks[0].takeLanes==original.tracks[0].takeLanes,
              "Transition changed retained takes or assets");
        for(unsigned n=0;n<2;++n) {
            check(now[n].id==old[n].id && now[n].assetId==old[n].assetId && now[n].takeOrigin==old[n].takeOrigin &&
                  now[n].playbackRate==old[n].playbackRate && now[n].processing.gainDb==old[n].processing.gainDb,
                  "Transition lost fragment identity/source/settings");
            const auto before=clipSourceMap(old[n],rate,48000),after=clipSourceMap(now[n],rate,48000);
            for(Frame offset=0;offset<old[n].lengthFrames;++offset) {
                const auto a=before.at(offset),b=after.at(old[n].startFrame-now[n].startFrame+offset);
                check(!sourcePositionLess(a,b) && !sourcePositionLess(b,a),"Original source samples shifted");
            }
        }
        if(rate==44100 && speed==ClipPlaybackRate{3,2}) {
            // Python Fraction: 30 + 1/7 + 230*(44100/48000)*(3/2).
            const SourcePosition expected{347,25,224};const auto actual=clipSourceMap(now[1],rate,48000).at(0);
            check(!sourcePositionLess(actual,expected) && !sourcePositionLess(expected,actual),"Independent fractional handle origin differs");
        }
        check(now[0].processing.fadeIn==old[0].processing.fadeIn && now[1].processing.fadeOut.startFrame==old[1].processing.fadeOut.startFrame+26,
              "Opposite fade phase was not preserved");
        check(decodeProject(encodeProject(changed))==changed,"Transition state does not reopen");
        const auto resources=h.resources();check(!h.structural({f.edit()}) && h.resources()==resources,"Repeated preparation changes history");
        check(h.undo() && f.session==original && h.redo() && f.session==changed,"Transition Undo/Redo not atomic");
    }
}
void groupedRollback() {
    Fixture f;
    for(unsigned n=1;n<8;++n) {
        auto t=makeAudioTrack("Linked",{},f.session.sampleRate);
        auto left=f.session.tracks[0].takeLanes[0],right=f.session.tracks[0].takeLanes[1];
        left.id=Id::generate();right.id=Id::generate();left.clips[0].id=Id::generate();right.clips[0].id=Id::generate();
        t.takeLanes={left,right};const auto id=t.id;f.session.tracks.push_back(t);
        applySessionEdits(f.session,{SelectTakeRegion{id,left.id,0,256},SelectTakeRegion{id,right.id,256,512}});
    }
    const auto original=f.session;EditHistory h(f.session);std::vector<SessionEdit> edits;
    for(const auto &t:f.session.tracks)edits.push_back(PrepareCompTransition{t.id,t.clips[0].id,t.clips[1].id,230,282,ClipFadeCurve::Linear});
    auto bad=std::get<PrepareCompTransition>(edits.back());bad.endFrame=513;edits.back()=bad;
    const auto resources=h.resources();refuses([&]{h.structural(edits);});
    check(f.session==original && h.resources()==resources,"Last-group failure partly published handles");
    bad.endFrame=282;edits.back()=bad;check(h.structural(edits),"Group preparation missing");
    check(h.undo() && f.session==original,"Group preparation needs multiple Undo steps");
    EditHistory tiny(f.session,{},HistoryBudget{32*1024*1024,1,256});
    refuses([&]{tiny.structural({f.edit()});});check(f.session==original,"History budget refusal changed comp");
}
void safety() {
    Fixture f;EditHistory h(f.session);const auto original=f.session;
    auto missing=f.edit();missing.incoming=Id::generate();
    auto self=f.edit();self.incoming=self.outgoing;
    auto before=f.edit();before.startFrame=-1;
    auto after=f.edit();after.endFrame=513;
    auto empty=f.edit();empty.endFrame=empty.startFrame;
    auto wrongCurve=f.edit();wrongCurve.curve=static_cast<ClipFadeCurve>(99);
    for(const auto &edit:{missing,self,before,after,empty,wrongCurve}) {
        const auto resources=h.resources();refuses([&]{h.structural({RenameTrack{f.track,"Must roll back"},edit});});
        check(f.session==original && h.resources()==resources,"Invalid transition partly changed state/history");
    }
    auto noHandles=original;
    auto &source=noHandles.tracks[0].takeLanes[1].clips[0];
    source.sourceFrame=286;source.startFrame=256;source.lengthFrames=256;
    source.processing.fadeIn={};source.processing.fadeOut={};
    // Raw asset has more samples, but they are outside this retained take window.
    validate(noHandles);const auto noHandlesBefore=noHandles;
    refuses([&]{applySessionEdits(noHandles,{f.edit()});});
    check(noHandles==noHandlesBefore,"Missing retained handles were borrowed from other raw audio");
    auto noOrigin=original;noOrigin.tracks[0].clips[1].takeOrigin.reset();validate(noOrigin);
    const auto noOriginBefore=noOrigin;refuses([&]{applySessionEdits(noOrigin,{f.edit()});});
    check(noOrigin==noOriginBefore,"Unproven source was extended");
    auto separated=original;separated.tracks[0].clips[1].startFrame=257;validate(separated);
    const auto separatedBefore=separated;refuses([&]{applySessionEdits(separated,{f.edit()});});
    check(separated==separatedBefore,"Unrelated gap was filled implicitly");
    const auto maximum=UINT64_MAX;
    check(sourcePositionLess({0,1,maximum},{0,1,maximum-1}) &&
          sourcePositionLess({0,maximum-2,maximum-1},{0,maximum-1,maximum}),"Large fractional ordering lost precision");
}
void preparedDerivatives() {
    for(unsigned mode=0;mode<4;++mode) {
        Fixture f;
        auto &s=f.session;
        for(unsigned n=0;n<2;++n) {
            const auto raw=s.assets[n];
            auto &c=s.tracks[0].clips[n];
            ClipStretchAnchor anchor;
            anchor.sourceAssetId=raw.id;anchor.sourceSha256=raw.sha256;
            anchor.sourceOrigin={30,1,7};anchor.sourceFrames=512;
            anchor.settings={3,2,0,true};anchor.renderKey=std::string(64,'e');
            if(mode==1)anchor.context=StretchContext{12,18};
            if(mode==2) {
                WarpSettings warp;warp.protection={8,16,2,8};
                warp.markers.push_back({Id::generate(),{256,0,1},{384,0,1}});
                anchor.warp=warp;
            }
            anchor.processor=stretchProcessorFor(anchor.settings,anchor.context,anchor.warp);
            const auto geometry=stretchGeometry(anchor,raw.frames);
            Asset derived;derived.relativePath="media/derived-"+std::to_string(n)+".wav";
            derived.sha256=std::string(64,n?'d':'c');derived.frames=geometry.outputFrames;
            derived.sampleRate=raw.sampleRate;derived.layout=raw.layout;
            s.assets.push_back(derived);c.assetId=derived.id;c.stretch=anchor;
            c.sourceFrame=geometry.visibleBegin.frame+c.startFrame;
            c.sourceTiming={geometry.visibleBegin.fraction,geometry.visibleBegin.denominator};
            if(mode==3) {
                auto prior=derived;prior.id=Id::generate();
                prior.relativePath="media/retained-render-"+std::to_string(n)+".wav";
                s.assets.push_back(prior);
                auto &retained=s.tracks[0].takeLanes[n].clips[0];
                retained.assetId=prior.id;retained.sourceFrame=geometry.visibleBegin.frame;
                retained.sourceTiming={geometry.visibleBegin.fraction,geometry.visibleBegin.denominator};
                retained.stretch=anchor;
            }
        }
        validate(s);const auto before=s;EditHistory h(s);
        check(h.structural({f.edit()}),"Valid prepared derivative handles were refused");
        const auto after=s;
        check(after.assets==before.assets && after.tracks[0].takeLanes==before.tracks[0].takeLanes,
              "Prepared derivative handles changed raw assets or retained lanes");
        for(unsigned n=0;n<2;++n) {
            const auto &old=before.tracks[0].clips[n],&now=after.tracks[0].clips[n];
            check(now.stretch==old.stretch && now.assetId==old.assetId && now.takeOrigin==old.takeOrigin,
                  "Prepared handles discarded stretch/warp/source provenance");
            const auto oldMap=clipSourceMap(old,48000,48000),newMap=clipSourceMap(now,48000,48000);
            for(Frame i=0;i<old.lengthFrames;++i) {
                const auto a=oldMap.at(i),b=newMap.at(old.startFrame-now.startFrame+i);
                check(!sourcePositionLess(a,b) && !sourcePositionLess(b,a),"Visible derivative samples moved");
            }
        }
        check(h.undo() && s==before && h.redo() && s==after,"Prepared handles undo/redo changed state");
        check(decodeProject(encodeProject(after))==after,"Prepared handle state did not reopen");
        if(mode<2) {
            const auto raw=stretchOutputToRaw(*after.tracks[0].clips[1].stretch,
                clipSourceMap(after.tracks[0].clips[1],48000,48000).at(0));
            check(raw==SourcePosition{183,10,21},"Independent Fraction raw handle origin differs");
        }
        const auto reject=[&](Session bad) {
            validate(bad);const auto unchanged=bad;EditHistory history(bad);const auto resources=history.resources();
            refuses([&]{history.structural({RenameTrack{f.track,"Must roll back"},f.edit()});});
            check(bad==unchanged && history.resources()==resources,"Rendered handle refusal partly changed state/history");
        };
        auto wrongSource=before;
        wrongSource.tracks[0].clips[0].stretch->sourceAssetId=before.assets[1].id;
        wrongSource.tracks[0].clips[0].stretch->sourceSha256=before.assets[1].sha256;
        refuses([&]{validate(wrongSource);});
        check(f.session==after,"Invalid source declaration changed canonical state");
        if(mode==0) {
            auto shortWindow=before;auto &retained=shortWindow.tracks[0].takeLanes[1].clips[0];
            retained.sourceFrame=200;retained.startFrame=256;retained.lengthFrames=256;
            retained.processing.fadeIn={};retained.processing.fadeOut={};reject(shortWindow);
            auto missing=before;auto &out=missing.tracks[0].clips[0];
            out.stretch->sourceFrames=256;out.stretch->settings={1,1,0,true};
            out.stretch->processor=unityStretchProcessorId;
            missing.assets[2].frames=256;reject(missing);
        }
    }
}
void exactAddition() {
    check(addSourcePosition({17,1,7},{1150,3,5})==SourcePosition{1167,26,35},"Fraction addition changed raw origin");
    check(addSourcePosition({INT64_MAX-1,1,2},{0,1,2})==SourcePosition{INT64_MAX,0,1},"Exact addition lost carry");
    check(addSourcePosition({0,1,UINT64_MAX},{0,1,UINT64_MAX})==SourcePosition{0,2,UINT64_MAX},"Large denominator addition changed precision");
    refuses([]{addSourcePosition({INT64_MAX,1,2},{0,1,2});});
    refuses([]{addSourcePosition({0,1,UINT64_MAX},{0,1,UINT64_MAX-1});});
    refuses([]{addSourcePosition({-1,0,1},{0,0,1});});
}

}
int main() {try {geometry();groupedRollback();safety();preparedDerivatives();exactAddition();std::cout<<"comp_transition_checks="<<checks<<" exact_source_handles_preserved=true\n";return 0;}catch(const std::exception &e){std::cerr<<e.what()<<'\n';return 1;}}
