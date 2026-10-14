// SPDX-License-Identifier: GPL-3.0-only
#include <soundcurrent/comp_pair_recovery.hpp>
#include <nlohmann/json.hpp>
#include <fstream>
#include <iostream>
using namespace soundcurrent::daw;
namespace {
using Json=nlohmann::json;unsigned checks=0;
void check(bool v,const char *m){++checks;if(!v)throw std::runtime_error(m);}
std::string read(const std::filesystem::path &p){std::ifstream f(p,std::ios::binary);check(bool(f),"Pair fixture file missing");return {(std::istreambuf_iterator<char>(f)),{}};}
void write(const std::filesystem::path &p,std::string_view bytes){std::ofstream f(p,std::ios::binary|std::ios::trunc);f.write(bytes.data(),std::streamsize(bytes.size()));check(bool(f),"Pair fixture write failed");}
CompSpanWorkerPolicy policy(){return {256*1024*1024,64*1024*1024,64*1024*1024,10000};}
const CompHandlePlanContext context{9,"fresh-pair-owner"};
Json state(const CompPairRecovery &r){Json a=Json::array();for(const auto &c:r.children)a.push_back({{"state",unsigned(c.state)},{"cancelRequested",c.cancellationRequested},{"candidate",c.candidate.has_value()}});return {{"children",a},{"ready",r.candidatesReady()},{"staleSelection",r.staleSelection}};}
template<class F>void refuses(F f){try{f();}catch(const ProjectError &){++checks;return;}throw std::runtime_error("Invalid pair accepted");}
}
int main(int argc,char **argv){try{check(argc==2,"Expected pair fixture mode");Json input;std::cin>>input;const auto root=utf8Path(input.at("root").get<std::string>());ResourceLedger ledger(512*1024*1024,"Pair fixture snapshots");auto current=ProjectStore(root).load();const auto original=current;
    if(std::string_view(argv[1])=="prepare"){
        const auto requests=Json::parse(read(root/"requests.json"));const auto first=read(root/requests[0].at("file").get<std::string>());const auto j=Json::parse(first);
        const CompRecipeExecutor parent{j.at("recipeExecutor").at("id"),j.at("recipeExecutor").at("buildSha256")};
        auto recipe=decodeCompProcessingRecipe(j.at("recipe").get<std::string>(),Id(j.at("recipeOperation")),context,parent,j.at("recipeDigest").get<std::string>(),ledger);
        const auto binary=j.at("executor").at("buildSha256").get<std::string>();std::array<Id,2> operations{Id(requests[0].at("operation")),Id(requests[1].at("operation"))};
        auto selectedPolicy=policy();selectedPolicy.deadlineMilliseconds=input.value("deadline",10000u);auto selection=prepareCompPairSelection(std::move(recipe),parent,binary,operations,selectedPolicy,ledger);const auto digest=persistCompPairSelection(root,current,context,selection,ledger);
        check(persistCompPairSelection(root,current,context,selection,ledger)==digest,"Repeated selection publication changed bytes");
        auto fresh=loadCompPairSelection(root,current,context,selection.recipe().operation(),digest,ledger);check(fresh.digest()==digest && fresh.manifest()==selection.manifest(),"Selection reopened differently");
        const Json owner{{"operation",selection.recipe().operation().str()},{"digest",digest}};write(root/"trusted-owner-reference.json",owner.dump());std::cout<<owner.dump()<<'\n';return 0;
    }
    const auto owner=Json::parse(read(root/"trusted-owner-reference.json"));const auto op=Id(owner.at("operation"));const auto digest=owner.at("digest").get<std::string>();auto pair=loadCompPairSelection(root,current,context,op,digest,ledger);
    if(std::string_view(argv[1])=="start"){
        const auto ready=input.at("ready").dump();const auto side=input.at("ready").at("fragment").get<unsigned>();
        const auto before=inspectCompPair(root,current,context,pair,ledger);check(before.children[side].state==CompPairChildState::Active,"Ready child not active");
        auto changed=current;changed.tracks[0].clips[side].processing.gainDb=1;refuses([&]{startCompPairChild(root,changed,context,pair,side,ready,ledger);});
        startCompPairChild(root,current,context,pair,side,ready,ledger);startCompPairChild(root,current,context,pair,side,ready,ledger);std::cout<<state(before).dump()<<'\n';return 0;
    }
    if(std::string_view(argv[1])=="cancel"){cancelCompPair(root,pair,ledger);cancelCompPair(root,pair,ledger);std::cout<<state(inspectCompPair(root,current,context,pair,ledger)).dump()<<'\n';return 0;}
    if(std::string_view(argv[1])=="inspect"){std::cout<<state(inspectCompPair(root,current,context,pair,ledger)).dump()<<'\n';return 0;}
    if(std::string_view(argv[1])=="retired"){
        refuses([&]{startCompPairChild(root,current,context,pair,0,input.at("ready").dump(),ledger);});
        // Synthetic retained state: the real watchdog retired before start; add
        // the original acknowledgment to exercise interrupted-after-start review.
        write(root/"media/comp-spans"/pair.children()[0]->operation().str()/"start.request",pair.children()[0]->digest()+"\n");
        auto recovered=inspectCompPair(root,current,context,pair,ledger);
        check(recovered.children[0].state==CompPairChildState::Interrupted && !recovered.candidatesReady(),"Interrupted start appeared ready");
        std::cout<<state(recovered).dump()<<'\n';return 0;
    }
    if(std::string_view(argv[1])=="existing"){
        auto existing=current;for(auto &clip:existing.tracks[0].clips){clip.stretch.reset();clip.assetId=existing.assets[0].id;clip.sourceFrame=32768;clip.sourceTiming={};}
        validate(existing);const auto &track=existing.tracks[0];const auto join=track.clips[1].startFrame;
        auto plan=prepareCompProcessing(existing,{track.id,track.clips[0].id,track.clips[1].id,join-26,join+26,ClipFadeCurve::EqualPower},context,8,ledger);
        auto recipe=encodeCompProcessingRecipe(existing,context,plan,Id::generate(),pair.recipeExecutor(),ledger);
        auto selection=prepareCompPairSelection(std::move(recipe),pair.recipeExecutor(),pair.workerSha256(),{Id::generate(),Id::generate()},pair.policy(),ledger);
        check(!selection.children()[0] && !selection.children()[1],"Existing media unexpectedly required child render");
        const auto saved=persistCompPairSelection(root,existing,context,selection,ledger);
        auto reopened=loadCompPairSelection(root,existing,context,selection.recipe().operation(),saved,ledger);
        auto recovered=inspectCompPair(root,existing,context,reopened,ledger);check(recovered.candidatesReady(),"Existing media failed recovery");
        auto stale=context;++stale.projectEpoch;auto rejected=inspectCompPair(root,existing,stale,reopened,ledger);
        check(rejected.staleSelection && !rejected.candidatesReady(),"Unneeded children authorized stale selection");
        check(current==original,"Existing media review changed project");std::cout<<Json{{"existingMediaRecovered",true},{"staleSelectionRejected",true},{"modelAdoptionChanged",false}}.dump()<<'\n';return 0;
    }
    check(std::string_view(argv[1])=="verify","Unknown pair fixture mode");
    {auto recovered=inspectCompPair(root,current,context,pair,ledger);check(recovered.candidatesReady() && recovered.children[0].state==CompPairChildState::Complete && recovered.children[1].state==CompPairChildState::Complete,"Paired completed candidates not recovered");}
    auto stale=context;++stale.projectEpoch;auto staleResult=inspectCompPair(root,current,stale,pair,ledger);check(staleResult.staleSelection && !staleResult.candidatesReady(),"Stale pair authorized readiness");
    const auto pairPath=root/"media"/"comp-pairs"/(op.str()+".json");const auto before=read(pairPath);auto altered=Json::parse(before);altered["workerSha256"]=std::string(64,'0');write(pairPath,altered.dump());
    refuses([&]{loadCompPairSelection(root,current,context,op,digest,ledger);});write(pairPath,before);
    const auto request=root/"media"/"comp-requests"/(pair.children()[0]->operation().str()+".json");const auto bytes=read(request);write(request,bytes+" ");refuses([&]{loadCompPairSelection(root,current,context,op,digest,ledger);});write(request,bytes);
    const auto path=root/"media"/"comp-spans"/pair.children()[0]->operation().str()/"audio.wav";const auto audio=read(path);auto bad=audio;bad.back()^=1;write(path,bad);
    {auto r=inspectCompPair(root,current,context,pair,ledger);check(r.children[0].state==CompPairChildState::Invalid && r.children[1].state==CompPairChildState::Complete && !r.candidatesReady(),"Bad first result exposed paired readiness");}write(path,audio);
    const auto rawPath=root/"media/raw.wav";const auto raw=read(rawPath);bad=raw;bad.back()^=1;write(rawPath,bad);
    {auto r=inspectCompPair(root,current,context,pair,ledger);check(r.children[0].state==CompPairChildState::Stale && !r.candidatesReady(),"Changed source admitted recovered pair");}write(rawPath,raw);
    cancelCompPair(root,pair,ledger);{auto r=inspectCompPair(root,current,context,pair,ledger);check(r.candidatesReady() && r.children[0].cancellationRequested,"Late cancel hid committed pair");}
    std::stop_source canceled;canceled.request_stop();refuses([&]{inspectCompPair(root,current,context,pair,ledger,canceled.get_token());});check(current==original,"Pair recovery changed project");
    std::cout<<Json{{"checks",checks},{"pairedCandidatesRecovered",true},{"modelAdoptionChanged",false},{"qualityAdmission",false}}.dump()<<'\n';return 0;
}catch(const std::exception &e){std::cerr<<e.what()<<'\n';return 1;}}
