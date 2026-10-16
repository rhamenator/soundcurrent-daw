// SPDX-License-Identifier: GPL-3.0-only
#include <soundcurrent/comp_handle_render.hpp>
#include <soundcurrent/project_store.hpp>
#include <soundcurrent/mix_reader.hpp>
#include <soundcurrent/export.hpp>
#include <soundcurrent/wave_validation.hpp>
#include <nlohmann/json.hpp>
#include "rt_audit.hpp"
#include <fstream>
#include <iostream>
#include <algorithm>
#include <array>
#include <cmath>
#include <numbers>
using namespace soundcurrent::daw;
namespace {
using Json=nlohmann::json;
unsigned checks=0;
void check(bool value,const char *why){++checks;if(!value)throw std::runtime_error(why);}
Json read(const std::filesystem::path &p){std::ifstream f(p);Json j;f>>j;check(bool(f),"Fixture JSON read failed");return j;}
void write(const std::filesystem::path &p,const Json &j){std::ofstream f(p);f<<j.dump(2)<<'\n';check(bool(f),"Fixture JSON write failed");}
StretchRenderPolicy policy(){StretchRenderPolicy p;p.memoryBytes=64*1024*1024;p.deadlineMilliseconds=10000;return p;}
ClipStretchAnchor anchor(const Json &r,const Asset &raw){ClipStretchAnchor p;p.sourceAssetId=raw.id;p.sourceSha256=raw.sha256;p.sourceOrigin={r.at("first"),r.at("firstFraction"),r.at("firstDenominator")};p.sourceFrames=r.at("frames");p.settings={1,1,0,false};p.processor=std::string(unityStretchProcessorId);return p;}
CompHandlePlanContext context(const Session &s){return {1,s.id.str()};}
CompTransitionHandlePlan plan(const Session &s,ResourceLedger ledger){const auto &t=s.tracks[0];return prepareCompTransitionHandles(s,{t.id,t.clips[0].id,t.clips[1].id,458,510,ClipFadeCurve::EqualPower},context(s),ledger);}
std::vector<float> live(const std::filesystem::path &root,const Session &s,std::uint32_t block,ResourceLedger ledger){
    const auto channels=s.tracks[0].layout.channels;MixPlan p;p.output=s.tracks[0].layout;TrackMix track{s.tracks[0].id,{}};
    for(std::uint32_t ch=0;ch<channels;++ch)track.channels.push_back({ch,ch,1});
    p.tracks={track};
    MixPlaybackConfig config;config.graph.startFrame=100;config.endFrame=868;config.slabFrames=512;config.graph.maximumFrames=block;config.graph.resources=ledger;
    MixPlayback playback(s,p,config);ReadAheadOptions o;o.resources=ledger;o.cache.resources=ledger;MixReader reader(playback,root,s,o);
    std::vector<float> slab(std::size_t(block)*channels),pcm(std::size_t(768)*channels);std::array<float *,256> pointers{};
    for(std::uint32_t ch=0;ch<channels;++ch)pointers[ch]=slab.data()+std::size_t(ch)*block;
    for(Frame at=100;at<868;){while(reader.fillRound()){}const auto count=std::uint32_t(std::min<Frame>(block,868-at));MixPlaybackReport r;
        {rt_audit::Guard guard;r=playback.process({pointers.data(),channels},count);}
        check(!r.missingTrackFrames && r.timelineFrames==count && r.mix.status==ProcessStatus::Ok,"Generated handles lost live PCM");
        for(std::uint32_t f=0;f<count;++f)for(std::uint32_t ch=0;ch<channels;++ch)pcm[std::size_t(at-100+f)*channels+ch]=pointers[ch][f];
        at+=count;
    }
    return pcm;
}
}
int main(int argc,char **argv){try{
    check(argc==2,"Expected prepare/admit mode");Json input;std::cin>>input;const auto root=utf8Path(input.at("root").get<std::string>());ResourceLedger ledger(128*1024*1024,"Comp render fixture");
    if(std::string_view(argv[1])=="prepare"){
        const auto requests=read(root/"base-requests.json"),ready=read(root/"base-ready.json");const auto &r=requests[0];
        Asset raw;raw.id=Id(r.at("assetId"));raw.relativePath=r.at("relative");raw.sha256=r.at("sha256");raw.sampleRate=r.at("rate");raw.frames=r.at("sourceFrames");const auto channels=r.at("channels").get<std::uint32_t>();raw.layout={channels==1?LayoutKind::Mono:channels==2?LayoutKind::Stereo:LayoutKind::Discrete,channels};
        auto s=makeOneTrackSession("Actual comp handles","Audio",raw.sampleRate);auto &t=s.tracks[0];t.layout=raw.layout;t.eq={};s.assets={raw};
        TakeLane lane;lane.captureEndFrame=raw.frames;Clip take;take.assetId=raw.id;take.lengthFrames=raw.frames;lane.clips={take};t.takeLanes={lane};
        for(unsigned n=0;n<2;++n){Clip c;c.startFrame=100+Frame(n)*384;c.lengthFrames=384;c.assetId=raw.id;c.takeOrigin=ClipTakeOrigin{t.id,lane.id,take.id};
            ClipStretchPlan p{t.id,c,raw,anchor(requests[n],raw)};const auto op=Id(requests[n].at("operation"));
            verifyStretchRenderReady(ready[n].dump(),p,op,ledger);auto verified=verifyOwnedClipStretch(root,p,op,policy(),ledger);
            c.assetId=verified.edit().rendered.id;c.stretch=verified.edit().value;s.assets.push_back(verified.edit().rendered);t.clips.push_back(c);
        }
        // Keep a distinct processed take which spans both comp fragments. The
        // handle planner uses its raw map to define the allowed extension, so
        // admission must rehash this artifact as well as raw/current/nominal.
        ClipStretchPlan retainedPlan{t.id,t.takeLanes[0].clips[0],raw,anchor(requests[2],raw)};
        const auto retainedOp=Id(requests[2].at("operation"));
        verifyStretchRenderReady(ready[2].dump(),retainedPlan,retainedOp,ledger);
        auto retainedVerified=verifyOwnedClipStretch(root,retainedPlan,retainedOp,policy(),ledger);
        t.takeLanes[0].clips[0].assetId=retainedVerified.edit().rendered.id;
        t.takeLanes[0].clips[0].lengthFrames=retainedVerified.edit().rendered.frames;
        t.takeLanes[0].clips[0].stretch=retainedVerified.edit().value;
        s.assets.push_back(retainedVerified.edit().rendered);
        validate(s);ProjectStore(root).save(s);auto pair=plan(s,ledger);Json outputs=Json::array();
        for(const auto *e:{&pair.outgoing(),&pair.incoming()}){auto p=prepareUnityCompHandleRender(*e);auto bytes=encodeStretchRenderRequest(p,Id::generate(),policy(),ledger);outputs.push_back(Json::parse(bytes.bytes()));}
        write(root/"comp-requests.json",outputs);std::cout<<outputs.dump()<<'\n';return 0;
    }
    check(std::string_view(argv[1])=="admit","Unknown comp fixture mode");auto s=ProjectStore(root).load();const auto original=s;auto pair=plan(s,ledger);
    const auto requests=read(root/"comp-requests.json"),ready=read(root/"comp-ready.json");
    auto a=prepareUnityCompHandleRender(pair.outgoing()),b=prepareUnityCompHandleRender(pair.incoming());
    verifyStretchRenderReady(ready[0].dump(),a,Id(requests[0].at("operation")),ledger);verifyStretchRenderReady(ready[1].dump(),b,Id(requests[1].at("operation")),ledger);
    auto out=verifyOwnedClipStretch(root,a,Id(requests[0].at("operation")),policy(),ledger),in=verifyOwnedClipStretch(root,b,Id(requests[1].at("operation")),policy(),ledger);
    auto verified=verifyUnityCompHandlePair(root,s,context(s),pair,&out,&in,policy(),ledger);check(s==original,"Artifact review mutated project");
    const auto refusal=[&](auto action){try{action();}catch(const ProjectError &){++checks;return;}throw std::runtime_error("Invalid comp review accepted");};
    auto stale=context(s);++stale.projectEpoch;
    refusal([&]{verifyUnityCompHandlePair(root,s,stale,pair,&out,&in,policy(),ledger);});
    std::stop_source canceled;canceled.request_stop();
    refusal([&]{verifyUnityCompHandlePair(root,s,context(s),pair,&out,&in,policy(),ledger,canceled.get_token());});
    refusal([&]{verifyUnityCompHandlePair(root,s,context(s),pair,&out,nullptr,policy(),ledger);});
    const auto changedPath=root/utf8Path(in.edit().rendered.relativePath);
    std::ifstream originalFile(changedPath,std::ios::binary);std::string originalBytes((std::istreambuf_iterator<char>(originalFile)),{});originalFile.close();
    {std::ofstream changed(changedPath,std::ios::binary|std::ios::trunc);auto bytes=originalBytes;bytes.back()^=1;changed.write(bytes.data(),std::streamsize(bytes.size()));}
    refusal([&]{verifyUnityCompHandlePair(root,s,context(s),pair,&out,&in,policy(),ledger);});
    {std::ofstream restore(changedPath,std::ios::binary|std::ios::trunc);restore.write(originalBytes.data(),std::streamsize(originalBytes.size()));}
    const auto &retainedClip=s.tracks[0].takeLanes[0].clips[0];
    const auto retainedIt=std::find_if(s.assets.begin(),s.assets.end(),[&](const Asset &asset){return asset.id==retainedClip.assetId;});
    check(retainedIt!=s.assets.end(),"Processed retained asset disappeared");
    const auto &retainedAsset=*retainedIt;
    const auto retainedPath=root/utf8Path(retainedAsset.relativePath);
    std::ifstream retainedFile(retainedPath,std::ios::binary);std::string retainedBytes((std::istreambuf_iterator<char>(retainedFile)),{});retainedFile.close();
    {std::ofstream changed(retainedPath,std::ios::binary|std::ios::trunc);auto bytes=retainedBytes;bytes.back()^=1;changed.write(bytes.data(),std::streamsize(bytes.size()));}
    refusal([&]{verifyUnityCompHandlePair(root,s,context(s),pair,&out,&in,policy(),ledger);});
    {std::ofstream restore(retainedPath,std::ios::binary|std::ios::trunc);restore.write(retainedBytes.data(),std::streamsize(retainedBytes.size()));}
    check(s==original,"Refused comp review changed canonical state");
    auto bad=verified.edit();bad.incoming->window.baseSha256=std::string(64,'f');const auto beforeResources=ledger.usage();
    try{applySessionEdits(s,{bad});throw std::runtime_error("Last fragment corruption accepted");}catch(const ProjectError &){++checks;}
    check(s==original && ledger.usage()==beforeResources,"Last fragment refusal changed canonical project/credit");
    const auto channels=s.tracks[0].layout.channels;rt_audit::reset();
    for(const auto curve:{ClipFadeCurve::Linear,ClipFadeCurve::EqualPower,ClipFadeCurve::Smoothstep}){
        auto edit=verified.edit();edit.transition.curve=curve;EditHistory history(s);check(history.structural({edit}),"Actual reviewed pair not admitted");const auto applied=s;
        check(s.tracks[0].clips[0].stretch==original.tracks[0].clips[0].stretch && s.tracks[0].clips[1].stretch==original.tracks[0].clips[1].stretch,"Adoption replaced nominal raw anchor");
        const auto pcm=live(root,s,127,ledger);check(live(root,s,511,ledger)==pcm,"Generated handles differ across block partitions");
        std::filesystem::create_directories(root/"exports");ExportSpec spec(s.tracks[0].id);spec.startFrame=100;spec.endFrame=868;spec.blockFrames=37;ExportOptions options;options.resources=ledger;
        const auto name="comp-"+std::to_string(int(curve))+".wav";exportTrackWav(root,s,root/"exports"/name,spec,options);
        ApprovedMediaRoot approved(root,ledger);auto file=approved.open("exports/"+name,1024*1024);std::vector<float> exported;
        validateApprovedWave(file,{}, {},[&](std::uint64_t,std::span<const double> samples){for(auto v:samples)exported.push_back(float(v));});
        check(exported==pcm,"Generated handle live/export PCM differs");
        std::ofstream rawPcm(root/"exports"/(name+".pcm"),std::ios::binary);rawPcm.write(reinterpret_cast<const char *>(pcm.data()),std::streamsize(pcm.size()*sizeof(float)));rawPcm.close();check(bool(rawPcm),"PCM evidence write failed");
        ProjectStore(root).save(s);check(ProjectStore(root).load()==s,"Actual handle pair did not reopen");
        check(history.undo() && s==original && history.redo() && s==applied && history.undo() && s==original,"Actual pair Undo/Redo not atomic");
        ProjectStore(root).save(s);
    }
    const auto audit=rt_audit::counts;check(!audit.cppAllocate && !audit.cppFree && !audit.cAllocate && !audit.cFree && !audit.blockingLock,"Comp live callbacks allocated/freed/locked");
    std::cout<<Json{{"checks",checks},{"channels",channels},{"actual_render",true},{"atomic_pair",true},{"callback_audit",0},{"full_sonic_quality",false}}.dump()<<'\n';return 0;
}catch(const std::exception &e){std::cerr<<e.what()<<'\n';return 1;}}
