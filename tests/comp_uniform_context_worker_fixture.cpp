// SPDX-License-Identifier: GPL-3.0-only
#include <soundcurrent/comp_span_protocol.hpp>
#include <soundcurrent/comp_span_history.hpp>
#include <soundcurrent/media_cache.hpp>
#include "windows_file_path.hpp"
#include <bit>
#include <algorithm>
#include <sndfile.h>
#include <nlohmann/json.hpp>
#include <fstream>
#include <iostream>
#include <cmath>
#include <numbers>
using namespace soundcurrent::daw;
namespace {
using Json=nlohmann::json;unsigned checks=0;
void check(bool v,const char *m){++checks;if(!v)throw std::runtime_error(m);}
Json read(const std::filesystem::path &p){std::ifstream f(fileSystemApiPath(p),std::ios::binary);Json j;f>>j;check(bool(f),"Fixture JSON missing");return j;}
std::string bytes(const std::filesystem::path &p){std::ifstream f(fileSystemApiPath(p),std::ios::binary);return {(std::istreambuf_iterator<char>(f)),{}};}
void write(const std::filesystem::path &p,std::string_view text){std::ofstream f(fileSystemApiPath(p),std::ios::binary|std::ios::trunc);f.write(text.data(),std::streamsize(text.size()));check(bool(f),"Fixture write failed");}
CompSpanWorkerPolicy policy(){CompSpanWorkerPolicy p;p.maximumSourceBytes=64*1024*1024;p.maximumOutputBytes=64*1024*1024;p.deadlineMilliseconds=10000;return p;}
}
int main(int argc,char **argv){try{check(argc==2,"Expected fixture mode");Json input;std::cin>>input;const auto root=utf8Path(input.at("root").get<std::string>());ResourceLedger ledger(384*1024*1024,"Uniform context worker fixture");
    if(std::string_view(argv[1])=="prepare" || std::string_view(argv[1])=="prepare-history" || std::string_view(argv[1])=="prepare-short-history"){
        const bool shortHistory=std::string_view(argv[1])=="prepare-short-history";
        const bool history=std::string_view(argv[1])!="prepare";
        auto s=history && !shortHistory?decodeProject(bytes(root/"adopted-project.json")):ProjectStore(root).load();
        auto &t=s.tracks[0];check(t.clips.size()==2,"Expected protected/uniform pair");
        if(history){check(!s.compPlaybackSources.empty(),"Missing actual adopted history");for(auto &c:t.clips){if(shortHistory){c.sourceFrame=13;c.sourceTiming={2,11};c.lengthFrames=3000;c.playbackRate={2,3};}else{c.sourceFrame+=1000;c.sourceTiming={1,7};c.lengthFrames=3990;c.playbackRate={3,2};}}t.clips[1].startFrame=t.clips[0].startFrame+t.clips[0].lengthFrames;validate(s);}
        write(root/"context-project.json",encodeProject(s));
        const auto &c=t.clips[0],&second=t.clips[1];
        const PrepareCompTransition transition{t.id,c.id,second.id,second.startFrame-32,second.startFrame+32,ClipFadeCurve::EqualPower};
        auto plan=prepareCompProcessing(s,transition,{7,"parent-fixture"},8,ledger);
        const CompRecipeExecutor parent{"soundcurrent.comp-join-quintic-v1",std::string(64,'e')};auto recipe=encodeCompProcessingRecipe(s,{7,"parent-fixture"},plan,Id::generate(),parent,ledger);
        bool protectedRefused=false;try{(void)encodeUniformContextCompSpanRequest(recipe,parent,0,Id::generate(),input.at("workerSha256").get<std::string>(),ledger);}catch(const ProjectError &){protectedRefused=true;}check(protectedRefused,"Uniform encoder flattened protected geometry");
        auto r=encodeUniformContextCompSpanRequest(recipe,parent,1,Id::generate(),input.at("workerSha256").get<std::string>(),ledger);
        check(r.isUniformContext() && !r.isProtected() && r.requiresHistory()==history && r.protocol()==compUniformContextSpanProtocol && r.processor()==compUniformContextProcessorId,"Uniform context protocol identity lost");
        const auto document=Json::parse(r.bytes()),intent=Json::parse(compSpanIntent(r,policy(),ledger));check(document.at("uniformContext")==intent.at("uniformContext"),"Context binding differs between request/readiness");
        auto legacy=encodeCompSpanRequest(recipe,parent,1,Id::generate(),input.at("workerSha256").get<std::string>(),ledger);check(!legacy.isUniformContext() && legacy.protocol()!=r.protocol(),"Legacy encoder silently changed producer");
        bool legacyWindowRefused=false;
        if(shortHistory){
            check(legacy.span().processingFrames()<4096 && r.uniformContextSpan().inputFrames()>=4096 && r.uniformContextSpan().bakedPitchScale()==SourcePosition{1,1,2},"Short history did not exercise unsupported legacy window/baked pitch");
            unsigned reads=0,writes=0;
            try{renderRequestedCompSpan(legacy,[&](Frame,std::span<float>){++reads;throw ProjectError(ErrorCode::InvalidState,"Unexpected legacy source read");},[&](auto){++writes;},ledger,[]{});}
            catch(const ProjectError &error){legacyWindowRefused=error.code()==ErrorCode::UnsupportedSchema;}
            check(legacyWindowRefused && !reads && !writes,"Legacy short candidate was not refused before audio I/O");
        }
        const auto name="context-request.json";write(root/name,r.bytes());Json row={{"file",name},{"digest",r.digest()},{"operation",r.operation().str()},{"fragment",1},{"outputFrames",r.outputFrames()},{"legacyProcessingFrames",legacy.span().processingFrames()},{"contextInputFrames",r.uniformContextSpan().inputFrames()},{"legacyWindowRefused",legacyWindowRefused}};
        if(history)row["historicalCompletion"]="media/comp-assemblies/"+s.compPlaybackSources.front().assembly.operation.str()+"/complete.json";
        Json outputs=Json::array({row});write(root/"context-requests.json",outputs.dump());std::cout<<outputs.dump()<<'\n';return 0;
    }
    check(std::string_view(argv[1])=="verify","Unknown fixture mode");const auto outputs=read(root/"context-requests.json");auto s=decodeProject(bytes(root/"context-project.json"));const auto original=s;
    std::uint64_t compared=0;
    for(const auto &row:outputs){const unsigned side=row.at("fragment").get<unsigned>();auto r=decodeCompSpanRequest(bytes(root/row.at("file").get<std::string>()),row.at("digest").get<std::string>(),{7,"parent-fixture"},input.at("workerSha256").get<std::string>(),ledger);
        verifyCompProcessingCurrent(s,{7,"parent-fixture"},r.recipe().plan());verifyCompSpanReady(read(root/("ready-"+std::to_string(side)+".json")).dump(),r,policy(),ledger);
        verifyCompSpanHistory(root,r,policy(),ledger,[]{});
        auto verified=verifyOwnedCompSpan(root,r,policy(),ledger);check(verified.asset().frames==r.outputFrames() && verified.requestDigest()==r.digest(),"Owned span output lost identity");
        {
            const std::array<Asset,2> assets{r.source(),verified.asset()};MediaCacheConfig config;config.maximumOpenFiles=2;config.pageFrames=512;config.cacheBudgetBytes=1024*1024;config.registryBudgetBytes=256*1024;config.resources=ledger;
            MediaReadCache cache(root,assets,r.source().sampleRate,config);Frame at=0;std::vector<float> actual(1024);
            renderRequestedCompSpan(r,[&](Frame first,std::span<float> slab){cache.read(0,first,slab);},[&](std::span<const float> expected){auto view=std::span(actual).first(expected.size());cache.read(1,at,view);
                for(std::size_t i=0;i<expected.size();++i){check(std::bit_cast<std::uint32_t>(expected[i])==std::bit_cast<std::uint32_t>(view[i]),"Worker PCM differs from full core recomputation");}
                at+=Frame(expected.size()/2);compared+=expected.size();},ledger,[]{});
            check(at==r.outputFrames(),"Worker recomputation extent differs");

        }
        const auto refusal=[&](auto action){try{action();}catch(const ProjectError &){++checks;return;}throw std::runtime_error("Invalid owned span accepted");};
        auto wrongReady=read(root/("ready-"+std::to_string(side)+".json"));wrongReady["uniformContext"]["chunkFrames"]=257;
        refusal([&]{verifyCompSpanReady(wrongReady.dump(),r,policy(),ledger);});
        std::stop_source canceled;canceled.request_stop();refusal([&]{verifyOwnedCompSpan(root,r,policy(),ledger,canceled.get_token());});
        const auto path=root/utf8Path(verified.asset().relativePath);const auto audio=bytes(path);auto corrupt=audio;corrupt.back()^=1;write(path,corrupt);refusal([&]{verifyOwnedCompSpan(root,r,policy(),ledger);});write(path,audio);
        const auto marker=path.parent_path()/"complete.json";const auto complete=bytes(marker);auto altered=Json::parse(complete);altered["fragment"]=1-side;write(marker,altered.dump());refusal([&]{verifyOwnedCompSpan(root,r,policy(),ledger);});write(marker,complete);
        altered=Json::parse(complete);altered["uniformContext"]["candidateOrigin"][0]=altered["uniformContext"]["candidateOrigin"][0].get<Frame>()+1;
        write(marker,altered.dump());refusal([&]{verifyOwnedCompSpan(root,r,policy(),ledger);});write(marker,complete);
        const auto rawPath=root/"media/raw.wav";const auto raw=bytes(rawPath);auto bad=raw;bad.back()^=1;write(rawPath,bad);refusal([&]{verifyOwnedCompSpan(root,r,policy(),ledger);});write(rawPath,raw);
        auto changed=s;changed.tracks[0].clips[side].processing.gainDb=1;refusal([&]{verifyCompProcessingCurrent(changed,{7,"parent-fixture"},r.recipe().plan());});
        check(s==original,"Owned verification modified project");
    }
    check(ledger.usage().reservedBytes==0,"Owned request/verification leaked payload");std::cout<<Json{{"checks",checks},{"completedSpans",1},{"comparedSamples",compared},{"workerProtocolIntegrated",true},{"uniformOriginalContext",true},{"modelAdoptionChanged",false},{"qualityAdmission",false}}.dump()<<'\n';return 0;
}catch(const std::exception &e){std::cerr<<e.what()<<'\n';return 1;}}
