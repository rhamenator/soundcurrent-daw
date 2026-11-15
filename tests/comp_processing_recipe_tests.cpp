// SPDX-License-Identifier: GPL-3.0-only
#include <soundcurrent/comp_processing_recipe.hpp>
#include <soundcurrent/clip_timing.hpp>
#include <nlohmann/json.hpp>
#include "media_io.hpp"
#include <algorithm>
#include <fstream>
#include <iostream>
#include <type_traits>
using namespace soundcurrent::daw;
using Json=nlohmann::json;
namespace {
unsigned checks=0,physical=0,postSrc=0,existing=0;
void check(bool v,const char *m){++checks;if(!v)throw std::runtime_error(m);}
template<class F> void refuses(F f){try{f();}catch(const ProjectError &){++checks;return;}throw std::runtime_error("Invalid recipe accepted");}
Session fixture(unsigned rate,ClipPlaybackRate speed,bool fractional,Frame offset=0){
    auto s=makeOneTrackSession("Processing permission","Audio",48000);auto &t=s.tracks[0];t.layout={LayoutKind::Mono,1};
    Asset raw;raw.relativePath="media/raw.wav";raw.sampleRate=rate;raw.frames=8192;raw.sha256=std::string(64,'a');raw.layout=t.layout;s.assets={raw};
    TakeLane lane;Clip take;take.assetId=raw.id;take.lengthFrames=SourceFrameMap(rate,48000).projectFramesForSource(raw.frames);lane.captureEndFrame=take.lengthFrames;lane.clips={take};t.takeLanes={lane};
    ClipStretchAnchor a;a.sourceAssetId=raw.id;a.sourceSha256=raw.sha256;a.sourceOrigin={1024,1,7};a.sourceFrames=512;a.settings={3,2,0,true};a.renderKey=std::string(64,'b');a.processor=stretchProcessorFor(a.settings);
    Asset base=raw;base.id=Id::generate();base.relativePath="media/render.wav";base.frames=768;base.sha256=std::string(64,'c');s.assets.push_back(base);
    Clip c;c.assetId=base.id;c.startFrame=100;c.sourceFrame=offset;c.playbackRate=speed;c.stretch=a;c.takeOrigin=ClipTakeOrigin{t.id,lane.id,take.id};if(fractional)c.sourceTiming={1,3};
    auto map=clipSourceMap(c,rate,48000);c.lengthFrames=map.projectFramesForSource(base.frames-offset);while(map.at(c.lengthFrames-1).frame>=base.frames)--c.lengthFrames;
    auto in=c;in.id=Id::generate();in.startFrame=c.startFrame+c.lengthFrames;t.clips={c,in};validate(s);return s;
}

Session windowFixture(unsigned mode) {
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
struct Temp {
    std::filesystem::path root=std::filesystem::temp_directory_path()/utf8Path("comp-é-日本-"+Id::generate().str());
    Temp(){std::filesystem::create_directory(root);}
    ~Temp(){std::error_code ignored;std::filesystem::remove_all(root,ignored);}
};
void write(const std::filesystem::path &p,std::string_view b){std::ofstream f(p,std::ios::binary|std::ios::trunc);f.write(b.data(),std::streamsize(b.size()));check(bool(f),"Test sidecar write failed");}
}
int main(){try{
    static_assert(!std::is_copy_constructible_v<CompProcessingRecipe>);
    static_assert(!std::is_move_assignable_v<CompProcessingRecipe>);
    ResourceLedger ledger(192*1024*1024,"Recipe tests");
    const CompRecipeExecutor executor{"soundcurrent.comp-join-quintic-v1",std::string(64,'d')};
    for(unsigned mode=0;mode<11;++mode){
        std::cout<<"recipe_case="<<mode<<'\n';
        auto s=fixture(mode%3==0?44100u:mode%3==1?48000u:96000u,mode%2?ClipPlaybackRate{1,1}:ClipPlaybackRate{3,2},mode%2==0);
        if(mode==6){for(auto &c:s.tracks[0].clips){c.stretch.reset();c.assetId=s.assets[0].id;c.sourceFrame=1024;}}
        if(mode==7){auto source=s.tracks[0];source.id=Id::generate();source.eq.id=Id::generate();source.eq.bands.clear();source.clips.clear();
            for(auto &c:s.tracks[0].clips)c.takeOrigin->trackId=source.id;
            s.tracks[0].takeLanes.clear();s.tracks.push_back(source);
            for(auto &c:s.tracks[0].clips)c.startFrame+=Frame(1)<<40;}
        if(mode>=8){s=windowFixture(mode-8);auto in=s.tracks[0].clips[0];in.id=Id::generate();in.startFrame+=in.lengthFrames;
            if(in.stretch->warp)for(auto &marker:in.stretch->warp->markers)marker.id=Id::generate();
            s.tracks[0].clips.push_back(in);}
        validate(s);const auto original=s;const auto &t=s.tracks[0];const auto boundary=t.clips[1].startFrame;
        const PrepareCompTransition edit{t.id,t.clips[0].id,t.clips[1].id,boundary-26,boundary+26,ClipFadeCurve::EqualPower};
        const CompHandlePlanContext old{7,"old-root-sensitive"},fresh{22,"relocated-root"};const auto op=Id::generate();
        {
            auto plan=prepareCompProcessing(s,edit,old,8,ledger);auto encoded=encodeCompProcessingRecipe(s,old,plan,op,executor,ledger);
            auto decoded=decodeCompProcessingRecipe(encoded.bytes(),op,fresh,executor,encoded.digest(),ledger);
            verifyCompProcessingCurrent(s,fresh,decoded.plan());
            check(decoded.bytes()==encoded.bytes() && decoded.digest()==encoded.digest(),"Recipe changed on reopen");
            check(encoded.bytes().find(old.rootIdentity)==std::string_view::npos,"Runtime root serialized");
            for(auto domain:{decoded.plan().outgoing().domain,decoded.plan().incoming().domain}){
                physical+=domain==CompProcessingDomain::PhysicalArtifact;postSrc+=domain==CompProcessingDomain::ProjectAfterSrc;existing+=domain==CompProcessingDomain::ExistingMedia;}
            refuses([&]{verifyCompProcessingCurrent(s,old,decoded.plan());});
            auto irrelevant=s;irrelevant.name="Different name";irrelevant.tracks[0].name="Renamed";irrelevant.tracks[0].eq.enabled=false;
            verifyCompProcessingCurrent(irrelevant,fresh,decoded.plan());
            for(unsigned bad=0;bad<5;++bad){auto changed=s;
                if(bad==0)changed.id=Id::generate();
                if(bad==1)changed.sampleRate=32000;
                if(bad==2)changed.tracks[0].clips[1].processing.gainDb=3;
                if(bad==3)changed.assets[0].sha256=std::string(64,'f');
                if(bad==4)changed.tracks.back().takeLanes[0].clips[0].lengthFrames--;
                refuses([&]{verifyCompProcessingCurrent(changed,fresh,decoded.plan());});}
            const auto j=Json::parse(encoded.bytes());
            check(decodeCompProcessingRecipe(j.dump(2),op,fresh,executor,encoded.digest(),ledger).digest()==encoded.digest(),"Canonical whitespace changed identity");
            if(mode==0){
                std::string tooMany="[";for(unsigned i=0;i<70000;++i)tooMany+="0,";tooMany.back()=']';
                bool bounded=false;try{(void)decodeCompProcessingRecipe(tooMany,op,fresh,executor,encoded.digest(),ledger);}
                catch(const ProjectError &e){bounded=e.code()==ErrorCode::ResourceLimit;}
                check(bounded,"Parser did not refuse staging before DOM construction");
            }
            for(unsigned bad=0;bad<13;++bad){auto altered=j;
                if(bad==0)altered["format"]="v999";
                if(bad==1)altered["outgoing"]["domain"]=999;
                if(bad==2)altered["outgoing"]["pcm"]["channels"]=256;
                if(bad==3)altered["incoming"]["map"][0]["raw"][0]=999;
                if(bad==4)altered["executor"]["buildSha256"]=std::string(64,'a');
                if(bad==5)altered["maximumBlendProjectFrames"]=8.0;
                if(bad==6)altered["transition"]["start"]=false;
                if(bad==7)altered["extra"]=0;
                if(bad==8)altered["selection"]="{}";
                if(bad==9)altered["curve"]="other";
                if(bad==10)altered["resampler"]="other";
                if(bad==11)altered["maximumBlendProjectFrames"]=9;
                if(bad==12)altered["operation"]=Id::generate().str();
                refuses([&]{decodeCompProcessingRecipe(altered.dump(),op,fresh,executor,encoded.digest(),ledger);});}
            auto duplicate=std::string(encoded.bytes());duplicate.insert(1,"\"format\":\"sc-comp-processing-recipe-v1\",");
            refuses([&]{decodeCompProcessingRecipe(duplicate,op,fresh,executor,encoded.digest(),ledger);});
            refuses([&]{decodeCompProcessingRecipe(std::string(17,'[')+std::string(17,']'),op,fresh,executor,encoded.digest(),ledger);});
            refuses([&]{decodeCompProcessingRecipe(encoded.bytes(),op,fresh,executor,"",ledger);});
            auto limits=CompRecipeLimits{};limits.maximumBytes=8;
            refuses([&]{decodeCompProcessingRecipe(encoded.bytes(),op,fresh,executor,encoded.digest(),ledger,limits);});
            std::stop_source canceled;canceled.request_stop();
            refuses([&]{decodeCompProcessingRecipe(encoded.bytes(),op,fresh,executor,encoded.digest(),ledger,{},canceled.get_token());});
            ResourceLedger tiny(1,"Tiny recipe");refuses([&]{decodeCompProcessingRecipe(encoded.bytes(),op,fresh,executor,encoded.digest(),tiny);});check(!tiny.usage().reservedBytes,"Tiny refusal leaked");
            Temp temp;const auto saved=persistCompProcessingRecipe(temp.root,s,old,plan,op,executor,ledger);
            check(saved==encoded.digest(),"Sidecar changed digest");
            auto reopened=loadCompProcessingRecipe(temp.root,s,fresh,op,executor,saved,ledger);
            check(reopened.bytes()==encoded.bytes(),"Sidecar roundtrip changed recipe");
            const auto path=temp.root/"media"/"comp-recipes"/(op.str()+".json");const auto before=hashMediaFile(path);
            refuses([&]{persistCompProcessingRecipe(temp.root,s,old,plan,op,executor,ledger);});check(hashMediaFile(path)==before,"No-overwrite violated");
            refuses([&]{persistCompProcessingRecipe("relative-root",s,old,plan,Id::generate(),executor,ledger);});
            auto changed=s;changed.tracks[0].clips[1].processing.gainDb=2;
            refuses([&]{loadCompProcessingRecipe(temp.root,changed,fresh,op,executor,saved,ledger);});
            const auto canceledOp=Id::generate();refuses([&]{persistCompProcessingRecipe(temp.root,s,old,plan,canceledOp,executor,ledger,{},canceled.get_token());});
            check(!std::filesystem::exists(path.parent_path()/(canceledOp.str()+".json")),"Canceled publication exists");
            // A supplied matching digest must not bypass derived-scope checks.
            for(const char *field:{"domain","map","blends"}){auto malformed=j;
                if(std::string_view(field)=="domain")malformed["outgoing"][field]=999;
                else if(std::string_view(field)=="map")malformed["outgoing"][field][0]["raw"][0]=999;
                else malformed["outgoing"][field]["suffix"]={{"frames",8},{"permittedFirst",0},{"permittedEnd",1000000}};
                write(path,malformed.dump());const auto alteredDigest=hashMediaFile(path);
                refuses([&]{decodeCompProcessingRecipe(malformed.dump(),op,fresh,executor,alteredDigest,ledger);});}
            write(path,encoded.bytes());
            {media_io::JobLease held(path.parent_path(),false,true);check(held.status()==media_io::LeaseStatus::Held,"Test lock not held");
                refuses([&]{persistCompProcessingRecipe(temp.root,s,old,plan,Id::generate(),executor,ledger);});}
            auto coherent=j;coherent["maximumBlendProjectFrames"]=9;write(path,coherent.dump());
            refuses([&]{loadCompProcessingRecipe(temp.root,s,fresh,op,executor,saved,ledger);});
            write(path,encoded.bytes());
#ifndef _WIN32
            auto real=path;real+=".original";std::filesystem::rename(path,real);std::filesystem::create_symlink(real,path);
            refuses([&]{loadCompProcessingRecipe(temp.root,s,fresh,op,executor,saved,ledger);});
#endif
            for(const auto &entry:std::filesystem::directory_iterator(path.parent_path()))check(entry.path().extension()!=".partial","Publication left owned partial");
            auto moved=std::move(encoded);verifyCompProcessingCurrent(s,old,moved.plan());
            refuses([&]{(void)encoded.plan().outgoing();});
            check(s==original,"Recipe modified original project");
        }
        check(!ledger.usage().reservedBytes,"Recipe retirement leaked credit");
    }
    check(physical && postSrc && existing,"Recipe corpus missed processing domain");
    std::cout<<"comp_recipe_checks="<<checks<<" physical="<<physical<<" post_src="<<postSrc<<" existing="<<existing<<" source_media_verified=false renderer_executed=false\n";return 0;
}catch(const std::exception &e){std::cerr<<e.what()<<'\n';return 1;}}
