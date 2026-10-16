#include <soundcurrent/stretch_render_protocol.hpp>
// SPDX-License-Identifier: GPL-3.0-only
#include <soundcurrent/mix_reader.hpp>
#include <soundcurrent/export.hpp>
#include <soundcurrent/wave_validation.hpp>
#include <soundcurrent/project_store.hpp>
#include <nlohmann/json.hpp>
#include "warp_codec.hpp"
#include "rt_audit.hpp"
#include <array>
#include <fstream>
#include <iostream>
#include <algorithm>
#include <cmath>
#include <numbers>
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif
using namespace soundcurrent::daw;
namespace {
unsigned checks=0;
void check(bool v,const char *message){++checks;if(!v)throw std::runtime_error(message);}
std::vector<float> live(const std::filesystem::path &root,const Session &s,Frame first,Frame end,std::uint32_t block,ResourceLedger ledger){
    auto channels=s.tracks[0].layout.channels;MixPlan plan;plan.output=s.tracks[0].layout;TrackMix route{s.tracks[0].id,{}};
    for(std::uint32_t ch=0;ch<channels;++ch)route.channels.push_back({ch,ch,1});
    plan.tracks.push_back(route);
    MixPlaybackConfig config;config.graph.startFrame=first;config.endFrame=end;config.slabFrames=512;config.graph.maximumFrames=block;config.graph.resources=ledger;
    MixPlayback mix(s,plan,config);ReadAheadOptions options;options.resources=ledger;options.cache.resources=ledger;MixReader reader(mix,root,s,options);
    std::vector<float> slab(std::size_t(block)*channels),result(std::size_t(end-first)*channels);std::array<float *,256> output{};
    for(std::uint32_t ch=0;ch<channels;++ch)output[ch]=slab.data()+std::size_t(ch)*block;
    for(Frame at=first;at<end;){while(reader.fillRound()){}auto n=std::uint32_t(std::min<Frame>(block,end-at));MixPlaybackReport report;
        {rt_audit::Guard guard;report=mix.process({output.data(),channels},n);}
        check(!report.missingTrackFrames && report.mix.status==ProcessStatus::Ok && report.timelineFrames==n,"Derived live playback lost samples");
        for(std::uint32_t f=0;f<n;++f)for(std::uint32_t ch=0;ch<channels;++ch)result[std::size_t(at-first+f)*channels+ch]=output[ch][f];
        at+=n;
    }
    return result;
}
}
int run(const std::vector<std::string> &args){try{
    check(args.size()==5,"Expected owned project root and relative derived artifact");auto root=utf8Path(args[1]);std::string relative=args[2];validateRelativeMediaPath(relative);
    auto path=root/utf8Path(relative);std::ifstream receiptFile(path.parent_path()/"complete.json");nlohmann::json receipt;receiptFile>>receipt;receiptFile.close();
    const auto processor=receipt.at("processor").get<std::string>();
    check(receipt["complete"]==true && (processor==stretchProcessorId || processor==protectedWarpProcessorId),"Expected completed stretch artifact");
    const auto request=nlohmann::json::parse(args[3]);const auto operation=Id(request.at("operation").get<std::string>());
    Asset raw;raw.id=Id(request.at("assetId").get<std::string>());raw.relativePath=request.at("relative");raw.sha256=request.at("sha256");raw.sampleRate=request.at("rate");raw.frames=request.at("sourceFrames");const auto channels=request.at("channels").get<std::uint32_t>();raw.layout={channels==1?LayoutKind::Mono:channels==2?LayoutKind::Stereo:LayoutKind::Discrete,channels};
    auto s=makeOneTrackSession("Editable derived stretch","Audio",raw.sampleRate);s.assets={raw};s.tracks[0].layout=raw.layout;s.tracks[0].eq={};
    Clip c;c.assetId=raw.id;c.sourceFrame=request.at("first");c.sourceTiming={request.at("firstFraction"),request.at("firstDenominator")};c.lengthFrames=request.at("frames");s.tracks[0].clips={c};
    TakeLane retained;retained.name="Raw take — Ελληνικά";retained.captureEndFrame=c.lengthFrames;
    auto originalTake=c;originalTake.id=Id::generate();retained.clips={originalTake};
    c.takeOrigin=ClipTakeOrigin{s.tracks[0].id,retained.id,originalTake.id};
    s.tracks[0].clips={c};s.tracks[0].takeLanes={retained};
    const auto original=s;StretchSettings settings{request.at("timeNumerator"),request.at("timeDenominator"),request.at("pitchMilliCents"),request.at("formantPreserved")};
    std::optional<WarpSettings> warp;if(request.contains("warp"))warp=warp_codec::decode(request.at("warp"),c.lengthFrames,stretchOutputFrames(c.lengthFrames,settings),8*1024*1024);
    auto plan=prepareClipStretch(s,s.tracks[0].id,c.id,settings,{},warp);StretchRenderPolicy policy;policy.deadlineMilliseconds=10000;
    {
        ResourceLedger protocolLedger(32*1024*1024,"Parent stretch verification");
        auto encoded=encodeStretchRenderRequest(plan,operation,policy,protocolLedger);check(nlohmann::json::parse(encoded.bytes())==request,"Parent request differs from actual worker request");
        verifyStretchRenderReady(args[4],plan,operation,protocolLedger);
        auto verified=verifyOwnedClipStretch(root,plan,operation,policy,protocolLedger);
        check(verified.edit().rendered.relativePath==relative && (warp?verified.peak()>0:verified.peak()>1),"Parent returned wrong owned artifact/headroom");
        auto refusal=[&](auto action){try {action();}catch(const ProjectError &){++checks;return;}throw std::runtime_error("Unverified stretch completion accepted");};
        // Portability: verify either producer's metric without rewriting PCM or
        // weakening the existing grant, digest, geometry and source checks.
        for(const char *metric:{"address-space","process-commit"}) {
            auto portable=receipt;portable["memoryMetric"]=metric;
            {std::ofstream marker(path.parent_path()/"complete.json",std::ios::binary|std::ios::trunc);marker<<portable.dump();}
            auto reviewed=verifyOwnedClipStretch(root,plan,operation,policy,protocolLedger);
            check(reviewed.edit().value==verified.edit().value && reviewed.edit().rendered==verified.edit().rendered,
                  "Producer memory metric changed the portable stretch edit");
            portable["sampleSha256"]=std::string(64,'f');
            {std::ofstream marker(path.parent_path()/"complete.json",std::ios::binary|std::ios::trunc);marker<<portable.dump();}
            refusal([&]{verifyOwnedClipStretch(root,plan,operation,policy,protocolLedger);});
        }
        for(const nlohmann::json &metric:{nlohmann::json("resident-set"),nlohmann::json(""),nlohmann::json(nullptr),nlohmann::json(1),nlohmann::json(true),nlohmann::json::array(),nlohmann::json::object()}) {
            auto bad=receipt;bad["memoryMetric"]=metric;
            {std::ofstream marker(path.parent_path()/"complete.json",std::ios::binary|std::ios::trunc);marker<<bad.dump();}
            refusal([&]{verifyOwnedClipStretch(root,plan,operation,policy,protocolLedger);});
        }
        for(unsigned mode=0;mode<(warp?24u:20u);++mode) {
            auto bad=receipt;
            if(mode==0) bad["renderKey"]=std::string(64,'f');
            if(mode==1) bad["processor"]="unknown";
            if(mode==2) bad["protocol"]="sc-stretch-render-v1";
            if(mode==3) bad["operation"]=Id::generate().str();
            if(mode==4) bad["assetId"]=Id::generate().str();
            if(mode==5) bad["relative"]="media/other.wav";
            if(mode==6) bad["firstFraction"]=0.0;
            if(mode==7) bad["formantPreserved"]=0;
            if(mode==8) bad["sourceAlgorithm"]="unknown";
            if(mode==9) bad["target"]=18001;
            if(mode==10) bad["memoryCeilingBytes"]=128*1024*1024;
            if(mode==11) bad["payloadPeakBytes"]=0;
            if(mode==12) bad["writtenFrames"]=true;
            if(mode==13) bad["peakLinear"]=nullptr;
            if(mode==14) bad["sampleSha256"]=std::string(64,'f');
            if(mode==15) bad["audioSha256"]=std::string(64,'f');
            if(mode==16) bad["unexpected"]=0;
            if(mode==17) bad["peakLinear"]=nlohmann::json::object();
            if(mode==18) bad.erase("memoryMetric");
            if(mode==20)bad["warp"]["points"][0]["source"][0]=0.0;
            if(mode==21)bad["warp"]["spans"][0]["owner"]=Id::generate().str();
            if(mode==22)bad["warp"]["markers"][0]["output"][0]=true;
            if(mode==23)bad["warp"]["points"][0]["extra"]=0;
            const auto bytes=mode==19 ? "{\"complete\":true,"+bad.dump().substr(1) : bad.dump();
            {std::ofstream marker(path.parent_path()/"complete.json",std::ios::binary|std::ios::trunc);marker<<bytes;}
            refusal([&]{verifyOwnedClipStretch(root,plan,operation,policy,protocolLedger);});
            check(s==original,"Refused completion changed Session");
        }
        {std::ofstream marker(path.parent_path()/"complete.json",std::ios::binary|std::ios::trunc);marker<<receipt.dump();}
        auto wrongPolicy=policy;wrongPolicy.maximumInputFrames=1;
        refusal([&]{verifyOwnedClipStretch(root,plan,operation,wrongPolicy,protocolLedger);});
        std::stop_source canceled;canceled.request_stop();refusal([&]{verifyOwnedClipStretch(root,plan,operation,policy,protocolLedger,canceled.get_token());});
        for(const auto &bad:{std::string(stretchProtocolLimit(plan.anchor)+1,'x'),std::string("[]"),std::string("{\"event\":\"ready\",\"event\":\"ready\"}")})
            refusal([&]{verifyStretchRenderReady(bad,plan,operation,protocolLedger);});
        auto rawPath=root/utf8Path(raw.relativePath);std::ifstream input(rawPath,std::ios::binary);std::string rawBytes((std::istreambuf_iterator<char>(input)),{});input.close();
        {std::ofstream changed(rawPath,std::ios::binary|std::ios::trunc);auto altered=rawBytes;altered.back()^=1;changed.write(altered.data(),std::streamsize(altered.size()));}
        refusal([&]{verifyOwnedClipStretch(root,plan,operation,policy,protocolLedger);});
        {std::ofstream restore(rawPath,std::ios::binary|std::ios::trunc);restore.write(rawBytes.data(),std::streamsize(rawBytes.size()));}
        std::ifstream renderedInput(path,std::ios::binary);
        std::string renderedBytes((std::istreambuf_iterator<char>(renderedInput)),{});renderedInput.close();
        {std::ofstream changed(path,std::ios::binary|std::ios::trunc);auto altered=renderedBytes;altered.back()^=1;changed.write(altered.data(),std::streamsize(altered.size()));}
        refusal([&]{verifyOwnedClipStretch(root,plan,operation,policy,protocolLedger);});
        check(s==original,"Corrupted derived bytes changed Session");
        {std::ofstream restore(path,std::ios::binary|std::ios::trunc);restore.write(renderedBytes.data(),std::streamsize(renderedBytes.size()));}
        auto reverified=verifyOwnedClipStretch(root,plan,operation,policy,protocolLedger);
        check(reverified.edit().value==verified.edit().value && reverified.edit().rendered==verified.edit().rendered,"Restored source/marker changed verified adoption");
        EditHistory history(s);history.structural({verified.edit()});const auto adopted=s;
        check(history.undo() && s==original && history.redo() && s==adopted,"Actual render adoption was not one exact Undo/Redo operation");
        check(s.assets[0]==raw && s.tracks[0].clips[0].stretch==verified.edit().value,"Actual render lost raw anchor or immutable source");
    }
    const auto asset=s.assets.back();c=s.tracks[0].clips[0];s.exportEndFrame=asset.frames;
    ResourceLedger ledger(128*1024*1024,"Derived workflow");rt_audit::reset();auto expected=live(root,s,0,asset.frames,37,ledger);check(!ledger.usage().reservedBytes,"Derived reader grant retained");
    check(live(root,s,0,asset.frames,511,ledger)==expected,"Callback partitions changed derived audio");
    auto split=s;applySessionEdits(split,{SplitClip{s.tracks[0].id,c.id,Id::generate(),4097}});check(live(root,split,0,asset.frames,127,ledger)==expected,"Derived split restarted stretch phase");
    auto comp=split;const auto beforeHandles=comp;EditHistory compHistory(comp);
    const auto outgoing=comp.tracks[0].clips[0].id,incoming=comp.tracks[0].clips[1].id;
    for(const auto curve:{ClipFadeCurve::Linear,ClipFadeCurve::Smoothstep,ClipFadeCurve::EqualPower}) {
        check(compHistory.structural({PrepareCompTransition{s.tracks[0].id,outgoing,incoming,4071,4123,curve}}),
              "Verified native render handles were refused");
        const auto applied=comp;
        check(comp.assets==s.assets && comp.tracks[0].takeLanes==s.tracks[0].takeLanes &&
              hashMediaFile(path)==asset.sha256 && hashMediaFile(root/utf8Path(raw.relativePath))==raw.sha256,
              "Native handle preparation changed original/derived bytes or retained lanes");
        const auto actual=live(root,comp,0,asset.frames,127,ledger);
        check(actual==live(root,comp,0,asset.frames,511,ledger),"Native comp partitions differ");
        for(Frame f=0;f<asset.frames;++f)for(std::uint32_t ch=0;ch<channels;++ch) {
            double gain=1;
            if(f>=4071 && f<4123 && curve==ClipFadeCurve::EqualPower) {
                const auto phase=double(f-4071)/51;
                gain=std::sin(phase*std::numbers::pi/2)+std::cos(phase*std::numbers::pi/2);
            }
            const auto index=std::size_t(f)*channels+ch;
            check(std::abs(double(actual[index])-double(expected[index])*gain)<1e-5,
                  "Independent whole-PCM native comp law differs");
        }
        std::filesystem::create_directory(root/"exports");ExportSpec compSpec(s.tracks[0].id);
        compSpec.endFrame=asset.frames;compSpec.blockFrames=37;ExportOptions compOptions;compOptions.resources=ledger;
        const auto filename="comp-"+operation.str()+"-"+std::to_string(int(curve))+".wav";
        exportTrackWav(root,comp,root/"exports"/filename,compSpec,compOptions);
        ApprovedMediaRoot approvedComp(root,ledger);auto compFile=approvedComp.open("exports/"+filename,16*1024*1024);
        std::vector<float> exportedComp;
        validateApprovedWave(compFile,{}, {},[&](std::uint64_t,std::span<const double> samples){for(auto v:samples)exportedComp.push_back(float(v));});
        check(exportedComp==actual,"Verified rendered comp live/export PCM differ");
        ProjectStore(root).save(comp);check(ProjectStore(root).load()==comp,"Verified comp save/reopen changed state");
        check(compHistory.undo() && comp==beforeHandles && compHistory.redo() && comp==applied &&
              compHistory.undo() && comp==beforeHandles,"Native rendered comp Undo/Redo not atomic");
    }
    auto suffix=live(root,s,257,asset.frames,113,ledger);check(std::equal(suffix.begin(),suffix.end(),expected.begin()+257*channels),"Derived seek changed waveform");
    auto crop=s;applySessionEdits(crop,{CropClip{s.tracks[0].id,c.id,37,37,asset.frames-37}});check(live(root,crop,37,asset.frames,113,ledger)==std::vector<float>(expected.begin()+37*channels,expected.end()),"Derived crop restarted processor");
    std::filesystem::create_directory(root/"exports");ExportSpec spec(s.tracks[0].id);spec.endFrame=asset.frames;spec.blockFrames=127;ExportOptions options;options.resources=ledger;
    auto exported=exportTrackWav(root,s,root/"exports"/("stretch-"+operation.str()+".wav"),spec,options);ApprovedMediaRoot approved(root,ledger);auto file=approved.open("exports/stretch-"+operation.str()+".wav",16*1024*1024);std::vector<float> actual;
    validateApprovedWave(file,{}, {},[&](std::uint64_t,std::span<const double> samples){for(auto sample:samples)actual.push_back(float(sample));});
    check(actual==expected && (warp?exported.peak>0:exported.peak>1) && exported.frames==asset.frames,"Derived offline export differs from live/headroom");
    ProjectStore(root).save(s);check(ProjectStore(root).load()==s && hashMediaFile(path)==asset.sha256,"Derived Save/reopen or immutable bytes differ");
    check(!rt_audit::counts.cppAllocate && !rt_audit::counts.cppFree && !rt_audit::counts.cAllocate && !rt_audit::counts.cFree && !rt_audit::counts.blockingLock,"Derived callback performed audited allocation/free/lock");
    std::cout<<"derived_shared_live_export_checks="<<checks<<" callback_audit=0 seek_split_crop_equal=true save_reopen=true\n";return 0;
}catch(const std::exception &e){std::cerr<<e.what()<<'\n';return 1;}}

#ifdef _WIN32
int wmain(int argc,wchar_t **argv){std::vector<std::string> args;for(int i=0;i<argc;++i){auto n=WideCharToMultiByte(CP_UTF8,WC_ERR_INVALID_CHARS,argv[i],-1,nullptr,0,nullptr,nullptr);if(n<=0)return 1;std::string s(std::size_t(n),'\0');if(!WideCharToMultiByte(CP_UTF8,WC_ERR_INVALID_CHARS,argv[i],-1,s.data(),n,nullptr,nullptr))return 1;s.pop_back();args.push_back(std::move(s));}return run(args);}
#else
int main(int argc,char **argv){return run({argv,argv+argc});}
#endif
