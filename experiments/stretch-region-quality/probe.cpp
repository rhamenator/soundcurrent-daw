// SPDX-License-Identifier: GPL-3.0-only
// Original bounded diagnostic; outside the application build and audio devices.
#include <soundcurrent/stretch_render_protocol.hpp>
#include <soundcurrent/mix_reader.hpp>
#include <soundcurrent/export.hpp>
#include <soundcurrent/wave_validation.hpp>
#include <soundcurrent/project_store.hpp>
#include <nlohmann/json.hpp>
#include "rt_audit.hpp"
#include <algorithm>
#include <array>
#include <fstream>
#include <iostream>
#include <cmath>
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif
using namespace soundcurrent::daw;
using Json=nlohmann::json;
namespace {
unsigned checks=0;
void check(bool condition,const char *message){++checks;if(!condition)throw std::runtime_error(message);}
Json bounded(std::string_view text){check(!text.empty() && text.size()<=16384,"Diagnostic JSON exceeds bank");return Json::parse(text);}
StretchRenderPolicy policy(){StretchRenderPolicy p;p.maximumInputFrames=131072;p.maximumOutputBytes=16*1024*1024;p.maximumSourceBytes=16*1024*1024;p.deadlineMilliseconds=10000;return p;}
StretchSettings settings(const Json &j){return {j.at("timeNumerator"),j.at("timeDenominator"),j.at("pitchMilliCents"),j.at("formantPreserved")};}
std::optional<StretchContext> context(const Json &j){if(!j.at("contextEnabled").get<bool>())return {};return StretchContext{j.at("contextBefore"),j.at("contextAfter")};}
ClipStretchPlan plan(const Session &s,const Json &request){auto p=prepareClipStretch(s,s.tracks.at(0).id,s.tracks.at(0).clips.at(0).id,settings(request),context(request));return p;}
std::vector<float> live(const std::filesystem::path &root,const Session &s,Frame end,std::uint32_t block,ResourceLedger ledger){
    const auto channels=s.tracks.at(0).layout.channels;
    check(channels<=8 && end>0 && end<=131072,"Diagnostic output exceeds admission");
    MixPlan routes;routes.output=s.tracks[0].layout;TrackMix track{s.tracks[0].id,{}};
    for(std::uint32_t ch=0;ch<channels;++ch)track.channels.push_back({ch,ch,1});
    routes.tracks.push_back(track);
    MixPlaybackConfig config;config.endFrame=end;config.slabFrames=512;config.graph.maximumFrames=block;config.graph.resources=ledger;
    MixPlayback mix(s,routes,config);ReadAheadOptions options;options.resources=ledger;options.cache.resources=ledger;MixReader reader(mix,root,s,options);
    std::vector<float> slab(std::size_t(block)*channels),result(std::size_t(end)*channels);std::array<float *,8> output{};
    for(std::uint32_t ch=0;ch<channels;++ch)output[ch]=slab.data()+std::size_t(ch)*block;
    for(Frame at=0;at<end;){while(reader.fillRound()){}const auto n=std::uint32_t(std::min<Frame>(block,end-at));MixPlaybackReport report;
        {rt_audit::Guard guard;report=mix.process({output.data(),channels},n);}
        check(!report.missingTrackFrames && report.mix.status==ProcessStatus::Ok && report.timelineFrames==n,"Diagnostic live graph lost samples");
        for(std::uint32_t f=0;f<n;++f)for(std::uint32_t ch=0;ch<channels;++ch)result[std::size_t(at+f)*channels+ch]=output[ch][f];
        at+=n;
    }
    return result;
}
Json exportAndCompare(const std::filesystem::path &root,const Session &s,const std::string &relative,ResourceLedger ledger){
    const auto end=s.tracks.at(0).clips.at(0).lengthFrames;rt_audit::reset();
    auto expected=live(root,s,end,37,ledger);check(live(root,s,end,127,ledger)==expected,"Callback partitions changed diagnostic samples");
    if(end>2){auto split=s;const auto &c=s.tracks[0].clips[0];applySessionEdits(split,{SplitClip{s.tracks[0].id,c.id,Id::generate(),end/2}});check(live(root,split,end,53,ledger)==expected,"Diagnostic split changed phase");}
    check(!rt_audit::counts.cppAllocate && !rt_audit::counts.cppFree && !rt_audit::counts.cAllocate && !rt_audit::counts.cFree && !rt_audit::counts.blockingLock,"Diagnostic callback allocation/free/lock");
    std::filesystem::create_directories(root/"exports");check(relative=="exports/visible.wav" || (relative.size()==24 && relative.starts_with("exports/reference-") && relative.ends_with(".wav") && relative[18]>='0' && relative[18]<='9' && relative[19]>='0' && relative[19]<='9'),"Unexpected owned diagnostic export name");
    ExportSpec spec(s.tracks[0].id);spec.endFrame=end;spec.blockFrames=41;ExportOptions options;options.resources=ledger;
    const auto out=exportTrackWav(root,s,root/utf8Path(relative),spec,options);ApprovedMediaRoot approved(root,ledger);auto file=approved.open(relative,policy().maximumOutputBytes);
    std::vector<float> actual;WaveValidationLimits limits;limits.maximumChannels=8;limits.maximumFrames=131072;
    validateApprovedWave(file,limits,{},[&](std::uint64_t,std::span<const double> samples){for(auto x:samples)actual.push_back(float(x));});
    check(actual==expected && out.frames==end,"Diagnostic WAV differs from shared live graph");
    return {{"relative",relative},{"frames",out.frames},{"peak",out.peak},{"sha256",out.fileSha256},{"sharedLiveExportExact",true},{"splitExact",true},{"callbackAudit",0}};
}
int execute(const std::vector<std::string> &args){try {
    check(args.size()>=4 && args.size()<=5,"Expected mode/root/bounded JSON/[ready]");auto root=utf8Path(args[2]);ResourceLedger ledger(128*1024*1024,"Owned region diagnostic");const auto j=bounded(args[3]);
    if(args[1]=="prepare"){
        ApprovedMediaRoot approved(root,ledger);auto rawFile=approved.open("media/signal-été.wav",policy().maximumSourceBytes);WaveValidationLimits limits;limits.maximumChannels=8;limits.maximumFrames=32768;
        const auto wave=validateApprovedWave(rawFile,limits);check(wave.encoding==WaveEncoding::Float32 && wave.rate==48000 && wave.frames<=32768,"Unexpected original fixture format");
        auto s=makeOneTrackSession("Owned region — Київ","Audio",wave.rate);Asset a;a.relativePath="media/signal-été.wav";a.sampleRate=wave.rate;a.frames=Frame(wave.frames);a.layout={wave.channels==1?LayoutKind::Mono:wave.channels==2?LayoutKind::Stereo:LayoutKind::Discrete,wave.channels};a.sha256=std::string(wave.sourceSha256.begin(),wave.sourceSha256.end());
        s.assets={a};s.tracks[0]=makeAudioTrack("Owned channels",a.layout,wave.rate);s.tracks[0].eq={};
        Clip c;c.assetId=a.id;c.sourceFrame=j.at("first");c.sourceTiming={j.at("firstFraction"),j.at("firstDenominator")};c.lengthFrames=j.at("frames");s.tracks[0].clips={c};validate(s);
        auto prepared=plan(s,j);const Id operation=Id::generate();auto encoded=encodeStretchRenderRequest(prepared,operation,policy(),ledger);ProjectStore(root).save(s);std::cout<<encoded.bytes()<<'\n';return 0;
    }
    auto s=ProjectStore(root).load();check(s.tracks.size()==1 && s.tracks[0].clips.size()==1 && s.assets.size()<=2,"Diagnostic project shape changed");
    if(args[1]=="ready" || args[1]=="adopt"){
        auto prepared=plan(s,j);const Id operation(j.at("operation").get<std::string>());auto encoded=encodeStretchRenderRequest(prepared,operation,policy(),ledger);check(bounded(encoded.bytes())==j,"Diagnostic parent/request differ");check(args.size()==5,"Expected ready identity");verifyStretchRenderReady(args[4],prepared,operation,ledger);
        if(args[1]=="ready"){std::cout<<Json({{"readyVerified",true}}).dump()<<'\n';return 0;}
        auto verified=verifyOwnedClipStretch(root,prepared,operation,policy(),ledger);const auto original=s;EditHistory history(s);history.structural({verified.edit()});const auto applied=s;
        check(history.undo() && s==original && history.redo() && s==applied,"Diagnostic adoption lost exact Undo/Redo");ProjectStore(root).save(s);check(ProjectStore(root).load()==s,"Diagnostic save/reopen differs");
        auto result=exportAndCompare(root,s,"exports/visible.wav",ledger);const auto &c=s.tracks[0].clips[0];const auto g=stretchGeometry(*c.stretch,s.assets[0].frames);
        auto position=[](SourcePosition p){return Json::array({p.frame,p.fraction,p.denominator});};
        result["clipSource"]=position({c.sourceFrame,c.sourceTiming.fraction,c.sourceTiming.denominator});result["visibleBegin"]=position(g.visibleBegin);result["visibleEnd"]=position(g.visibleEnd);result["inputOrigin"]=position(g.inputOrigin);result["inputFrames"]=g.inputFrames;result["fullOutputFrames"]=g.outputFrames;result["processor"]=c.stretch->processor;result["rawSha256"]=s.assets[0].sha256;result["checks"]=checks;result["saveReopenUndoRedo"]=true;result["nativeAudio"]=false;std::cout<<result.dump()<<'\n';return 0;
    }
    check(args[1]=="crop" && s.tracks[0].clips[0].stretch.has_value(),"Expected an adopted whole-source diagnostic");
    auto &c=s.tracks[0].clips[0];const auto p=scaleSourcePosition({j.at("rawOffset").get<Frame>(),0,1},c.stretch->settings.timeNumerator,c.stretch->settings.timeDenominator);
    c.sourceFrame=p.frame;c.sourceTiming={p.fraction,p.denominator};c.lengthFrames=j.at("visibleFrames");validate(s);
    const auto result=exportAndCompare(root,s,j.at("relative").get<std::string>(),ledger);std::cout<<result.dump()<<'\n';return 0;
}catch(const std::exception &error){std::cerr<<error.what()<<'\n';return 1;}}
}
#ifdef _WIN32
int wmain(int argc,wchar_t **argv){std::vector<std::string> args;for(int i=0;i<argc;++i){const auto n=WideCharToMultiByte(CP_UTF8,WC_ERR_INVALID_CHARS,argv[i],-1,nullptr,0,nullptr,nullptr);if(n<=0)return 1;std::string s(std::size_t(n),'\0');if(!WideCharToMultiByte(CP_UTF8,WC_ERR_INVALID_CHARS,argv[i],-1,s.data(),n,nullptr,nullptr))return 1;s.pop_back();args.push_back(std::move(s));}return execute(args);}
#else
int main(int argc,char **argv){return execute({argv,argv+argc});}
#endif
