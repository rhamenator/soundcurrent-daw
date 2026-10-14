// SPDX-License-Identifier: GPL-3.0-only
#include <soundcurrent/comp_handle_plan.hpp>
#include <soundcurrent/clip_timing.hpp>
#include <soundcurrent/project_store.hpp>
#include <nlohmann/json.hpp>
#include <iostream>
using namespace soundcurrent::daw;
namespace {
unsigned checks=0;
void check(bool v,const char *m){++checks;if(!v)throw std::runtime_error(m);}
template<class F>void refuses(F f){try{f();}catch(const ProjectError &){++checks;return;}throw std::runtime_error("Invalid processed window accepted");}
Session fixture(unsigned mode) {
    auto s=makeOneTrackSession("Handles — Ελληνικά","Audio");auto &t=s.tracks[0];
    Asset raw;raw.frames=4096;raw.relativePath="media/raw.wav";raw.sha256=std::string(64,'a');
    Asset nominal=raw;nominal.id=Id::generate();nominal.relativePath="media/base.wav";nominal.sha256=std::string(64,'b');nominal.frames=mode==1?504:384;
    TakeLane lane;lane.captureEndFrame=4096;Clip take;take.assetId=raw.id;take.lengthFrames=4096;lane.clips={take};t.takeLanes={lane};
    Clip c;c.startFrame=74;c.lengthFrames=436;c.takeOrigin=ClipTakeOrigin{t.id,lane.id,take.id};
    ClipStretchAnchor p;p.sourceAssetId=raw.id;p.sourceSha256=raw.sha256;p.sourceOrigin={512,1,7};p.sourceFrames=256;
    p.settings={3,2,0,true};p.renderKey=std::string(64,'c');
    if(mode==1)p.context=StretchContext{32,48};
    if(mode==2){WarpSettings w;w.protection={8,16,2,8};w.markers.push_back({Id::generate(),{128,0,1},{192,0,1}});p.warp=w;}
    p.processor=stretchProcessorFor(p.settings,p.context,p.warp);c.stretch=p;
    ClipCompHandleWindow window;window.baseAssetId=nominal.id;window.baseSha256=nominal.sha256;
    window.prefixFrames=window.suffixFrames=mode==1?0:26;window.renderKey=mode==1?p.renderKey:std::string(64,'d');
    Asset extended=nominal;
    if(mode!=1){extended.id=Id::generate();extended.relativePath="media/handles.wav";extended.sha256=std::string(64,'e');extended.frames=436;}
    c.assetId=extended.id;c.sourceFrame=mode==1?22:0;c.compHandles=window;
    s.assets={raw,nominal};if(mode!=1)s.assets.push_back(extended);t.clips={c};validate(s);return s;
}
}
int main(){try {
    const CompHandlePlanContext context{19,"root/é-日本"};
    for(unsigned mode=0;mode<3;++mode) {
        auto s=fixture(mode);const auto original=s;const auto track=s.tracks[0].id;const auto clip=s.tracks[0].clips[0].id;
        const auto encoded=encodeProject(s);const auto j=nlohmann::json::parse(encoded);
        check(j["schemaMinor"]==20 && decodeProject(encoded)==s,"Handle project did not roundtrip through1.20");
        const auto c=s.tracks[0].clips[0];
        check(c.stretch->sourceOrigin==SourcePosition{512,1,7} && c.stretch->sourceFrames==256 && c.stretch->renderKey==std::string(64,'c'),"Nominal anchor was replaced");
        check(processedClipOutputToRaw(c,s.assets[0],{c.sourceFrame,0,1})==(mode==2?SourcePosition{495,30,161}:SourcePosition{494,17,21}),"Independent raw prefix Fraction differs");
        check(processedClipOutputToRaw(c,s.assets[0],{c.sourceFrame+436,0,1})==(mode==2?SourcePosition{784,53,77}:SourcePosition{785,10,21}),"Independent raw suffix Fraction differs");
        EditHistory history(s);
        check(history.structural({SplitClip{track,clip,Id::generate(),300}}),"Handle split did not apply");
        check(s.tracks[0].clips.size()==2 && s.tracks[0].clips[0].compHandles==c.compHandles && s.tracks[0].clips[1].compHandles==c.compHandles,"Split discarded processed window");
        const auto split=s;
        check(history.structural({PrepareCompTransition{track,s.tracks[0].clips[0].id,s.tracks[0].clips[1].id,285,315,ClipFadeCurve::EqualPower}}),
            "Processed handle pair did not extend atomically");
        check(s.tracks[0].clips[0].stretch==c.stretch && s.tracks[0].clips[1].compHandles==c.compHandles,
            "Paired transition replaced nominal/window provenance");
        check(history.undo() && s==split,"Paired processed transition did not undo once");
        check(history.undo() && s==original && history.redo() && decodeProject(encodeProject(s))==s,"Handle split Undo/Redo/reopen differs");
        check(history.undo() && s==original,"Second handle Undo differs");
        auto plan=prepareCompHandleEnvelope(s,track,clip,64,520,context,ResourceLedger{});
        check(plan.data().nominalAsset==s.assets[1] && plan.data().expected.compHandles==original.tracks[0].clips[0].compHandles,"Further preparation lost original artifact");
        check(plan.data().prefixFrames==(mode==1?0:10) && plan.data().suffixFrames==(mode==1?0:10),"Further preparation reinterpreted old handles");
        verifyCompHandleEnvelopeCurrent(s,context,plan);
        auto stale=s;stale.assets[1].sha256=std::string(64,'f');refuses([&]{verifyCompHandleEnvelopeCurrent(stale,context,plan);});
        refuses([&]{prepareClipStretch(s,track,clip,{2,1,0,true});});
        for(unsigned bad=0;bad<8;++bad) {
            auto malformed=j;auto &w=malformed["tracks"][0]["clips"][0]["compHandles"];
            if(bad==0)w["prefixFrames"]=-1;
            if(bad==1)w["suffixFrames"]=INT64_MAX;
            if(bad==2)w["baseSha256"]=std::string(64,'f');
            if(bad==3)w["version"]=2;
            if(bad==4)w["processor"]="unknown";
            if(bad==5)w["unexpected"]=true;
            if(bad==6)w["renderKey"]="invalid";
            if(bad==7)malformed["tracks"][0]["clips"][0]["stretch"]=nullptr;
            refuses([&]{decodeProject(malformed.dump());});
        }
        auto uncovered=original;uncovered.tracks[0].takeLanes[0].clips[0].sourceFrame=512;uncovered.tracks[0].takeLanes[0].clips[0].lengthFrames=256;
        refuses([&]{validate(uncovered);});
    }
    {
        auto edge=fixture(0);auto &t=edge.tracks[0];auto &c=t.clips[0];
        edge.assets[0].frames=768;t.takeLanes[0].captureEndFrame=768;t.takeLanes[0].clips[0].lengthFrames=768;
        c.assetId=edge.assets[1].id;edge.assets.pop_back();c.startFrame=100;c.sourceFrame=0;c.lengthFrames=384;
        c.compHandles->prefixFrames=c.compHandles->suffixFrames=0;c.compHandles->renderKey=c.stretch->renderKey;
        validate(edge);check(decodeProject(encodeProject(edge))==edge,"Fractional source-edge window did not reopen");
        auto plan=prepareCompHandleEnvelope(edge,t.id,c.id,100,484,context,ResourceLedger{});
        check(plan.data().requestedRawEnd==SourcePosition{768,1,7} && !plan.data().needsNewSpan,
            "Exclusive endpoint rejected physically present samples");
    }
    auto plain=makeOneTrackSession("Old","Track");auto j=nlohmann::json::parse(encodeProject(plain));
    check(j["schemaMinor"]==18 && decodeProject(j.dump())==plain,"Ordinary project encoding changed");
    std::cout<<"comp_window_state_checks="<<checks<<" nominal_anchor_retained=true schema=1.20 actual_render=false\n";return 0;
}catch(const std::exception &e){std::cerr<<e.what()<<'\n';return 1;}}
