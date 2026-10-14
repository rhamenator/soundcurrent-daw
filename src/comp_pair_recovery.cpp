// SPDX-License-Identifier: GPL-3.0-only
#include <soundcurrent/comp_pair_recovery.hpp>
#include <soundcurrent/approved_media.hpp>
#include "media_io.hpp"
#include "warp_codec.hpp"
#include <algorithm>
#include <set>
namespace soundcurrent::daw {
namespace {
using Json=nlohmann::json;
void check(bool v,const char *m,ErrorCode code=ErrorCode::InvalidState){if(!v)throw ProjectError(code,m);}
void poll(std::stop_token stop){check(!stop.stop_requested(),"Comp pair review canceled",ErrorCode::Canceled);}
std::string hash(std::string_view s){media_io::SampleHash h;h.updateBytes({reinterpret_cast<const std::byte *>(s.data()),s.size()});return h.digest();}
bool sha(std::string_view s){return s.size()==64 && std::all_of(s.begin(),s.end(),[](char c){return(c>='0'&&c<='9')||(c>='a'&&c<='f');});}
std::string pairPath(const Id &id){return "media/comp-pairs/"+id.str()+".json";}
std::string requestPath(const Id &id){return "media/comp-requests/"+id.str()+".json";}
std::string jobPath(const Id &id){return "media/comp-spans/"+id.str();}
void folder(const std::filesystem::path &p){if(!std::filesystem::exists(std::filesystem::symlink_status(p))){check(std::filesystem::create_directory(p),"Cannot create comp pair folder",ErrorCode::Io);media_io::flushDirectory(p.parent_path());}media_io::plainDirectory(p);}
void rootPath(const std::filesystem::path &p){check(p.is_absolute() && p==p.lexically_normal() && p.native().size()<=4096,"Comp pair root must be explicit/canonical",ErrorCode::InvalidParameter);media_io::plainDirectory(p);}
struct OwnedBytes {ResourceLease lease;std::string bytes;operator std::string_view() const noexcept {return bytes;}};
OwnedBytes read(ApprovedMediaRoot &root,std::string_view path,std::size_t maximum,ResourceLedger ledger,std::stop_token stop){poll(stop);auto f=root.open(path,maximum,stop);auto credit=ledger.reserve(std::size_t(f.size())*2);std::string bytes(std::size_t(f.size()),'\0');
    for(std::size_t at=0;at<bytes.size();at+=65536){poll(stop);f.readAt(at,{bytes.data()+at,std::min<std::size_t>(65536,bytes.size()-at)},stop);}f.verifyUnchanged();return {std::move(credit),std::move(bytes)};
}
Json parse(std::string_view bytes){check(bytes.size()<=16384,"Comp pair marker too large");std::array<std::set<std::string>,6> keys;
    auto callback=[&](int depth,Json::parse_event_t event,Json &v){check(depth>=0 && depth<4,"Comp pair nesting exceeds bound");if(event==Json::parse_event_t::object_start)keys[std::size_t(depth+1)].clear();if(event==Json::parse_event_t::key)check(keys[std::size_t(depth)].insert(v.get<std::string>()).second,"Duplicate comp pair field");return true;};
    try{return Json::parse(bytes.begin(),bytes.end(),callback);}catch(const Json::exception &){throw ProjectError(ErrorCode::InvalidState,"Invalid comp pair JSON");}
}
Json policy(const CompSpanWorkerPolicy &p){return {{"memoryBytes",p.memoryBytes},{"maximumSourceBytes",p.maximumSourceBytes},{"maximumOutputBytes",p.maximumOutputBytes},{"deadlineMilliseconds",p.deadlineMilliseconds}};}
CompSpanWorkerPolicy policy(const Json &j){check(j.is_object() && j.size()==4,"Invalid saved comp pair policy");return {warp_codec::unsignedInteger(j.at("memoryBytes"),4096ULL*1024*1024),warp_codec::unsignedInteger(j.at("maximumSourceBytes"),8ULL*1024*1024*1024),warp_codec::unsignedInteger(j.at("maximumOutputBytes"),8ULL*1024*1024*1024),warp_codec::unsignedInteger(j.at("deadlineMilliseconds"),3600000)};}
Json document(const CompProcessingRecipe &r,const CompRecipeExecutor &parent,std::string_view worker,const std::array<std::optional<CompSpanRequest>,2> &children,const CompSpanWorkerPolicy &p){
    Json list=Json::array();for(const auto &c:children)list.push_back(c?Json{{"operation",c->operation().str()},{"fragment",c->fragment()},{"requestDigest",c->digest()}}:Json(nullptr));
    return {{"format","sc-comp-pair-selection-v1"},{"operation",r.operation().str()},{"recipeDigest",r.digest()},{"recipeExecutor",{{"id",parent.id},{"buildSha256",parent.buildSha256}}},{"workerSha256",worker},{"policy",policy(p)},{"children",list}};
}
void immutable(const std::filesystem::path &root,std::string_view relative,std::string_view bytes,ResourceLedger ledger,std::stop_token stop){poll(stop);const auto path=root/utf8Path(relative);media_io::plainDirectory(path.parent_path());
    if(std::filesystem::exists(std::filesystem::symlink_status(path))){ApprovedMediaRoot approved(root,ledger,{1});check(read(approved,relative,bytes.size()+1,ledger,stop).bytes==bytes,"Existing comp pair bytes differ",ErrorCode::MediaMismatch);return;}
    const auto temp=path.parent_path()/("pair-"+Id::generate().str()+".partial");bool owned=false;
    try{media_io::File f(temp,true);owned=true;f.write(bytes);f.flush();f.close();poll(stop);media_io::publishMedia(temp,path);owned=false;}
    catch(...){if(owned){std::error_code ignored;std::filesystem::remove(temp,ignored);}throw;}
}
bool present(const std::filesystem::path &p){return std::filesystem::exists(std::filesystem::symlink_status(p));}
}
CompPairSelection prepareCompPairSelection(CompProcessingRecipe recipe,CompRecipeExecutor parent,std::string worker,const std::array<Id,2> &operations,CompSpanWorkerPolicy p,ResourceLedger ledger){
    check(!recipe.plan().handles().outgoing().data().regeneration && !recipe.plan().handles().incoming().data().regeneration,
        "Repeated comp pairs require historical file verification",ErrorCode::UnsupportedSchema);
    check(p.memoryBytes>=128ULL*1024*1024 && p.memoryBytes<=4096ULL*1024*1024 && p.memoryBytes%(1024*1024)==0 && p.maximumSourceBytes>0 && p.maximumSourceBytes<=8ULL*1024*1024*1024 && p.maximumOutputBytes>=4096 && p.maximumOutputBytes<=8ULL*1024*1024*1024 && p.deadlineMilliseconds>=100 && p.deadlineMilliseconds<=3600000,"Invalid paired worker policy");
    check(sha(worker) && operations[0]!=operations[1] && operations[0]!=recipe.operation() && operations[1]!=recipe.operation(),"Invalid paired worker/operations");auto retained=ledger.reserve(65536);std::array<std::optional<CompSpanRequest>,2> children;
    for(unsigned side=0;side<2;++side){const auto domain=side?recipe.plan().incoming().domain:recipe.plan().outgoing().domain;
        if(domain!=CompProcessingDomain::ExistingMedia){children[side].emplace(encodeCompSpanRequest(recipe,parent,side,operations[side],worker,ledger));(void)compSpanIntent(*children[side],p,ledger);}}
    auto bytes=document(recipe,parent,worker,children,p).dump();check(bytes.size()<=16384,"Comp pair selection exceeds bank");const auto digest=hash(bytes);
    return {std::move(retained),std::move(recipe),std::move(children),p,std::move(parent),std::move(worker),digest,std::move(bytes)};
}
std::string persistCompPairSelection(const std::filesystem::path &root,const Session &current,const CompHandlePlanContext &context,const CompPairSelection &s,ResourceLedger ledger,std::stop_token stop){
    rootPath(root);poll(stop);verifyCompProcessingCurrent(current,context,s.recipe().plan());folder(root/"media");folder(root/"media"/"comp-pairs");folder(root/"media"/"comp-requests");folder(root/"media"/"comp-recipes");folder(root/"media"/"comp-spans");
    const auto at=root/"media"/"comp-pairs";const bool fresh=!present(at/"writer.lock");media_io::JobLease lease(at,fresh,!fresh);check(lease.status()==media_io::LeaseStatus::Held,"Comp pair publication busy",ErrorCode::Io);
    immutable(root,"media/comp-recipes/"+s.recipe().operation().str()+".json",s.recipe().bytes(),ledger,stop);
    for(const auto &r:s.children())if(r)immutable(root,requestPath(r->operation()),r->bytes(),ledger,stop);
    immutable(root,pairPath(s.recipe().operation()),s.manifest(),ledger,stop);return s.digest();
}
CompPairSelection loadCompPairSelectionSnapshot(const std::filesystem::path &root,const CompHandlePlanContext &context,const Id &op,std::string_view digest,ResourceLedger ledger,std::stop_token stop){
    poll(stop);check(sha(digest),"Missing trusted comp pair selection digest");auto work=ledger.reserve(512*1024);ApprovedMediaRoot approved(root,ledger,{1});const auto bytes=read(approved,pairPath(op),16384,ledger,stop);check(hash(bytes)==digest,"Saved comp pair changed from trusted owner",ErrorCode::MediaMismatch);
    try{const auto j=parse(bytes);check(j.is_object() && j.size()==7 && j.at("format")=="sc-comp-pair-selection-v1" && j.at("operation")==op.str(),"Invalid comp pair identity/schema");
        const CompRecipeExecutor parent{j.at("recipeExecutor").at("id").get<std::string>(),j.at("recipeExecutor").at("buildSha256").get<std::string>()};const auto worker=j.at("workerSha256").get<std::string>();const auto p=policy(j.at("policy"));
        auto recipe=loadCompProcessingRecipeSnapshot(root,context,op,parent,j.at("recipeDigest").get<std::string>(),ledger,{},stop);
        const auto &list=j.at("children");check(list.is_array() && list.size()==2,"Invalid pair child count");std::array<Id,2> ops{Id::generate(),Id::generate()};
        for(unsigned side=0;side<2;++side)if(!list[side].is_null()){check(list[side].is_object() && list[side].size()==3 && warp_codec::unsignedInteger(list[side].at("fragment"),1)==side,"Wrong saved child fragment");ops[side]=Id(list[side].at("operation").get<std::string>());}
        auto result=prepareCompPairSelection(std::move(recipe),parent,worker,ops,p,ledger);check(result.manifest()==bytes.bytes && result.digest()==digest,"Saved pair differs from rederived requests");
        for(unsigned side=0;side<2;++side)if(result.children()[side]){const auto &r=*result.children()[side];const auto saved=read(approved,requestPath(r.operation()),compSpanRequestMaximum,ledger,stop);check(saved.bytes==r.bytes() && hash(saved)==r.digest(),"Original paired request changed",ErrorCode::MediaMismatch);}
        poll(stop);return result;
    }catch(const Json::exception &){throw ProjectError(ErrorCode::InvalidState,"Invalid saved comp pair fields");}
}
CompPairSelection loadCompPairSelection(const std::filesystem::path &root,const Session &current,const CompHandlePlanContext &context,const Id &op,std::string_view digest,ResourceLedger ledger,std::stop_token stop){
    auto result=loadCompPairSelectionSnapshot(root,context,op,digest,ledger,stop);
    verifyCompProcessingCurrent(current,context,result.recipe().plan());return result;
}
CompPairSelectionReference referenceCompPairSelection(const Session &current,const CompHandlePlanContext &context,const CompPairSelection &selection) {
    verifyCompProcessingCurrent(current,context,selection.recipe().plan());
    return {selection.recipe().operation(),selection.digest()};
}
CompPairSelection loadReferencedCompPairSelection(const std::filesystem::path &root,const Session &current,const CompHandlePlanContext &context,const Id &operation,ResourceLedger ledger,std::stop_token stop) {
    poll(stop);const StateBudget budget{ledger.usage().limitBytes};
    auto validation=ledger.reserve(sessionPayloadBytes(current,budget));validate(current,budget);
    const auto found=std::find_if(current.compPairSelections.begin(),current.compPairSelections.end(),[&](const auto &r){return r.operation==operation;});
    check(found!=current.compPairSelections.end(),"No trusted project comp selection reference",ErrorCode::InvalidId);
    return loadCompPairSelection(root,current,context,operation,found->selectionSha256,ledger,stop);
}
bool CompPairRecovery::candidatesReady() const noexcept {if(staleSelection)return false;return std::all_of(children.begin(),children.end(),[](const auto &c){return c.state==CompPairChildState::Unneeded || (c.state==CompPairChildState::Complete && c.candidate.has_value());});}
CompPairRecovery inspectCompPair(const std::filesystem::path &root,const Session &current,const CompHandlePlanContext &context,const CompPairSelection &s,ResourceLedger ledger,std::stop_token stop){
    CompPairRecovery result;poll(stop);try{verifyCompProcessingCurrent(current,context,s.recipe().plan());}catch(const ProjectError &e){result.staleSelection=true;for(unsigned i=0;i<2;++i)if(s.children()[i]){result.children[i].state=CompPairChildState::Stale;result.children[i].error=e.code();}return result;}
    for(unsigned side=0;side<2;++side){poll(stop);if(!s.children()[side])continue;const auto &r=*s.children()[side];auto &out=result.children[side];const auto relative=jobPath(r.operation());const auto folder=root/utf8Path(relative);
        try{if(!present(folder)){out.state=CompPairChildState::Absent;continue;}media_io::plainDirectory(folder);out.cancellationRequested=present(folder/"cancel.request");media_io::JobLease lease(folder,false);
            if(lease.status()==media_io::LeaseStatus::Busy){out.state=CompPairChildState::Active;continue;}check(lease.status()==media_io::LeaseStatus::Held,"Comp pair child ownership absent");
            if(!present(folder/"intent.json")){out.state=CompPairChildState::Interrupted;continue;}ApprovedMediaRoot approved(root,ledger,{1});check(warp_codec::exact(parse(read(approved,relative+"/intent.json",16384,ledger,stop)),parse(compSpanIntent(r,s.policy(),ledger))),"Saved child intent differs from pair");
            try{admitCompSpanSources(root,r,s.policy(),ledger,[&]{poll(stop);});}catch(const ProjectError &e){if(e.code()==ErrorCode::Canceled)throw;out.state=CompPairChildState::Stale;out.error=e.code();continue;}
            if(present(folder/"complete.json")){out.candidate.emplace(verifyOwnedCompSpan(root,r,s.policy(),ledger,stop));out.state=CompPairChildState::Complete;}
            else if(out.cancellationRequested)out.state=CompPairChildState::Canceled;
            else if(present(folder/"start.request"))out.state=CompPairChildState::Interrupted;
            else out.state=CompPairChildState::WaitingForStart;
        }catch(const ProjectError &e){if(e.code()==ErrorCode::Canceled)throw;out.state=CompPairChildState::Invalid;out.error=e.code();}
    }
    poll(stop);return result;
}
void startCompPairChild(const std::filesystem::path &root,const Session &current,const CompHandlePlanContext &context,const CompPairSelection &s,unsigned side,std::string_view ready,ResourceLedger ledger,std::stop_token stop){
    check(side<2 && s.children()[side].has_value(),"Missing paired child start target");poll(stop);verifyCompProcessingCurrent(current,context,s.recipe().plan());const auto &r=*s.children()[side];media_io::JobLease active(root/utf8Path(jobPath(r.operation())),false);check(active.status()==media_io::LeaseStatus::Busy,"Cannot start a retired/absent paired child");verifyCompSpanReady(ready,r,s.policy(),ledger);
    ApprovedMediaRoot approved(root,ledger,{1});const auto relative=jobPath(r.operation());check(warp_codec::exact(parse(read(approved,relative+"/intent.json",16384,ledger,stop)),parse(compSpanIntent(r,s.policy(),ledger))),"Child intent differs before start");
    check(!present(root/utf8Path(relative+"/cancel.request")) && !present(root/utf8Path(relative+"/complete.json")),"Cannot start canceled/completed child");
    immutable(root,relative+"/start.request",r.digest()+"\n",ledger,stop);
}
void cancelCompPair(const std::filesystem::path &root,const CompPairSelection &s,ResourceLedger ledger,std::stop_token stop){
    rootPath(root);media_io::plainDirectory(root/"media");media_io::plainDirectory(root/"media"/"comp-spans");
    for(const auto &r:s.children())if(r){poll(stop);const auto relative=jobPath(r->operation());if(present(root/utf8Path(relative)))immutable(root,relative+"/cancel.request",r->digest()+"\n",ledger,stop);}
}
} // namespace soundcurrent::daw
