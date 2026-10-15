// SPDX-License-Identifier: GPL-3.0-only
#include <soundcurrent/comp_assembly_job.hpp>
#include <soundcurrent/comp_media_review.hpp>
#include <soundcurrent/comp_regeneration_history.hpp>
#include "comp_assembly_file_verifier.hpp"
#include <soundcurrent/approved_media.hpp>
#include <soundcurrent/media_cache.hpp>
#include <soundcurrent/wave_validation.hpp>
#include "media_io.hpp"
#include "warp_codec.hpp"
#include <sndfile.h>
#include <algorithm>
#include <bit>
#include <chrono>
#include <cmath>
#include <set>
namespace soundcurrent::daw {
namespace {
using Json=nlohmann::json;
void check(bool v,const char *m,ErrorCode c=ErrorCode::InvalidState){if(!v)throw ProjectError(c,m);}
bool sha(std::string_view v){return v.size()==64 && std::all_of(v.begin(),v.end(),[](char c){return (c>='0'&&c<='9')||(c>='a'&&c<='f');});}
std::string hash(std::string_view v){media_io::SampleHash h;h.updateBytes({reinterpret_cast<const std::byte *>(v.data()),v.size()});return h.digest();}
bool present(const std::filesystem::path &p){return std::filesystem::exists(std::filesystem::symlink_status(p));}
void directory(const std::filesystem::path &p){if(!present(p)){check(std::filesystem::create_directory(p),"Cannot create assembly directory",ErrorCode::Io);media_io::flushDirectory(p.parent_path());}media_io::plainDirectory(p);}
void rootPath(const std::filesystem::path &p){check(p.is_absolute() && p==p.lexically_normal(),"Assembly root must be canonical absolute");auto at=p.root_path();media_io::plainDirectory(at);for(const auto &part:p.relative_path()){at/=part;media_io::plainDirectory(at);}}
Json parse(std::string_view bytes){check(!bytes.empty() && bytes.size()<=16384,"Assembly marker exceeds bank",ErrorCode::ResourceLimit);std::array<std::set<std::string>,9> keys;unsigned entries=0;
    auto callback=[&](int depth,Json::parse_event_t event,Json &v){check(depth>=0 && depth<7 && ++entries<=1024,"Assembly marker structure exceeds bound");if(event==Json::parse_event_t::object_start)keys[std::size_t(depth+1)].clear();if(event==Json::parse_event_t::key)check(keys[std::size_t(depth)].insert(v.get<std::string>()).second,"Duplicate assembly field");return true;};
    try{return Json::parse(bytes.begin(),bytes.end(),callback);}catch(const Json::exception &){throw ProjectError(ErrorCode::InvalidState,"Invalid assembly JSON");}
}
Json nominalPolicy(const StretchRenderPolicy &p){return {{"memoryBytes",p.memoryBytes},{"maximumInputFrames",p.maximumInputFrames},{"maximumOutputBytes",p.maximumOutputBytes},{"maximumSourceBytes",p.maximumSourceBytes},{"deadlineMilliseconds",p.deadlineMilliseconds}};}
StretchRenderPolicy nominalPolicy(const Json &j){check(j.is_object() && j.size()==5,"Invalid original nominal policy");return {warp_codec::unsignedInteger(j.at("memoryBytes"),4096ULL*1024*1024),warp_codec::unsignedInteger(j.at("maximumInputFrames"),1000000000),warp_codec::unsignedInteger(j.at("maximumOutputBytes"),8ULL*1024*1024*1024),warp_codec::unsignedInteger(j.at("maximumSourceBytes"),8ULL*1024*1024*1024),warp_codec::unsignedInteger(j.at("deadlineMilliseconds"),3600000)};}
Json policy(const CompAssemblyJobPolicy &p){return {{"maximumFileBytes",p.maximumFileBytes},{"maximumPcmBytes",p.assembly.maximumPcmBytes},{"deadlineMilliseconds",p.assembly.deadlineMilliseconds},{"nominalPolicies",Json::array({nominalPolicy(p.assembly.nominalPolicies[0]),nominalPolicy(p.assembly.nominalPolicies[1])})}};}
CompAssemblyJobPolicy policy(const Json &j){check(j.is_object() && j.size()==4 && j.at("nominalPolicies").is_array() && j.at("nominalPolicies").size()==2,"Invalid assembly policy");CompAssemblyJobPolicy p;p.maximumFileBytes=warp_codec::unsignedInteger(j.at("maximumFileBytes"),8ULL*1024*1024*1024);p.assembly.maximumPcmBytes=warp_codec::unsignedInteger(j.at("maximumPcmBytes"),8ULL*1024*1024*1024);p.assembly.deadlineMilliseconds=warp_codec::unsignedInteger(j.at("deadlineMilliseconds"),3600000);for(unsigned side=0;side<2;++side)p.assembly.nominalPolicies[side]=nominalPolicy(j.at("nominalPolicies")[side]);return p;}
const CompHandleEnvelopeData &envelope(const CompPairSelection &p,unsigned s){return (s?p.recipe().plan().handles().incoming():p.recipe().plan().handles().outgoing()).data();}
const CompFragmentProcessing &processing(const CompPairSelection &p,unsigned s){return s?p.recipe().plan().incoming():p.recipe().plan().outgoing();}
std::string jobPath(const CompAssemblyRequest &r){return "media/comp-assemblies/"+r.operation().str();}
Asset output(const CompPairSelection &p,const CompAssemblyRequest &r,unsigned side){const auto &e=envelope(p,side);const auto &v=processing(p,side);Asset a=e.base;a.id=*r.outputs()[side];a.nativeEvidence.clear();a.relativePath="media/comp-results/"+p.recipe().operation().str()+"/"+a.id.str()+".wav";a.frames=v.pcm.prefixFrames+v.pcm.currentFrames+v.pcm.suffixFrames;a.sampleRate=v.domain==CompProcessingDomain::PhysicalArtifact?e.base.sampleRate:e.projectRate;a.sha256.clear();return a;}
void fresh(const Session &s,const CompHandlePlanContext &ctx,const CompPairSelection &p,const CompAssemblyRequest &r){verifyCompProcessingCurrent(s,ctx,p.recipe().plan());check(r.selectionDigest()==p.digest() && std::find(s.compPairSelections.begin(),s.compPairSelections.end(),referenceCompPairSelection(s,ctx,p))!=s.compPairSelections.end(),"Assembly selection lost current trusted reference");}
std::function<void()> polling(const std::filesystem::path &root,const CompAssemblyRequest &r,std::stop_token stop,bool cancelFile){const auto deadline=std::chrono::steady_clock::now()+std::chrono::milliseconds(r.policy().assembly.deadlineMilliseconds);const auto path=root/utf8Path(jobPath(r))/"cancel.request";return [deadline,path,stop,cancelFile]{check(!stop.stop_requested() && (!cancelFile || !present(path)),"Assembly canceled",ErrorCode::Canceled);check(std::chrono::steady_clock::now()<deadline,"Assembly deadline exceeded",ErrorCode::ResourceLimit);};}
void verifyHistory(const std::filesystem::path &root,const Session &s,const CompHandlePlanContext &ctx,
    const CompPairSelection &p,const CompAssemblyRequest &r,ResourceLedger ledger,
    const std::function<void()> &poll,std::stop_token stop){
    const auto &handles=p.recipe().plan().handles();
    const std::array sources{handles.outgoing().data().regeneration,handles.incoming().data().regeneration};
    CompHistoryReviewLimits limits;limits.maximumInputFileBytes=r.policy().maximumFileBytes;
    const auto reviews=reviewCompPairedRegenerationHistory(root,s,ctx,sources,ledger,limits,poll,stop);
}
std::string read(ApprovedMediaRoot &root,std::string_view path,std::stop_token stop,const std::function<void()> &poll){poll();auto file=root.open(path,16384,stop);std::string bytes(std::size_t(file.size()),'\0');file.readAt(0,bytes,stop);file.verifyUnchanged();poll();return bytes;}
void immutable(const std::filesystem::path &path,std::string_view bytes){check(bytes.size()<=16384,"Assembly publication marker exceeds bank");const auto partial=path.parent_path()/(Id::generate().str()+".partial");media_io::File f(partial,true);f.write(bytes);f.flush();f.close();media_io::publishMedia(partial,path);}
struct Wave {
    media_io::File descriptor;SNDFILE *file=nullptr;
    Wave(const std::filesystem::path &p,const Asset &a):descriptor(p,true){SF_INFO info{};info.samplerate=int(a.sampleRate);info.channels=int(a.layout.channels);info.format=SF_FORMAT_RF64|SF_FORMAT_FLOAT;file=sf_open_fd(descriptor.descriptor(),SFM_WRITE,&info,SF_FALSE);check(file!=nullptr,"Cannot create assembly RF64",ErrorCode::Io);}
    ~Wave(){if(file)sf_close(file);}
    void close(){auto f=file;file=nullptr;check(sf_close(f)==0,"Cannot finalize assembly RF64",ErrorCode::Io);descriptor.flush();descriptor.close();}
};
Json row(const Asset &a,CompProcessingDomain domain,std::string samples,double peak){return {{"id",a.id.str()},{"path",a.relativePath},{"sha256",a.sha256},{"sampleSha256",std::move(samples)},{"frames",a.frames},{"rate",a.sampleRate},{"channels",a.layout.channels},{"domain",unsigned(domain)},{"peakLinear",peak}};}
// Independent complete-file validation and complete sample recomputation. The
// separate media reviewer also checks the original outside-edit scopes.
std::uint64_t verifyFiles(const std::filesystem::path &root,const Session &s,const CompHandlePlanContext &ctx,const CompPairSelection &p,const CompAssemblyRequest &r,const std::array<std::optional<Asset>,2> &assets,const Json &rows,ResourceLedger ledger,std::stop_token stop,const std::function<void()> &poll){
    check(rows.is_array() && rows.size()==2,"Wrong assembly output count");std::uint64_t compared=0,totalBytes=0;ApprovedMediaRoot approved(root,ledger,{2});
    std::array<std::optional<ApprovedMediaFile>,2> pinned;
    for(unsigned side=0;side<2;++side){poll();if(!assets[side]){check(rows[side].is_null(),"Unexpected unchanged assembly output");continue;}
        const auto &a=*assets[side];pinned[side].emplace(approved.open(a.relativePath,r.policy().maximumFileBytes,stop));auto &file=*pinned[side];check(file.size()<=r.policy().maximumFileBytes-totalBytes,"Assembly pair exceeds file grant",ErrorCode::ResourceLimit);totalBytes+=file.size();
        std::array<char,4> magic{};file.readAt(0,magic,stop);check(std::string_view(magic.data(),4)=="RF64","Assembly result is not RF64");
        auto credit=ledger.reserve(std::size_t(512)*a.layout.channels*sizeof(float));std::vector<float> slab(std::size_t(512)*a.layout.channels);media_io::SampleHash samples;
        WaveValidationLimits limits;limits.maximumFrames=std::uint64_t(a.frames);limits.maximumChannels=a.layout.channels;limits.blockFrames=512;limits.maximumBytesRead=r.policy().maximumFileBytes*4+65536;limits.maximumIoOperations=16000000;
        const auto wave=validateApprovedWave(file,limits,stop,[&](std::uint64_t,std::span<const double> values){poll();check(values.size()<=slab.size(),"Assembly sample slab exceeded");for(std::size_t i=0;i<values.size();++i)slab[i]=float(values[i]);samples.update({slab.data(),values.size()});},poll);
        check(wave.frames==std::uint64_t(a.frames) && wave.rate==a.sampleRate && wave.channels==a.layout.channels && wave.encoding==WaveEncoding::Float32 && std::string(wave.sourceSha256.begin(),wave.sourceSha256.end())==a.sha256,"Assembly WAV differs from expected asset",ErrorCode::MediaMismatch);
        check(warp_codec::exact(rows[side],row(a,processing(p,side).domain,samples.digest(),wave.peak)),"Assembly completion differs from actual WAV");file.verifyUnchanged();
    }
    // Recompute the two unpublished outputs as one batch: the same complete
    // candidate/nominal pair is checked before and after all requested samples.
    std::uint32_t channels=0;for(const auto &a:assets)if(a)channels=std::max(channels,a->layout.channels);
    auto slabCredit=ledger.reserve(std::size_t(512)*channels*sizeof(float));std::vector<float> slab(std::size_t(512)*channels);
    std::optional<MediaReadCache> cache;unsigned cachedSide=2;std::array<Frame,2> positions{};
    std::array<CompHandlePcmWrite,2> writes;
    for(unsigned side=0;side<2;++side)if(assets[side])writes[side]=[&,side](std::span<const float> expected){
        poll();const auto &a=*assets[side];check(expected.size()<=slab.size(),"Assembly recomputation exceeds slab");
        if(cachedSide!=side){
            cache.reset();MediaCacheConfig config;config.maximumOpenFiles=1;config.pageFrames=256;
            config.cacheBudgetBytes=std::size_t(256)*a.layout.channels*sizeof(float)+256;config.registryBudgetBytes=256*1024;config.resources=ledger;
            cache.emplace(root,std::span<const Asset>(&a,1),s.sampleRate,config,poll);cachedSide=side;
        }
        auto actual=std::span(slab).first(expected.size());cache->read(0,positions[side],actual);
        for(std::size_t n=0;n<actual.size();++n)check(std::bit_cast<std::uint32_t>(actual[n])==std::bit_cast<std::uint32_t>(expected[n]),"Assembly PCM differs from complete recomputation",ErrorCode::MediaMismatch);
        positions[side]+=Frame(expected.size()/a.layout.channels);compared+=expected.size();
    };
    const auto reports=streamCompPairFragments(root,s,ctx,p,writes,ledger,r.policy().assembly,stop);
    for(unsigned side=0;side<2;++side)if(assets[side]){
        check(reports[side] && positions[side]==assets[side]->frames && reports[side]->pcm.writtenFrames==assets[side]->frames,"Assembly recomputation incomplete");
        pinned[side]->verifyUnchanged();
    }
    auto reviewed=reviewCompMedia(root,s,ctx,p.recipe(),assets,ledger,{p.policy().maximumSourceBytes,r.policy().maximumFileBytes,r.policy().assembly.deadlineMilliseconds},stop);poll();return compared;
}
}
CompAssemblyRequest prepareCompAssemblyRequest(const Session &s,const CompHandlePlanContext &ctx,const CompPairSelection &p,Id op,const std::array<std::optional<Id>,2> &outputs,std::string executable,CompAssemblyJobPolicy policyValue,ResourceLedger ledger){
    verifyCompProcessingCurrent(s,ctx,p.recipe().plan());check(sha(executable) && op!=p.recipe().operation(),"Invalid assembly executor/operation");
    const auto &l=policyValue.assembly;check(l.maximumPcmBytes>0 && l.maximumPcmBytes<=8ULL*1024*1024*1024 && l.deadlineMilliseconds>=100 && l.deadlineMilliseconds<=3600000 && policyValue.maximumFileBytes>=4096 && policyValue.maximumFileBytes<=8ULL*1024*1024*1024,"Invalid assembly limits");
    auto credit=ledger.reserve(65536);std::set<std::string> ids{op.str(),p.recipe().operation().str()};for(const auto &a:s.assets)check(a.id!=op,"Assembly operation reused an asset identity");for(const auto &child:p.children())if(child){check(child->operation()!=op,"Assembly operation reused span identity");ids.insert(child->operation().str());}
    std::uint64_t total=0;unsigned count=0;Json outputList=Json::array();
    for(unsigned side=0;side<2;++side){const auto &v=processing(p,side);const bool needed=v.domain!=CompProcessingDomain::ExistingMedia;check(needed==outputs[side].has_value(),"Assembly output differs from required fragment");outputList.push_back(outputs[side]?Json(outputs[side]->str()):Json(nullptr));
        if(outputs[side]){check(ids.insert(outputs[side]->str()).second && std::none_of(s.assets.begin(),s.assets.end(),[&](const auto &a){return a.id==*outputs[side];}),"Assembly output reused an identity");const auto bytes=std::uint64_t(v.pcm.prefixFrames+v.pcm.currentFrames+v.pcm.suffixFrames)*v.pcm.channels*4;check(bytes<=l.maximumPcmBytes,"Assembly PCM exceeds grant",ErrorCode::ResourceLimit);total+=bytes+1048576;++count;}
        const auto &n=l.nominalPolicies[side];check(n.memoryBytes>=16*1024*1024 && n.memoryBytes<=4096ULL*1024*1024 && n.memoryBytes%(1024*1024)==0 && n.maximumInputFrames>0 && n.maximumInputFrames<=1000000000 && n.maximumOutputBytes>=4096 && n.maximumOutputBytes<=8ULL*1024*1024*1024 && n.maximumSourceBytes>0 && n.maximumSourceBytes<=8ULL*1024*1024*1024 && n.deadlineMilliseconds>=100 && n.deadlineMilliseconds<=3600000,"Invalid trusted original nominal policy");
    }
    check(count && total<=policyValue.maximumFileBytes,"Assembly pair exceeds combined file grant",ErrorCode::ResourceLimit);
    const auto bytes=Json{{"protocol",compAssemblyProtocol},{"operation",op.str()},{"selectionOperation",p.recipe().operation().str()},{"selectionDigest",p.digest()},{"recipeDigest",p.recipe().digest()},{"executorSha256",executable},{"outputs",outputList},{"policy",policy(policyValue)}}.dump();check(bytes.size()<=16384,"Assembly request exceeds bank");
    return {std::move(credit),op,outputs,policyValue,std::move(executable),p.digest(),hash(bytes),bytes};
}
CompAssemblyRequest decodeCompAssemblyRequest(std::string_view bytes,std::string_view expected,const Session &s,const CompHandlePlanContext &ctx,const CompPairSelection &p,ResourceLedger ledger){
    check(bytes.size()<=16384 && sha(expected) && hash(bytes)==expected,"Assembly request differs from trusted digest",ErrorCode::MediaMismatch);auto work=ledger.reserve(2*1024*1024);
    try{const auto j=parse(bytes);check(j.is_object() && j.size()==8,"Unexpected assembly request shape");std::array<std::optional<Id>,2> outputs;check(j.at("outputs").is_array() && j.at("outputs").size()==2,"Invalid assembly outputs");for(unsigned side=0;side<2;++side)if(!j.at("outputs")[side].is_null())outputs[side]=Id(j.at("outputs")[side].get<std::string>());
        auto r=prepareCompAssemblyRequest(s,ctx,p,Id(j.at("operation").get<std::string>()),outputs,j.at("executorSha256").get<std::string>(),policy(j.at("policy")),ledger);check(r.bytes()==bytes,"Assembly request differs from canonical current selection");return r;
    }catch(const Json::exception &){throw ProjectError(ErrorCode::InvalidState,"Invalid assembly request fields");}
}
CompAssemblyReference referenceCompAssembly(const Session &s,const CompHandlePlanContext &ctx,
    const CompPairSelection &p,const CompAssemblyRequest &r,ResourceLedger ledger){
    fresh(s,ctx,p,r);
    const StateBudget budget{ledger.usage().limitBytes};
    const auto payload=sessionPayloadBytes(s,budget);
    PayloadCharge charge("Assembly registration staging",budget.memoryBudgetBytes);
    charge.add(payload,2);charge.add(8192);auto staging=ledger.reserve(charge.bytes());
    auto candidate=s;
    CompAssemblyReference reference{r.operation(),p.recipe().operation(),r.digest(),r.outputs()};
    applySessionEdits(candidate,{AddCompAssembly{reference}},budget);
    return reference;
}
CompAssemblyReference persistCompAssemblyRequest(const std::filesystem::path &root,const Session &s,
    const CompHandlePlanContext &ctx,const CompPairSelection &p,const CompAssemblyRequest &r,
    ResourceLedger ledger,std::stop_token stop){
    rootPath(root);auto poll=polling(root,r,stop,false);poll();
    const auto reference=referenceCompAssembly(s,ctx,p,r,ledger);auto staging=ledger.reserve(65536);
    directory(root/"media");const auto folder=root/"media/comp-assembly-requests";directory(folder);
    const bool newLock=!present(folder/"writer.lock");media_io::JobLease owner(folder,newLock,!newLock);
    check(owner.status()==media_io::LeaseStatus::Held,"Assembly request publication busy",ErrorCode::Io);
    const auto relative="media/comp-assembly-requests/"+r.operation().str()+".json";
    if(present(root/utf8Path(relative))){ApprovedMediaRoot approved(root,ledger,{1});
        check(read(approved,relative,stop,poll)==r.bytes(),"Saved assembly request differs",ErrorCode::MediaMismatch);
    }else{poll();immutable(root/utf8Path(relative),r.bytes());}
    poll();fresh(s,ctx,p,r);return reference;
}
CompAssemblyRequest loadReferencedCompAssemblyRequest(const std::filesystem::path &root,const Session &s,
    const CompHandlePlanContext &ctx,const CompPairSelection &p,const Id &operation,
    ResourceLedger ledger,std::stop_token stop){
    check(!stop.stop_requested(),"Assembly request load canceled",ErrorCode::Canceled);
    const StateBudget budget{ledger.usage().limitBytes};auto validation=ledger.reserve(sessionPayloadBytes(s,budget));validate(s,budget);
    const auto found=std::find_if(s.compAssemblies.begin(),s.compAssemblies.end(),[&](const auto &r){return r.operation==operation;});
    check(found!=s.compAssemblies.end(),"No trusted project assembly reference",ErrorCode::InvalidId);
    check(found->selectionOperation==p.recipe().operation(),"Assembly reference selects a different pair");
    auto staging=ledger.reserve(65536);ApprovedMediaRoot approved(root,ledger,{1});
    const auto poll=[&]{check(!stop.stop_requested(),"Assembly request load canceled",ErrorCode::Canceled);};
    const auto bytes=read(approved,"media/comp-assembly-requests/"+operation.str()+".json",stop,poll);
    auto r=decodeCompAssemblyRequest(bytes,found->requestSha256,s,ctx,p,ledger);
    check(referenceCompAssembly(s,ctx,p,r,ledger)==*found,"Assembly request differs from reserved project identities");
    poll();return r;
}
std::string compAssemblyExecutableSha256(){return media_io::executingImageSha256();}
void publishCompAssembly(const std::filesystem::path &root,const Session &s,const CompHandlePlanContext &ctx,const CompPairSelection &p,const CompAssemblyRequest &r,ResourceLedger ledger,CompAssemblyProgress progress,std::stop_token stop){
    rootPath(root);fresh(s,ctx,p,r);check(compAssemblyExecutableSha256()==r.executorSha256(),"Running assembler differs from original request",ErrorCode::MediaMismatch);auto poll=polling(root,r,stop,true);poll();auto work=ledger.reserve(2*1024*1024);
    verifyHistory(root,s,ctx,p,r,ledger,poll,stop);
    directory(root/"media");directory(root/"media/comp-assemblies");const auto folder=root/utf8Path(jobPath(r));check(std::filesystem::create_directory(folder),"Assembly job already exists",ErrorCode::Io);media_io::flushDirectory(folder.parent_path());media_io::JobLease owner(folder,true);
    immutable(folder/"intent.json",r.bytes());directory(root/"media/comp-results");const auto resultFolder=root/"media/comp-results"/p.recipe().operation().str();directory(resultFolder);const bool newLock=!present(resultFolder/"writer.lock");media_io::JobLease outputOwner(resultFolder,newLock,!newLock);check(outputOwner.status()==media_io::LeaseStatus::Held,"Assembly result publication busy",ErrorCode::Io);
    std::array<std::optional<Asset>,2> assets;Json rows=Json::array();
    for(unsigned side=0;side<2;++side){poll();if(!r.outputs()[side]){rows.push_back(nullptr);continue;}auto a=output(p,r,side);const auto final=root/utf8Path(a.relativePath);check(!present(final),"Assembly output already exists",ErrorCode::Io);const auto partial=final.parent_path()/(a.id.str()+".partial");Wave wave(partial,a);media_io::SampleHash samples;
        const auto report=streamCompPairFragment(root,s,ctx,p,side,[&](std::span<const float> values){poll();const auto n=sf_count_t(values.size()/a.layout.channels);check(sf_writef_float(wave.file,values.data(),n)==n && sf_error(wave.file)==SF_ERR_NO_ERROR,"Assembly WAV write failed",ErrorCode::Io);samples.update(values);},ledger,r.policy().assembly,stop);
        wave.close();poll();if(progress)progress(CompAssemblyStage::FileClosed,side);poll();a.sha256=hashMediaFile(partial,poll);media_io::publishMedia(partial,final);if(progress)progress(CompAssemblyStage::FilePublished,side);poll();rows.push_back(row(a,report.domain,samples.digest(),report.pcm.floatPeak));assets[side]=a;
    }
    (void)verifyFiles(root,s,ctx,p,r,assets,rows,ledger,stop,poll);fresh(s,ctx,p,r);check(compAssemblyExecutableSha256()==r.executorSha256(),"Assembler image changed during publication");poll();if(progress)progress(CompAssemblyStage::BeforeCompletion,2);poll();fresh(s,ctx,p,r);verifyHistory(root,s,ctx,p,r,ledger,poll,stop);
    const auto complete=Json{{"protocol",compAssemblyProtocol},{"requestDigest",r.digest()},{"complete",true},{"outputs",rows},{"durabilityMinimum","file-flushed"}}.dump();immutable(folder/"complete.json",complete);
}
VerifiedCompAssembly detail::CompAssemblyFileVerifier::verify(const std::filesystem::path &root,const Session &s,const CompHandlePlanContext &ctx,const CompPairSelection &p,const CompAssemblyRequest &r,ResourceLedger ledger,std::stop_token stop){
    fresh(s,ctx,p,r);auto poll=polling(root,r,stop,false);poll();auto work=ledger.reserve(2*1024*1024);auto retained=ledger.reserve(65536);media_io::JobLease owner(root/utf8Path(jobPath(r)),false);check(owner.status()==media_io::LeaseStatus::Held,"Assembly writer active or missing");ApprovedMediaRoot approved(root,ledger,{1});const auto intent=read(approved,jobPath(r)+"/intent.json",stop,poll);check(intent==r.bytes(),"Assembly intent changed");const auto bytes=read(approved,jobPath(r)+"/complete.json",stop,poll);
    try{const auto j=parse(bytes);check(j.is_object() && j.size()==5 && j.at("protocol").is_string() && j.at("protocol").get_ref<const std::string &>()==compAssemblyProtocol && j.at("requestDigest")==r.digest() && j.at("complete").is_boolean() && j.at("complete")==true && j.at("durabilityMinimum")=="file-flushed","Invalid assembly completion");const auto &rows=j.at("outputs");check(rows.is_array() && rows.size()==2,"Invalid completion outputs");std::array<std::optional<Asset>,2> assets;
        for(unsigned side=0;side<2;++side){if(!r.outputs()[side]){check(rows[side].is_null(),"Unexpected assembly output");continue;}auto a=output(p,r,side);a.sha256=rows[side].at("sha256").get<std::string>();check(sha(a.sha256),"Invalid assembly output hash");assets[side]=a;}
        const auto compared=verifyFiles(root,s,ctx,p,r,assets,rows,ledger,stop,poll);check(read(approved,jobPath(r)+"/intent.json",stop,poll)==intent && read(approved,jobPath(r)+"/complete.json",stop,poll)==bytes,"Assembly markers changed during verification");fresh(s,ctx,p,r);poll();return {std::move(retained),std::move(assets),r.digest(),compared};
    }catch(const Json::exception &){throw ProjectError(ErrorCode::InvalidState,"Invalid assembly completion fields");}
}
VerifiedCompAssembly verifyCompAssembly(const std::filesystem::path &root,const Session &s,const CompHandlePlanContext &ctx,
    const CompPairSelection &p,const CompAssemblyRequest &r,ResourceLedger ledger,std::stop_token stop){
    fresh(s,ctx,p,r);auto poll=polling(root,r,stop,false);poll();
    verifyHistory(root,s,ctx,p,r,ledger,poll,stop);
    auto result=detail::CompAssemblyFileVerifier::verify(root,s,ctx,p,r,ledger,stop);
    verifyHistory(root,s,ctx,p,r,ledger,poll,stop);fresh(s,ctx,p,r);poll();return result;
}
CompAssemblyRecovery inspectCompAssembly(const std::filesystem::path &root,const Session &s,const CompHandlePlanContext &ctx,const CompPairSelection &p,const CompAssemblyRequest &r,ResourceLedger ledger,std::stop_token stop){
    CompAssemblyRecovery result;try{check(!stop.stop_requested(),"Assembly recovery canceled",ErrorCode::Canceled);fresh(s,ctx,p,r);const auto folder=root/utf8Path(jobPath(r));if(!present(folder))return result;result.cancellationRequested=present(folder/"cancel.request");
        {media_io::JobLease owner(folder,false);if(owner.status()==media_io::LeaseStatus::Busy){result.state=CompAssemblyRecoveryState::Active;return result;}check(owner.status()==media_io::LeaseStatus::Held,"Assembly job lacks ownership");}
        if(present(folder/"intent.json")){auto credit=ledger.reserve(65536);ApprovedMediaRoot approved(root,ledger,{1});auto poll=polling(root,r,stop,false);check(read(approved,jobPath(r)+"/intent.json",stop,poll)==r.bytes(),"Recovery intent differs from original assembly request");}
        if(present(folder/"complete.json")){result.result.emplace(verifyCompAssembly(root,s,ctx,p,r,ledger,stop));result.state=CompAssemblyRecoveryState::Complete;}
        else result.state=result.cancellationRequested?CompAssemblyRecoveryState::Canceled:CompAssemblyRecoveryState::Interrupted;
    }catch(const ProjectError &e){if(e.code()==ErrorCode::Canceled)throw;result.state=CompAssemblyRecoveryState::Invalid;result.error=e.code();}return result;
}
void cancelCompAssembly(const std::filesystem::path &root,const CompAssemblyRequest &r,ResourceLedger ledger){
    rootPath(root);const auto folder=root/utf8Path(jobPath(r));if(!present(folder))return;
    media_io::plainDirectory(folder);auto credit=ledger.reserve(65536);ApprovedMediaRoot approved(root,ledger,{1});
    const auto poll=[]{};check(read(approved,jobPath(r)+"/intent.json",{},poll)==r.bytes(),"Cannot cancel a different assembly intent");
    const auto expected=r.digest()+"\n";const auto relative=jobPath(r)+"/cancel.request";
    if(present(root/utf8Path(relative)))check(read(approved,relative,{},poll)==expected,"Assembly cancellation marker changed");
    else immutable(root/utf8Path(relative),expected);
}
} // namespace soundcurrent::daw
