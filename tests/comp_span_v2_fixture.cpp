// SPDX-License-Identifier: GPL-3.0-only
#include <soundcurrent/comp_span_history.hpp>
#include <soundcurrent/comp_playback_state.hpp>
#include <soundcurrent/comp_pair_recovery.hpp>
#include <soundcurrent/media_cache.hpp>
#include "media_io.hpp"
#include <nlohmann/json.hpp>
#include <algorithm>
#include <bit>
#include <fstream>
#include <iostream>
using namespace soundcurrent::daw;
namespace {
using Json=nlohmann::json;unsigned checks=0;
void check(bool v,const char *m){++checks;if(!v)throw std::runtime_error(m);}
template<class F>void refuses(F f){try{f();}catch(const ProjectError &){++checks;return;}throw std::runtime_error("Invalid v2 span accepted");}
std::string read(const std::filesystem::path &p){std::ifstream f(p,std::ios::binary);check(bool(f),"Missing v2 fixture file");return {(std::istreambuf_iterator<char>(f)),{}};}
void write(const std::filesystem::path &p,std::string_view b){std::ofstream f(p,std::ios::binary|std::ios::trunc);f.write(b.data(),std::streamsize(b.size()));check(bool(f),"Cannot write v2 fixture");}
std::string hash(std::string_view b){media_io::SampleHash h;h.updateBytes({reinterpret_cast<const std::byte *>(b.data()),b.size()});return h.digest();}
CompSpanWorkerPolicy policy(){return {256*1024*1024,64*1024*1024,64*1024*1024,10000};}
const CompHandlePlanContext context{121,"v2-span-owner/é-日本"};
}
int main(int argc,char **argv){try{
    check(argc==2,"Expected v2 fixture mode");Json input;std::cin>>input;const auto root=utf8Path(input.at("root").get<std::string>());
    ResourceLedger ledger(512*1024*1024,"V2 span fixture");const auto projectBytes=read(root/"project.json");
    if(std::string_view(argv[1])=="prepare"){
        auto s=decodeProject(read(root/"adopted-project.json"));const auto original=s;const bool mixed=input.value("mixed",false);
        auto &track=s.tracks[0];auto &out=track.clips[0];auto &in=track.clips[1];
        if(mixed){out.sourceFrame+=64;out.lengthFrames-=128;const auto *h=findCompPlaybackSource(s,in.assetId);check(h,"Missing original mixed history");in=h->source.original;in.sourceFrame=0;in.sourceTiming={};in.lengthFrames=1000;}
        in.startFrame=out.startFrame+out.lengthFrames;validate(s);const auto boundary=in.startFrame;
        auto plan=prepareCompProcessing(s,{track.id,out.id,in.id,boundary-32,boundary+32,ClipFadeCurve::EqualPower},context,8,ledger);
        check(bool(plan.handles().outgoing().data().regeneration) && bool(plan.handles().incoming().data().regeneration)!=mixed,"Unexpected regenerated pair shape");
        if(mixed)check(plan.outgoing().domain==CompProcessingDomain::ExistingMedia && plan.incoming().domain!=CompProcessingDomain::ExistingMedia,"Mixed fixture did not reach unchanged assembled side");
        const CompRecipeExecutor parent{"soundcurrent.comp-join-quintic-v1",std::string(64,'e')};
        auto recipe=encodeCompProcessingRecipe(s,context,plan,Id::generate(),parent,ledger);
        const auto label=input.at("label").get<std::string>();check(!label.empty() && label.size()<64 && std::all_of(label.begin(),label.end(),[](char c){return (c>='a'&&c<='z') || c=='-';}),"Unsafe fixture label");
        Json requests=Json::array();
        for(unsigned side=0;side<2;++side){if(mixed && side==0)continue;
            auto request=encodeCompSpanRequest(recipe,parent,side,Id::generate(),input.at("workerSha256").get<std::string>(),ledger);
            check(request.requiresHistory() && request.protocol()==compRegenerationSpanProtocol,"Missing history protocol");
            const auto document=Json::parse(request.bytes());const auto intent=Json::parse(compSpanIntent(request,policy(),ledger));
            check(document.at("clock")==intent.at("clock") && intent.at("rate")==request.span().outputSampleRate(),"Clock binding missing from readiness/completion intent");
            for(unsigned mutation=0;mutation<12;++mutation){auto bad=document;
                if(mutation==0)bad["clock"]["outputRate"]=32000;
                if(mutation==1)bad["clock"]["inputRate"]=32000;
                if(mutation==2)bad["clock"]["backendClockScale"]=Json::array({9,0,1});
                if(mutation==3)bad["clock"]["bakedPitchScale"]=Json::array({9,0,1});
                if(mutation==4)bad["clock"]["processingStep"]=Json::array({9,1});
                if(mutation==5)bad["clock"]["extra"]=true;
                if(mutation==6)bad["clock"]["outputRate"]=double(request.span().outputSampleRate());
                if(mutation==7)bad["executor"]["id"]=std::string(compSpanProcessorId);
                if(mutation==8)bad.erase("clock");
                if(mutation==9){bad["protocol"]=std::string(compSpanProtocol);bad.erase("clock");bad["executor"]["id"]=std::string(compSpanProcessorId);}
                if(mutation==10)bad["protocol"]="sc-comp-span-v3";
                if(mutation==11)bad["extra"]=true;
                const auto bytes=bad.dump();refuses([&]{decodeCompSpanRequest(bytes,hash(bytes),context,request.executorSha256(),ledger);});
            }
            const auto file="v2-"+label+"-"+std::to_string(side)+".request.json";write(root/file,request.bytes());
            requests.push_back({{"file",file},{"digest",request.digest()},{"operation",request.operation().str()},{"fragment",side},{"sourceRate",request.span().source().sampleRate},{"outputRate",request.span().outputSampleRate()},{"frames",request.span().outputFrames()},{"mixed",mixed}});
        }
        // Request production is available; canonical paired Apply remains gated.
        refuses([&]{prepareCompPairSelection(std::move(recipe),parent,input.at("workerSha256").get<std::string>(),{Id::generate(),Id::generate()},policy(),ledger);});
        check(read(root/"project.json")==projectBytes && decodeProject(read(root/"adopted-project.json"))==original,"V2 preparation changed project state");
        std::cout<<Json{{"checks",checks},{"requests",requests},{"historicalCompletion","media/comp-assemblies/"+original.compPlaybackSources[0].assembly.operation.str()+"/complete.json"},{"projectUnchanged",true}}.dump()<<'\n';return 0;
    }
    const auto bytes=read(root/utf8Path(input.at("file").get<std::string>()));
    auto request=decodeCompSpanRequest(bytes,input.at("digest").get<std::string>(),context,input.at("workerSha256").get<std::string>(),ledger);
    if(std::string_view(argv[1])=="ready"){
        auto ready=input.at("ready");auto bad=ready;bad["clock"]["outputRate"]=32000;refuses([&]{verifyCompSpanReady(bad.dump(),request,policy(),ledger);});
        verifyCompSpanReady(ready.dump(),request,policy(),ledger);
        media_io::File start(root/"media/comp-spans"/request.operation().str()/"start.request",true);start.write(request.digest()+"\n");start.flush();start.close();
        std::cout<<Json{{"checks",checks},{"started",true}}.dump()<<'\n';return 0;
    }
    check(std::string_view(argv[1])=="verify","Unknown v2 fixture mode");
    const auto reserved=ledger.usage().reservedBytes;Json result;
    {
        verifyCompSpanHistory(root,request,policy(),ledger,[]{});
        const auto verified=verifyOwnedCompSpan(root,request,policy(),ledger);
        check(verified.asset().sampleRate==request.span().outputSampleRate() && verified.asset().frames==request.span().outputFrames(),"V2 output clock or duration changed");
        std::stop_source canceled;canceled.request_stop();refuses([&]{verifyCompSpanHistory(root,request,policy(),ledger,[]{},canceled.get_token());});
        auto small=policy();small.maximumSourceBytes=1;refuses([&]{verifyCompSpanHistory(root,request,small,ledger,[]{});});
        refuses([&]{verifyCompSpanHistory(root,request,policy(),ResourceLedger(1,"Tiny history"),[]{});});
        MediaCacheConfig config;config.resources=ledger;config.maximumOpenFiles=2;config.pageFrames=256;config.cacheBudgetBytes=256*1024;config.registryBudgetBytes=256*1024;
        const std::array<Asset,2> assets{request.span().source(),verified.asset()};MediaReadCache cache(root,assets,48000,config);
        Frame at=0;std::array<float,1024> actual{};std::uint64_t compared=0;
        const auto report=renderCompSpan(request.span(),[&](Frame first,std::span<float> v){cache.read(0,first,v);},[&](std::span<const float> expected){check(expected.size()<=actual.size(),"Oversize v2 fixture slab");auto view=std::span(actual).first(expected.size());cache.read(1,at,view);
            for(std::size_t n=0;n<expected.size();++n)check(std::bit_cast<std::uint32_t>(expected[n])==std::bit_cast<std::uint32_t>(view[n]),"Worker PCM differs from complete in-process recomputation");
            at+=Frame(expected.size()/2);compared+=expected.size();},ledger,[]{});
        check(at==verified.asset().frames && report.floatPeak>1,"V2 recomputation lost extent or headroom");
        const auto marker=root/"media/comp-spans"/request.operation().str()/"complete.json";const auto original=read(marker);auto changed=Json::parse(original);changed["clock"]["outputRate"]=32000;write(marker,changed.dump());
        try{refuses([&]{verifyOwnedCompSpan(root,request,policy(),ledger);});}catch(...){write(marker,original);throw;}write(marker,original);
        verifyCompSpanHistory(root,request,policy(),ledger,[]{});
        check(read(root/"project.json")==projectBytes,"V2 verification changed project state");
        result=Json{{"checks",checks},{"comparedSamples",compared},{"outputRate",verified.asset().sampleRate},{"sourceRate",request.span().source().sampleRate},{"fileSha256",verified.asset().sha256},{"historyReviewed",true},{"projectUnchanged",true},{"assemblyAdoption",false},{"qualityAdmission",false}};
    }
    check(ledger.usage().reservedBytes==reserved,"V2 review leaked joined payload credits");
    result["joinedMemoryReleased"]=true;result["checks"]=checks;std::cout<<result.dump()<<'\n';
    return 0;
}catch(const std::exception &e){std::cerr<<e.what()<<'\n';return 1;}}
