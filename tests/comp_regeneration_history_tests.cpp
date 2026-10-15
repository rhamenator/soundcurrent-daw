// SPDX-License-Identifier: GPL-3.0-only
#include <soundcurrent/comp_regeneration_history.hpp>
#include <soundcurrent/comp_assembly_adoption.hpp>
#include <chrono>
#include <soundcurrent/comp_pair_recovery.hpp>
#include <soundcurrent/project_store.hpp>
#include "media_io.hpp"
#include <nlohmann/json.hpp>
#include <algorithm>
#include <fstream>
#include <iostream>
#include <type_traits>
using namespace soundcurrent::daw;
namespace {
using Json=nlohmann::json;unsigned checks=0;Json refusals=Json::array();
void check(bool v,const char *m){++checks;if(!v)throw std::runtime_error(m);}
template<class F>void refuses(const char *label,F f){try{f();}catch(const ProjectError &e){++checks;refusals.push_back({{"case",label},{"reason",e.what()}});return;}throw std::runtime_error(std::string("Accepted invalid history: ")+label);}
std::string read(const std::filesystem::path &p){std::ifstream f(p,std::ios::binary);check(bool(f),"Missing history fixture file");return {(std::istreambuf_iterator<char>(f)),{}};}
void write(const std::filesystem::path &p,std::string_view b){std::ofstream f(p,std::ios::binary|std::ios::trunc);f.write(b.data(),std::streamsize(b.size()));check(bool(f),"Cannot write owned history fixture");}
const CompHandlePlanContext context{91,"history-review/é-日本"};
Json pairedReviewWorkflow(const std::filesystem::path &root,const Session &s,const Session &registered,ResourceLedger ledger){
    using Clock=std::chrono::steady_clock;
    const auto noProgress=[]{};
    const auto sourcesFor=[&](const Session &current){
        const auto &t=current.tracks[0];
        return std::array{
            std::make_shared<const CompRegenerationSource>(resolveCompRegenerationSource(current,t.id,t.clips[0].id,context,ledger)),
            std::make_shared<const CompRegenerationSource>(resolveCompRegenerationSource(current,t.id,t.clips[1].id,context,ledger))};
    };
    const auto sources=sourcesFor(s);const auto sourceBytes=ledger.usage().reservedBytes;
    std::uint64_t separateSamples=0,pairedSamples=0;unsigned separateAssemblies=0,pairedAssemblies=0;
    const auto start=Clock::now();
    for(const auto &source:sources){auto review=reviewCompRegenerationHistory(root,s,context,source,ledger);
        for(const auto &entry:review.data().entries){separateSamples+=entry.comparedSamples;++separateAssemblies;}}
    const auto separateSeconds=std::chrono::duration<double>(Clock::now()-start).count();
    const auto pairedStart=Clock::now();
    {
        const auto reviews=reviewCompPairedRegenerationHistory(root,s,context,sources,ledger,{},noProgress);
        check(reviews[0] && !reviews[1],"Paired history repeated a completed exact closure");
        for(const auto &entry:reviews[0]->data().entries){pairedSamples+=entry.comparedSamples;++pairedAssemblies;}
    }
    const auto pairedSeconds=std::chrono::duration<double>(Clock::now()-pairedStart).count();
    check(separateAssemblies==2 && pairedAssemblies==1 && separateSamples==2*pairedSamples && pairedSamples>0,
        "Paired review did not preserve one complete PCM comparison");
    {
        const std::array reversed{sources[1],sources[0]};
        const auto reviews=reviewCompPairedRegenerationHistory(root,s,context,reversed,ledger,{},noProgress);
        check(reviews[0] && !reviews[1] && reviews[0]->data().entries[0].comparedSamples==pairedSamples,"Reversed coverage changed verified PCM");
        const std::array<std::shared_ptr<const CompRegenerationSource>,2> incomingOnly{nullptr,sources[1]};
        const auto single=reviewCompPairedRegenerationHistory(root,s,context,incomingOnly,ledger,{},noProgress);
        check(!single[0] && single[1],"Unchanged outgoing side lost incoming history");
        const auto empty=reviewCompPairedRegenerationHistory(root,s,context,{},ledger,{},noProgress);
        check(!empty[0] && !empty[1],"Empty history produced trust");
    }
    check(ledger.usage().reservedBytes==sourceBytes,"Paired review leaked joined credits");
    for(unsigned side=0;side<2;++side){auto stale=s;stale.tracks[0].clips[side].processing.gainDb+=1;
        refuses("paired stale current source",[&]{reviewCompPairedRegenerationHistory(root,stale,context,sources,ledger,{},noProgress);});}
    {
        auto changed=s;bool injected=false;
        refuses("covered current source changes during review",[&]{reviewCompPairedRegenerationHistory(root,changed,context,sources,ledger,{},[&]{
            if(!injected && ledger.usage().reservedBytes>sourceBytes){changed.tracks[0].clips[1].processing.gainDb+=1;injected=true;}
        });});
        check(injected,"Covered source mutation did not reach completed review");
    }
    std::stop_source stopped;stopped.request_stop();
    refuses("paired pre-cancel",[&]{reviewCompPairedRegenerationHistory(root,s,context,sources,ledger,{},noProgress,stopped.get_token());});
    {
        std::stop_source canceled;bool injected=false;
        refuses("paired cancel after first review",[&]{reviewCompPairedRegenerationHistory(root,s,context,sources,ledger,{},[&]{
            if(ledger.usage().reservedBytes>sourceBytes){injected=true;canceled.request_stop();}
        },canceled.get_token());});check(injected,"Paired cancellation was not injected");
    }
    refuses("paired missing poll",[&]{reviewCompPairedRegenerationHistory(root,s,context,sources,ledger,{},{});});
    refuses("paired memory limit",[&]{reviewCompPairedRegenerationHistory(root,s,context,sources,ResourceLedger(1,"Tiny paired history"),{},noProgress);});
    CompHistoryReviewLimits shortBudget;shortBudget.maximumComparedSamples=pairedSamples-1;
    refuses("paired sample budget",[&]{reviewCompPairedRegenerationHistory(root,s,context,sources,ledger,shortBudget,noProgress);});
    const auto sibling=root/utf8Path(sources[1]->data().current.relativePath);
    const auto siblingBytes=read(sibling);auto changedSibling=siblingBytes;changedSibling.back()^=1;write(sibling,changedSibling);
    try{refuses("covered paired output corruption",[&]{reviewCompPairedRegenerationHistory(root,s,context,sources,ledger,{},noProgress);});}
    catch(...){write(sibling,siblingBytes);throw;}write(sibling,siblingBytes);
    const auto marker=root/"media/comp-assemblies"/sources[0]->data().history.front().assembly.operation.str()/"complete.json";
    const auto original=read(marker);auto altered=original;altered.back()^=1;
    {
        // Holding an earlier review never grants filesystem trust to a later call.
        const auto earlier=reviewCompPairedRegenerationHistory(root,s,context,sources,ledger,{},noProgress);
        write(marker,altered);
        try{refuses("paired cross-operation corruption",[&]{reviewCompPairedRegenerationHistory(root,s,context,sources,ledger,{},noProgress);});}
        catch(...){write(marker,original);throw;}write(marker,original);
    }
    check(ledger.usage().reservedBytes==sourceBytes,"Paired refusal leaked joined credits");
    // Produce a second independent original assembly using the same already
    // completed raw-source children. Its histories are disjoint, even though the
    // original selection and raw media are shared. Do not fabricate job receipts.
    auto pair=loadReferencedCompPairSelection(root,registered,context,registered.compPairSelections[0].operation,ledger);
    auto request=prepareCompAssemblyRequest(registered,context,pair,Id::generate(),{Id::generate(),Id::generate()},compAssemblyExecutableSha256(),{},ledger);
    auto independent=registered;
    const auto reference=persistCompAssemblyRequest(root,independent,context,pair,request,ledger);
    applySessionEdits(independent,{AddCompAssembly{reference}});
    publishCompAssembly(root,independent,context,pair,request,ledger);
    const auto adoption=verifyCompAssemblyAdoption(root,independent,context,pair,request,ledger);
    applySessionEdits(independent,{adoption.edit()});
    for(const auto &asset:s.assets)if(std::none_of(independent.assets.begin(),independent.assets.end(),[&](const auto &a){return a.id==asset.id;}))independent.assets.push_back(asset);
    // The first original assembly was pending in registered, but its outputs
    // are now being retained as adopted history. Release its reserved output IDs.
    check(std::erase_if(independent.compAssemblies,[&](const auto &pending){return pending==s.compPlaybackSources.front().assembly;})==1,
        "Independent fixture lost first pending assembly");
    independent.compPlaybackSources.insert(independent.compPlaybackSources.end(),s.compPlaybackSources.begin(),s.compPlaybackSources.end());
    independent.tracks[0].clips[0]=s.tracks[0].clips[0];validate(independent);
    const auto distinct=sourcesFor(independent);
    check(distinct[0]->data().history.size()==2 && distinct[1]->data().history.size()==2 &&
        distinct[0]->data().history[0].assembly.operation!=distinct[1]->data().history[0].assembly.operation,"Disjoint fixture shares its assembly");
    for(bool reverse:{false,true}){
        const auto ordered=reverse?std::array{distinct[1],distinct[0]}:distinct;
        const auto reviews=reviewCompPairedRegenerationHistory(root,independent,context,ordered,ledger,{},noProgress);
        check(reviews[0] && reviews[1] && reviews[0]->data().entries.size()==1 && reviews[1]->data().entries.size()==1,
            "Disjoint historical assembly skipped");
    }
    const auto secondMarker=root/"media/comp-assemblies"/reference.operation.str()/"complete.json";
    const auto secondBytes=read(secondMarker);write(secondMarker,secondBytes+"!");
    try{refuses("disjoint second history corruption",[&]{reviewCompPairedRegenerationHistory(root,independent,context,distinct,ledger,{},noProgress);});}
    catch(...){write(secondMarker,secondBytes);throw;}write(secondMarker,secondBytes);
    return {{"separateAssemblies",separateAssemblies},{"pairedAssemblies",pairedAssemblies},{"separateSamples",separateSamples},
        {"pairedSamples",pairedSamples},{"separateSeconds",separateSeconds},{"pairedSeconds",pairedSeconds},
        {"bothOrders",true},{"disjointAssembliesVerified",2},{"additionalOriginalAssemblyFixtures",1},{"staleCoveredSourceRefused",true},{"crossOperationCorruptionRefused",true},{"coveredOutputCorruptionRefused",true},
        {"cancellationRefused",true},{"disjointCorruptionRefused",true}};
}
static_assert(!std::is_copy_constructible_v<ReviewedCompRegenerationHistory> && !std::is_move_assignable_v<ReviewedCompRegenerationHistory>);
}
int main(){try{
    Json input;std::cin>>input;const auto root=utf8Path(input.at("root").get<std::string>());
    const auto projectBytes=read(root/"project.json"),adoptedBytes=read(root/"adopted-project.json");
    const auto s=decodeProject(adoptedBytes);const auto original=s;validate(s);
    ResourceLedger ledger(512*1024*1024,"Historical assembly review test");Json reviewed=Json::array();
    const bool pairedOnly=input.value("pairedOnly",false);
    if(!pairedOnly)for(unsigned side=0;side<2;++side){
        auto source=std::make_shared<const CompRegenerationSource>(resolveCompRegenerationSource(s,s.tracks[0].id,s.tracks[0].clips[side].id,context,ledger));
        const auto sourceBytes=ledger.usage().reservedBytes;
        {
            auto review=reviewCompRegenerationHistory(root,s,context,source,ledger);
            check(review.data().source==source && review.data().entries.size()==1,"Shared pair verified more than once");
            const auto &entry=review.data().entries.front();std::uint64_t samples=0;
            for(const auto &h:source->data().history)samples+=std::uint64_t(h.source.rendered.frames)*h.source.rendered.layout.channels;
            check(entry.comparedSamples==samples && samples>0,"Historical full PCM comparison count differs");
            check(!review.data().retained.empty() && !review.data().assets.empty(),"Original source dependencies lost");
            verifyCompRegenerationHistoryCurrent(s,context,review);
            auto renamed=s;renamed.name="Unrelated project title";verifyCompRegenerationHistoryCurrent(renamed,context,review);
            // Moving the live clips after adoption must not change the historical
            // source selection used for completed-job verification.
            auto edited=s;edited.tracks[0].clips[side].processing.gainDb+=1;
            auto editedSource=std::make_shared<const CompRegenerationSource>(resolveCompRegenerationSource(edited,edited.tracks[0].id,edited.tracks[0].clips[side].id,context,ledger));
            auto editedReview=reviewCompRegenerationHistory(root,edited,context,editedSource,ledger);
            check(editedReview.data().entries[0].comparedSamples==samples,"Current clip edit invalidated original job");
            refuses("stale current clip",[&]{verifyCompRegenerationHistoryCurrent(edited,context,review);});
            for(unsigned mutation=0;mutation<6;++mutation){auto changed=s;auto ctx=context;
                if(mutation==0)++ctx.projectEpoch;
                if(mutation==1)changed.assets.front().sha256=std::string(64,'0');
                if(mutation==2)changed.compPlaybackSources.back().selectionSha256=std::string(64,'0');
                if(mutation==3)changed.compPairSelections.front().selectionSha256=std::string(64,'0');
                if(mutation==4)changed.tracks.front().takeLanes.front().clips.front().processing.gainDb+=1;
                if(mutation==5)changed.tracks.front().layout.channels=1;
                refuses("stale dependency",[&]{verifyCompRegenerationHistoryCurrent(changed,ctx,review);});
            }
            const auto selection=entry.assembly.selectionOperation;
            refuses("live clips are not original selection",[&]{loadCompPairSelection(root,s,context,selection,entry.selectionSha256,ledger);});
            auto snapshot=loadCompPairSelectionSnapshot(root,context,selection,entry.selectionSha256,ledger);
            check(snapshot.recipe().selection()!=s,"Snapshot restored live adopted state");
            verifyCompProcessingCurrent(snapshot.recipe().selection(),context,snapshot.recipe().plan());
            const auto beforeGuard=ledger.usage().reservedBytes;
            {
                const auto &d=review.data();const std::array<CompAssemblyDependencyView,1> views{{{d.assets,d.history,d.selections,d.retained,d.tracks}}};
                auto guard=std::make_shared<const CompAssemblyDependencies>(captureCompAssemblyDependencies(s,snapshot.recipe().selection(),views,ledger));
                check(guard->ownedBy(ledger) && !guard->ownedBy(ResourceLedger(1024,"Other ledger")),"Guard lost joined ownership");
                check(guard->data().history.size()==source->data().history.size() && guard->data().history.size()==2,"Guard omitted paired ancestry");
                check(!guard->data().retained.empty() && !guard->data().tracks.empty(),"Guard omitted retained sources/layouts");
                std::vector<std::shared_ptr<const CompAssemblyDependencies>> queue{guard};const auto queuedBytes=ledger.usage().reservedBytes;guard.reset();
                check(ledger.usage().reservedBytes==queuedBytes,"Queued guard released its memory too early");
                const auto &queued=*queue.front();verifyCompAssemblyDependencies(s,queued);
                auto unrelated=s;unrelated.name="Unrelated title";unrelated.tracks[0].name="Renamed track";
                // The real adopted fixture deliberately has no track EQ bands.
                unrelated.tracks[0].eq.bands.push_back(EqBand{});
                unrelated.tracks[0].eq.bands.back().gainDb=1;
                validate(unrelated);verifyCompAssemblyDependencies(unrelated,queued);
                for(unsigned mutation=0;mutation<8;++mutation){auto changed=s;
                    if(mutation==0)changed.id=Id::generate();
                    if(mutation==1)changed.sampleRate=88200;
                    if(mutation==2)changed.assets.front().sha256=std::string(64,'0');
                    if(mutation==3)changed.compPlaybackSources.back().selectionSha256=std::string(64,'0');
                    if(mutation==4)changed.compPairSelections.front().selectionSha256=std::string(64,'0');
                    if(mutation==5)changed.tracks.front().takeLanes.front().clips.front().processing.gainDb+=1;
                    if(mutation==6)changed.tracks.front().layout.channels=1;
                    if(mutation==7)changed.compPlaybackSources.erase(changed.compPlaybackSources.begin());
                    const auto before=changed;refuses("queued dependency changed",[&]{verifyCompAssemblyDependencies(changed,queued);});check(changed==before,"Guard mutated stale project");
                }
                refuses("tiny dependency guard memory",[&]{captureCompAssemblyDependencies(s,snapshot.recipe().selection(),views,ResourceLedger(1,"Tiny guard"));});
                queue.clear();check(ledger.usage().reservedBytes==beforeGuard,"Last queued guard retained credits");
            }
            check(ledger.usage().reservedBytes==beforeGuard,"Dependency guard leaked memory");
            if(side==0){
                const auto attempt=[&]{(void)reviewCompRegenerationHistory(root,s,context,source,ledger);};
                const auto missing=[&](const char *label,const std::filesystem::path &p){const auto parked=std::filesystem::path(p.native()+std::filesystem::path(".history-test-parked").native());
                    check(!std::filesystem::exists(parked),"Fixture parking destination occupied");std::filesystem::rename(p,parked);
                    try{refuses(label,attempt);}catch(...){std::filesystem::rename(parked,p);throw;}std::filesystem::rename(parked,p);
                };
                const auto corrupt=[&](const char *label,const std::filesystem::path &p){const auto saved=read(p);auto bad=saved;check(!bad.empty(),"Empty tamper fixture");bad.back()^=1;write(p,bad);
                    try{refuses(label,attempt);}catch(...){write(p,saved);throw;}write(p,saved);check(read(p)==saved,"Owned fixture not restored");
                };
                const auto assembly=root/"media/comp-assemblies"/entry.assembly.operation.str();
                missing("missing original completion",assembly/"complete.json");
                corrupt("changed original completion",assembly/"complete.json");
                corrupt("changed original intent",assembly/"intent.json");
                corrupt("changed trusted assembly request",root/"media/comp-assembly-requests"/(entry.assembly.operation.str()+".json"));
                corrupt("changed trusted pair",root/"media/comp-pairs"/(selection.str()+".json"));
                corrupt("changed original recipe",root/"media/comp-recipes"/(selection.str()+".json"));
                const auto child=std::find_if(snapshot.children().begin(),snapshot.children().end(),[](const auto &c){return bool(c);});check(child!=snapshot.children().end(),"No original child");
                corrupt("changed original child request",root/"media/comp-requests"/((*child)->operation().str()+".json"));
                missing("missing original nominal completion",(root/utf8Path(source->data().originalNominal->relativePath)).parent_path()/"complete.json");
                corrupt("changed original span completion",root/"media/comp-spans"/(*child)->operation().str()/"complete.json");
                missing("missing original nominal",root/utf8Path(source->data().originalNominal->relativePath));
                corrupt("changed original nominal",root/utf8Path(source->data().originalNominal->relativePath));
                corrupt("changed original raw",root/utf8Path(source->data().raw.relativePath));
                corrupt("changed historical output",root/utf8Path(source->data().current.relativePath));
                {media_io::JobLease active(assembly,false,true);check(active.status()==media_io::LeaseStatus::Held,"Fixture original writer unavailable");refuses("original writer still active",attempt);}
                auto restored=reviewCompRegenerationHistory(root,s,context,source,ledger);check(restored.data().entries[0].comparedSamples==samples,"Restored original verification failed");
            }
            reviewed.push_back({{"side",side},{"assemblies",review.data().entries.size()},{"comparedSamples",samples},{"originalSelectionAssets",review.data().assets.size()},{"queuedDependencyGuard",true},{"queuedStaleRefusals",8}});
            const auto reserved=ledger.usage().reservedBytes;auto moved=std::move(review);refuses("moved review",[&]{review.data();});check(ledger.usage().reservedBytes==reserved,"Move released review credit");verifyCompRegenerationHistoryCurrent(s,context,moved);
        }
        check(ledger.usage().reservedBytes==sourceBytes,"Historical review leaked memory");
        std::stop_source canceled;canceled.request_stop();refuses("canceled review",[&]{reviewCompRegenerationHistory(root,s,context,source,ledger,{},canceled.get_token());});
        refuses("tiny joined memory",[&]{reviewCompRegenerationHistory(root,s,context,source,ResourceLedger(1,"Tiny history"));});
        for(const CompHistoryReviewLimits limits: {CompHistoryReviewLimits{0,64},CompHistoryReviewLimits{1025,64},CompHistoryReviewLimits{64,0},CompHistoryReviewLimits{64,1}})
            refuses("review policy bound",[&]{reviewCompRegenerationHistory(root,s,context,source,ledger,limits);});
        check(ledger.usage().reservedBytes==sourceBytes,"History refusal leaked memory");
    }
    Json paired;
    if(pairedOnly)paired=pairedReviewWorkflow(root,s,decodeProject(projectBytes),ledger);
    check(!ledger.usage().reservedBytes && s==original && read(root/"project.json")==projectBytes && read(root/"adopted-project.json")==adoptedBytes,"History check modified canonical state or leaked memory");
    const Json report{{"pairedReview",paired},{"checks",checks},{"reviews",reviewed},{"refusals",refusals},{"projectBytesUnchanged",true},{"joinedMemoryReleased",true},{"newRendering",pairedOnly},{"reviewerRendersNewAudio",false},{"qualityAdmission",false}};
    write(root/"regeneration-history-report.json",report.dump(2)+"\n");std::cout<<report.dump()<<'\n';return 0;
}catch(const std::exception &e){std::cerr<<e.what()<<'\n';return 1;}}
