// SPDX-License-Identifier: GPL-3.0-only
#include <soundcurrent/saved_manifest_source.hpp>
#include <soundcurrent/saved_manifest_stage.hpp>
#include <soundcurrent/saved_manifest_dispatch.hpp>
#include <algorithm>
#include <nlohmann/json.hpp>
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <unistd.h>
#endif
namespace soundcurrent::daw {
namespace {
void require(bool value,const char *message,ErrorCode code=ErrorCode::InvalidState) {
    if(!value)throw ProjectError(code,message);
}
void poll(std::stop_token stop) {require(!stop.stop_requested(),"Restoration audit canceled",ErrorCode::Canceled);}
std::optional<ApprovedMediaFile> optionalFile(const ApprovedMediaRoot &root,std::string_view name,
    std::uint64_t maximum,std::stop_token stop) {
    poll(stop);
    try {return root.open(name,maximum,stop);}
    catch(const ProjectError &e) {if(e.code()!=ErrorCode::MissingMedia)throw;}
    return {};
}
OwnedInspectionProtocol read(ApprovedMediaFile &file,ResourceLedger memory,std::stop_token stop) {
    // Native size already bounded by trusted open policy. Include spare capacity
    // before string allocation; retain actual capacity through its admitted owner.
    const auto count=static_cast<std::size_t>(file.size());
    PayloadCharge charge("Restoration audit encoded bytes",SIZE_MAX);charge.add(count);charge.add(count);charge.add(64);
    auto lease=memory.reserve(charge.bytes());std::string bytes(count,'\0');
    for(std::size_t at=0;at<count;) {
        poll(stop);const auto n=std::min<std::size_t>(65536,count-at);
        file.readAt(at,std::span(bytes.data()+at,n),stop);at+=n;
    }
    poll(stop);return {std::move(lease),std::move(bytes)};
}
}

struct SavedManifestAuditState {
    ResourceLease lease;
    ResourceLedger memory;
    std::filesystem::path path;
    ApprovedMediaRoot root;
    ApprovedDirectoryStamp stamp;
    ApprovedMediaFile manifestFile;
    OwnedInspectionProtocol manifestBytes;
    std::shared_ptr<const Session> manifest;
    std::array<char,64> manifestSha{},canonicalSha{};
    std::optional<ApprovedMediaFile> canonical;
    std::uint64_t maximum;
    std::size_t selected=0;
    SavedManifestAuditState(ResourceLease grant,ResourceLedger resources,const std::filesystem::path &p,
        std::uint64_t mediaMaximum,ProjectBudget budget,std::stop_token stop)
        :lease(std::move(grant)),memory(resources),path(p),root(p,resources,{3}),stamp(root.directoryStamp()),
         manifestFile(root.open("project.json",budget.encodedBytes,stop)),manifestBytes(read(manifestFile,resources,stop)),maximum(mediaMaximum) {}
};
SavedManifestAudit::SavedManifestAudit(std::unique_ptr<SavedManifestAuditState> state):state_(std::move(state)) {}
SavedManifestAudit::SavedManifestAudit(SavedManifestAudit &&) noexcept=default;
SavedManifestAudit::~SavedManifestAudit()=default;
SavedManifestAuditState &SavedManifestAudit::state() const {require(bool(state_),"Retired saved manifest audit");return *state_;}
const Session &SavedManifestAudit::manifest() const {return *state().manifest;}
const Asset &SavedManifestAudit::asset() const {return manifest().assets.at(state().selected);}
std::array<char,64> SavedManifestAudit::manifestSha256() const {return state().manifestSha;}
RestorationMediaObservation SavedManifestAudit::canonical() const {return state().canonical ? RestorationMediaObservation::Verified : RestorationMediaObservation::Missing;}
bool SavedManifestAudit::ownedBy(ResourceLedger memory) const {return memory.owns(state().lease);}
void SavedManifestAudit::reverify(std::stop_token stop) {
    auto &s=state();poll(stop);
    const auto root=[&]{require(s.root.directoryStamp()==s.stamp,"Saved manifest root changed",ErrorCode::MediaMismatch);
        ApprovedMediaRoot named(s.path,s.memory,{1});require(named.directoryStamp()==s.stamp,"Saved manifest named root changed",ErrorCode::MediaMismatch);};
    root();reverifyFiles(stop);root();poll(stop);
}
void SavedManifestAudit::reverifyFiles(std::stop_token stop) {
    auto &s=state();poll(stop);
    s.manifestFile.verifyUnchanged();require(s.manifestFile.digest(stop)==s.manifestSha,"Pinned saved manifest changed",ErrorCode::MediaMismatch);
    {auto named=s.root.open("project.json",s.manifestFile.size(),stop);
     require(named.size()==s.manifestFile.size() && named.digest(stop)==s.manifestSha,"Named saved manifest changed",ErrorCode::MediaMismatch);}
    if(s.canonical){s.canonical->verifyUnchanged();require(s.canonical->digest(stop)==s.canonicalSha,"Pinned canonical changed",ErrorCode::MediaMismatch);}
    auto named=optionalFile(s.root,asset().relativePath,s.maximum,stop);
    require(bool(named)==bool(s.canonical),"Saved canonical visibility changed",ErrorCode::MediaMismatch);
    if(named)require(named->size()==s.canonical->size() && named->digest(stop)==s.canonicalSha,"Named canonical changed",ErrorCode::MediaMismatch);
    poll(stop);
}
SavedManifestAudit inspectSavedManifestAsset(const std::filesystem::path &path,const Id &project,const Id &asset,
    std::uint64_t maximum,ResourceLedger memory,ProjectBudget budget,std::stop_token stop) {
    poll(stop);require(maximum>0 && maximum<=static_cast<std::uint64_t>(INT64_MAX),"Invalid saved manifest media budget",ErrorCode::InvalidParameter);
    require(budget.encodedBytes>0 && budget.encodedBytes<=maxProjectBytes,"Invalid saved manifest budget",ErrorCode::InvalidParameter);
    require(path.native().size()<=4096,"Saved manifest root path limit",ErrorCode::ResourceLimit);
    std::size_t components=0;for(const auto &part:path){(void)part;
        require(++components<=64,"Saved manifest root component limit",ErrorCode::ResourceLimit);}
    auto grant=memory.reserve(sizeof(SavedManifestAuditState)+16384);
    auto s=std::make_unique<SavedManifestAuditState>(std::move(grant),memory,path,maximum,budget,stop);
    {SessionSnapshots snapshots(memory,budget.state);auto stateGrant=snapshots.reserveLoad();auto parser=memory.reserve(budget.parserBytes);
     poll(stop);s->manifest=snapshots.adopt(decodeProject(s->manifestBytes.bytes(),budget),std::move(stateGrant));poll(stop);}
    require(s->manifest->id==project,"Saved manifest project mismatch");
    const auto found=std::find_if(s->manifest->assets.begin(),s->manifest->assets.end(),[&](const Asset &a){return a.id==asset;});
    require(found!=s->manifest->assets.end(),"Saved manifest asset absent");
    require(found->relativePath.size()<=1024,"Saved manifest asset path limit",ErrorCode::ResourceLimit);
    s->selected=static_cast<std::size_t>(found-s->manifest->assets.begin());s->manifestSha=s->manifestFile.digest(stop);
    if(auto file=optionalFile(s->root,found->relativePath,maximum,stop))s->canonical.emplace(std::move(*file));
    if(s->canonical){s->canonicalSha=s->canonical->digest(stop);
        require(std::string_view(s->canonicalSha.data(),64)==found->sha256,"Saved canonical hash mismatch",ErrorCode::MediaMismatch);}
    SavedManifestAudit audit(std::move(s));audit.reverify(stop);return audit;
}
SavedManifestSourceTicket::SavedManifestSourceTicket(SavedManifestAudit &audit,std::string_view leaf,
    SavedRestorationSourceContext context,ResourceLedger memory,std::stop_token stop)
    :lease_(memory.reserve(sizeof(SavedManifestSourceTicket)+4096)),
     asset_(audit.asset().id),frames_(audit.asset().frames),rate_(audit.asset().sampleRate),layout_(audit.asset().layout),context_(context) {
    poll(stop);require(context.request && context.job,"Invalid saved manifest checker owner");
    require(!leaf.empty() && leaf.size()<=1024 && validUtf8(leaf) && leaf.find('\0')==leaf.npos,"Invalid saved manifest source name",ErrorCode::InvalidParameter);
    require(audit.ownedBy(memory),"Saved manifest source scope mismatch");audit.reverify(stop);const auto &s=audit.state();
    const auto &hash=audit.asset().sha256;require(hash.size()==sourceSha_.size(),"Invalid saved manifest asset hash");
    std::copy(hash.begin(),hash.end(),sourceSha_.begin());
    root_=s.stamp;manifestSha_=s.manifestSha;canonicalSha_=s.canonicalSha;canonicalPresent_=bool(s.canonical);
    canonicalBytes_=s.canonical ? s.canonical->size() : 0;selectedLeaf_=leaf;poll(stop);
}
void SavedManifestSourceTicket::verify(SavedManifestAudit &audit,const WaveCheckReport &report,
    SavedRestorationSourceContext context,std::size_t pid,ResourceLedger memory,std::stop_token stop) const {
    poll(stop);require(memory.owns(lease_) && audit.ownedBy(memory),"Saved manifest source scope mismatch");
    require(report.ownedBy(memory) && pid && report.workerPid()==pid,"Saved manifest report scope or PID mismatch");
    require(context==context_ && report.relative()==selectedLeaf_,"Stale saved manifest source owner");audit.reverify(stop);
    const auto &s=audit.state();require(s.stamp==root_ && s.manifestSha==manifestSha_ && bool(s.canonical)==canonicalPresent_ &&
        s.canonicalSha==canonicalSha_ && (s.canonical ? s.canonical->size() : 0)==canonicalBytes_,"Original saved manifest fingerprint changed",ErrorCode::MediaMismatch);
    const auto &current=audit.asset();const auto &audio=report.audio();
    require(current.id==asset_ && current.frames==frames_ && current.sampleRate==rate_ && current.layout==layout_ &&
        current.sha256==std::string_view(sourceSha_.data(),sourceSha_.size()),"Saved manifest asset changed",ErrorCode::MediaMismatch);
    require(audio.frames==static_cast<std::uint64_t>(frames_) && audio.rate==rate_ && audio.channels==layout_.channels &&
        audio.sourceSha256==sourceSha_,"Checked source differs from saved manifest asset",ErrorCode::MediaMismatch);poll(stop);
}
void SavedManifestSourceTicket::verifyFile(SavedManifestAudit &audit,const WaveCheckReport &report,
    SavedRestorationSourceContext context,std::size_t pid,ApprovedMediaFile &source,ResourceLedger memory,std::stop_token stop,
    const std::function<void()> &beforeChunk) const {
    poll(stop);require(source.ownedBy(memory),"Saved manifest source file scope mismatch");verify(audit,report,context,pid,memory,stop);
    const auto &audio=report.audio();require(source.size()==audio.sourceBytes,"Saved manifest source extent mismatch",ErrorCode::MediaMismatch);
    source.verifyUnchanged();require(source.digest(stop,beforeChunk)==audio.sourceSha256,"Saved manifest source bytes mismatch",ErrorCode::MediaMismatch);
    source.verifyUnchanged();verify(audit,report,context,pid,memory,stop);source.verifyUnchanged();poll(stop);
}
namespace {
std::string manifestControlHex(std::string_view bytes) {
    constexpr char digits[]="0123456789abcdef";std::string out;out.reserve(bytes.size()*2);
    for(unsigned char c:bytes){out+=digits[c>>4];out+=digits[c&15];}return out;
}
std::string manifestControlUnhex(const nlohmann::json &value,std::size_t maximum) {
    require(value.is_string(),"Invalid manifest control hex type");const auto &text=value.get_ref<const std::string &>();
    require(text.size()<=maximum*2 && text.size()%2==0,"Manifest control hex limit");
    const auto digit=[](char c)->unsigned {
        if(c>='0'&&c<='9')return unsigned(c-'0');
        if(c>='a'&&c<='f')return unsigned(c-'a'+10);
        throw ProjectError(ErrorCode::InvalidState,"Invalid manifest control hex");
    };
    std::string out;out.reserve(text.size()/2);
    for(std::size_t n=0;n<text.size();n+=2)out+=char((digit(text[n])<<4)|digit(text[n+1]));return out;
}
}
std::array<char,193> SavedManifestSourceTicket::fingerprint() const {
    std::array<char,193> out{};std::size_t at=0;
    const auto word=[&](std::uint64_t value){for(unsigned n=0;n<8;++n)out[at++]=char((value>>(n*8))&255);};
    for(auto value:root_.identity)word(value);
    for(char c:manifestSha_)out[at++]=c;
    for(char c:canonicalSha_)out[at++]=c;
    out[at++]=char(canonicalPresent_);word(canonicalBytes_);return out;
}
OwnedInspectionProtocol SavedManifestSourceTicket::copyControlProtocol(SavedManifestAudit &audit,const WaveCheckReport &report,
    SavedRestorationSourceContext current,std::size_t pid,ResourceLedger memory,std::stop_token stop) const {
    verify(audit,report,current,pid,memory,stop);
    require(report.audio().sourceBytes<=audit.state().maximum,"Manifest handoff extent exceeds native policy",ErrorCode::ResourceLimit);
    auto parser=memory.reserve(savedManifestSourceControlMaximumBytes*32);
    auto output=memory.reserve(savedManifestSourceControlMaximumBytes+1024);
    const auto original=fingerprint();
    auto bytes=nlohmann::json({{"schema","sc-saved-manifest-source-control-v1"},
        {"project",audit.manifest().id.str()},{"asset",asset_.str()},
        {"projectEpoch",context_.projectEpoch},{"modelRevision",context_.modelRevision},{"request",context_.request},{"job",context_.job},
        {"fingerprintHex",manifestControlHex(std::string_view(original.data(),original.size()))},
        {"selectedLeafHex",manifestControlHex(selectedLeaf_)},{"sourceSha256",std::string(sourceSha_.data(),sourceSha_.size())},
        {"checkedSourceBytes",report.audio().sourceBytes},{"frames",static_cast<std::uint64_t>(frames_)},{"rateHz",rate_},
        {"channels",layout_.channels},{"layoutKind",static_cast<unsigned>(layout_.kind)},{"workerPid",static_cast<std::uint64_t>(pid)}}).dump();
    require(bytes.size()<=savedManifestSourceControlMaximumBytes,"Manifest source handoff exceeds bank");
    verify(audit,report,current,pid,memory,stop);return {std::move(output),std::move(bytes)};
}
SavedManifestSourceTicket SavedManifestSourceTicket::fromControlProtocol(std::string_view bytes,SavedManifestAudit &audit,
    const WaveCheckReport &report,SavedRestorationSourceContext current,std::size_t pid,ResourceLedger memory,std::stop_token stop) {
    poll(stop);require(!bytes.empty() && bytes.size()<=savedManifestSourceControlMaximumBytes,"Manifest source handoff exceeds bank");
    for(unsigned char c:bytes)require(c && c<128,"Manifest source handoff must be ASCII JSON");
    require(audit.ownedBy(memory) && report.ownedBy(memory) && pid && report.workerPid()==pid,"Manifest source handoff scope or PID mismatch");
    auto parser=memory.reserve(savedManifestSourceControlMaximumBytes*32);
    constexpr std::array<std::string_view,16> fields{"schema","project","asset","projectEpoch","modelRevision","request","job",
        "fingerprintHex","selectedLeafHex","sourceSha256","checkedSourceBytes","frames","rateHz","channels","layoutKind","workerPid"};
    std::array<bool,fields.size()> seen{};
    try {
        const auto callback=[&](int depth,nlohmann::json::parse_event_t event,nlohmann::json &value) {
            poll(stop);require(depth<=1 && event!=nlohmann::json::parse_event_t::array_start,"Nested manifest source handoff refused");
            if(event==nlohmann::json::parse_event_t::key){const auto &key=value.get_ref<const std::string &>();
                const auto found=std::find(fields.begin(),fields.end(),key);require(found!=fields.end(),"Unknown manifest source handoff key");
                const auto n=static_cast<std::size_t>(found-fields.begin());require(!seen[n],"Duplicate manifest source handoff key");seen[n]=true;}
            return true;
        };
        const auto j=nlohmann::json::parse(bytes.begin(),bytes.end(),callback);
        require(j.is_object() && std::all_of(seen.begin(),seen.end(),[](bool present){return present;}) &&
            j.at("schema")=="sc-saved-manifest-source-control-v1","Incomplete manifest source handoff");
        const auto integer=[&](const char *key){const auto &v=j.at(key);require(v.is_number_unsigned(),"Invalid manifest source handoff integer");return v.get<std::uint64_t>();};
        require((SavedRestorationSourceContext{integer("projectEpoch"),integer("modelRevision"),integer("request"),integer("job")}==current),
            "Stale manifest source handoff context");
        require(j.at("project").is_string() && j.at("project").get_ref<const std::string &>()==audit.manifest().id.str() &&
            j.at("asset").is_string() && j.at("asset").get_ref<const std::string &>()==audit.asset().id.str(),"Manifest source handoff identity mismatch");
        const auto leaf=manifestControlUnhex(j.at("selectedLeafHex"),1024);
        const auto encodedFingerprint=manifestControlUnhex(j.at("fingerprintHex"),193);
        require(encodedFingerprint.size()==193,"Invalid manifest source fingerprint extent");
        SavedManifestSourceTicket fresh(audit,leaf,current,memory,stop);const auto original=fresh.fingerprint();
        require(std::equal(original.begin(),original.end(),encodedFingerprint.begin()),"Original manifest source fingerprint changed",ErrorCode::MediaMismatch);
        require(j.at("sourceSha256").is_string() && j.at("sourceSha256").get_ref<const std::string &>()==std::string_view(fresh.sourceSha_.data(),64) &&
            integer("checkedSourceBytes")==report.audio().sourceBytes && integer("checkedSourceBytes")<=audit.state().maximum &&
            integer("frames")==static_cast<std::uint64_t>(fresh.frames_) && integer("rateHz")==fresh.rate_ &&
            integer("channels")==fresh.layout_.channels && integer("layoutKind")==static_cast<unsigned>(fresh.layout_.kind) &&
            integer("workerPid")==pid,"Manifest source handoff differs from original checked asset");
        fresh.verify(audit,report,current,pid,memory,stop);return fresh;
    }catch(const nlohmann::json::exception &){throw ProjectError(ErrorCode::InvalidState,"Malformed manifest source handoff");}
}
struct SavedManifestStageState {
    ResourceLease lease;
    ResourceLedger memory;
    SavedManifestAudit audit;
    SavedRestorationRecord record;
    ApprovedMediaFile source;
    StagedMedia stage;
    SavedManifestStageState(ResourceLease grant,ResourceLedger resources,SavedManifestAudit original,
        SavedRestorationRecord planned,ApprovedMediaFile selected,StagedMedia prepared)
        :lease(std::move(grant)),memory(resources),audit(std::move(original)),record(std::move(planned)),
         source(std::move(selected)),stage(std::move(prepared)) {}
};
SavedManifestStage::SavedManifestStage(std::unique_ptr<SavedManifestStageState> state):state_(std::move(state)) {}
SavedManifestStage::SavedManifestStage(SavedManifestStage &&) noexcept=default;
SavedManifestStage::~SavedManifestStage()=default;
const SavedRestorationRecord &SavedManifestStage::record() const {
    require(bool(state_),"Retired manifest stage");return state_->record;
}
const std::string &SavedManifestStage::relativePath() const {record();return state_->stage.relativePath();}
SavedManifestStage SavedManifestStage::prepare(ApprovedMediaFile source,const std::filesystem::path &root,SavedManifestAudit audit,
    const SavedManifestSourceTicket &ticket,const WaveCheckReport &report,std::size_t checkerPid,
    SavedRestorationRecordContext current,Id operation,std::uint64_t maximum,WaveValidationLimits limits,
    std::stop_token stop,const StageObserver &observer) {
    poll(stop);const auto memory=source.resourceLedger();
    require(audit.ownedBy(memory),"Manifest staging audit scope mismatch");
#ifdef _WIN32
    const auto actualPid=static_cast<std::uint64_t>(GetCurrentProcessId());
#else
    const auto actualPid=static_cast<std::uint64_t>(getpid());
#endif
    require(current.workerPid==actualPid,"Manifest staging writer is not this native process");
    require(maximum>0 && maximum<=audit.state().maximum && source.size()<=maximum,
        "Invalid manifest staging source budget",ErrorCode::ResourceLimit);
    auto grant=memory.reserve(sizeof(SavedManifestStageState)+4096);
    ticket.verifyFile(audit,report,current.source,checkerPid,source,memory,stop);
    require(audit.canonical()==RestorationMediaObservation::Missing,"Manifest staging needs missing canonical media");
    const auto &asset=audit.asset();
    const auto checked=verifyAssetReplacement(source,asset,limits,stop);
    require(checked.sourceBytes==report.audio().sourceBytes && checked.sourceSha256==report.audio().sourceSha256,
        "Fresh manifest decode differs from actual checker",ErrorCode::MediaMismatch);
    SavedRestorationRecordData data{operation,audit.manifest().id,asset.id};data.context=current;data.relativePath=asset.relativePath;
    data.content={checked.sourceBytes,checked.sourceSha256};data.frames=asset.frames;data.rate=asset.sampleRate;data.layout=asset.layout;
    auto record=SavedRestorationRecord::planned(data,memory,stop);
    ticket.verifyFile(audit,report,current.source,checkerPid,source,memory,stop);
    auto intent=encodeSavedRestorationRecord(record,SavedRestorationPhase::Planned,memory,stop);
    auto stage=StagedMedia::prepareSavedRestorationStage(source,root,data.content,maximum,std::move(operation),intent,
        audit.state().stamp,stop,observer);
    SavedManifestStage result(std::make_unique<SavedManifestStageState>(std::move(grant),memory,std::move(audit),
        std::move(record),std::move(source),std::move(stage)));
    result.reverify(stop);return result;
}
void SavedManifestStage::reverify(std::stop_token stop) {
    record();auto &s=*state_;poll(stop);const auto &d=s.record.data();const auto &a=s.audit.asset();
    require(s.record.ownedBy(s.memory) && s.audit.ownedBy(s.memory) && s.source.ownedBy(s.memory),"Manifest stage scope mismatch");
    require(d.project==s.audit.manifest().id && d.asset==a.id && d.relativePath==a.relativePath &&
        std::string_view(d.content.sha256.data(),d.content.sha256.size())==a.sha256 &&
        d.frames==a.frames && d.rate==a.sampleRate && d.layout==a.layout &&
        s.audit.canonical()==RestorationMediaObservation::Missing,"Manifest stage changed original asset");
    auto intent=encodeSavedRestorationRecord(s.record,SavedRestorationPhase::Planned,s.memory,stop);
    const auto verify=[&] {
        const auto held=s.audit.state().root.directoryStamp();
        ApprovedMediaRoot named(s.audit.state().path,s.memory,{1});
        require(named.directoryStamp()==held,"Manifest stage named root changed",ErrorCode::MediaMismatch);
        s.stage.verifyRestorationParentTransition(s.audit.state().stamp,held,d.content,intent.bytes(),s.memory,stop);
    };
    verify();s.audit.reverifyFiles(stop);s.source.verifyUnchanged();
    require(s.source.size()==d.content.bytes && s.source.digest(stop)==d.content.sha256,
        "Manifest stage source changed",ErrorCode::MediaMismatch);
    verify();poll(stop);
}

namespace {
void manifestDispatchOwner(const SavedRestorationOwnerView &owner,std::stop_token stop) {
    poll(stop);
    require(!owner.acceptedProject && !owner.closing && !owner.ioActive && !owner.publicationAmbiguous,
        "Manifest dispatch owner is not available");
    require(owner.root.is_absolute() && !owner.root.empty() && owner.root.native().size()<=4096,
        "Manifest dispatch root is not explicit");
    require(owner.context.source.request && owner.context.source.job && owner.context.preparationJob &&
        owner.context.request && owner.context.job,"Manifest dispatch context is incomplete");
}
}
SavedManifestDispatchTicket::SavedManifestDispatchTicket(ResourceLease grant,OwnedInspectionProtocol control,
    const SavedRestorationOwnerView &owner,const SavedManifestAudit &audit,const SavedManifestSourceTicket &source,
    ApprovedMediaFile &file,const WaveCheckReport &report,std::size_t pid,Id operation)
    :lease_(std::move(grant)),originalControl_(std::move(control)),root_(owner.root),context_(owner.context),
     originalRoot_(audit.state().stamp),originalSource_(file.identityStamp()),originalFingerprint_(source.fingerprint()),
     identity_(std::move(operation),audit.manifest().id,audit.asset().id),checkerPid_(pid) {
    const auto &asset=audit.asset();const auto &audio=report.audio();
    identity_.context={context_.source,context_.preparationJob,context_.request,context_.job,0};
    identity_.relativePath=asset.relativePath;identity_.content={audio.sourceBytes,audio.sourceSha256};
    identity_.frames=asset.frames;identity_.rate=asset.sampleRate;identity_.layout=asset.layout;
}
SavedManifestDispatchTicket SavedManifestDispatchTicket::bind(const SavedRestorationOwnerView &owner,
    SavedManifestAudit &audit,const SavedManifestSourceTicket &source,ApprovedMediaFile &file,std::string_view originalControl,
    const WaveCheckReport &report,std::size_t pid,Id operation,ResourceLedger memory,std::stop_token stop) {
    manifestDispatchOwner(owner,stop);
    require(!originalControl.empty() && originalControl.size()<=savedManifestSourceControlMaximumBytes,
        "Manifest original control extent");
    PayloadCharge metadata("Manifest dispatch metadata",SIZE_MAX);
    metadata.add(sizeof(SavedManifestDispatchTicket)+4096);
    metadata.add(owner.root.native().size(),sizeof(std::filesystem::path::value_type)*4);
    metadata.add(audit.asset().relativePath.size(),4);
    auto grant=memory.reserve(metadata.bytes());
    source.verifyFile(audit,report,owner.context.source,pid,file,memory,stop);
    require(owner.root==audit.state().path,"Manifest dispatch root differs from original audit",ErrorCode::MediaMismatch);
    require(audit.canonical()==RestorationMediaObservation::Missing,"Manifest dispatch needs missing media");
    auto control=source.copyControlProtocol(audit,report,owner.context.source,pid,memory,stop);
    require(control.bytes()==originalControl,"Manifest dispatch differs from original private control",ErrorCode::MediaMismatch);
    SavedManifestDispatchTicket result(std::move(grant),std::move(control),owner,audit,source,file,report,pid,std::move(operation));
    result.verifyBeforeLaunch(owner,audit,source,file,report,pid,memory,stop);return result;
}
void SavedManifestDispatchTicket::verifyOwner(const SavedRestorationOwnerView &owner,ResourceLedger memory,
    std::stop_token stop) const {
    manifestDispatchOwner(owner,stop);
    require(memory.owns(lease_) && originalControl_.ownedBy(memory),"Manifest dispatch ledger mismatch");
    require(owner.root==root_ && owner.context==context_,"Manifest dispatch current owner changed");
}
void SavedManifestDispatchTicket::verifyBeforeLaunch(const SavedRestorationOwnerView &owner,SavedManifestAudit &audit,
    const SavedManifestSourceTicket &source,ApprovedMediaFile &file,const WaveCheckReport &report,std::size_t pid,
    ResourceLedger memory,std::stop_token stop) const {
    verifyOwner(owner,memory,stop);require(pid==checkerPid_,"Manifest dispatch checker changed");
    source.verifyFile(audit,report,context_.source,pid,file,memory,stop);
    require(file.identityStamp()==originalSource_ && root_==audit.state().path,"Manifest dispatch original root/source changed",ErrorCode::MediaMismatch);
    require(audit.state().stamp==originalRoot_ && source.fingerprint()==originalFingerprint_,
        "Manifest original dispatch fingerprint changed",ErrorCode::MediaMismatch);
    auto control=source.copyControlProtocol(audit,report,context_.source,pid,memory,stop);
    require(control.bytes()==originalControl_.bytes(),"Manifest original dispatch control changed",ErrorCode::MediaMismatch);
    // A plain fresh UUID is a caller choice, not proof that the namespace is free.
    // This read-only absence observation is bracketed by the ORIGINAL native and
    // named root stamp; symlink_status never follows the final operation entry.
    std::error_code error;const auto entry=std::filesystem::symlink_status(root_/identity_.operation.str(),error);
    require((!error || error==std::errc::no_such_file_or_directory) && entry.type()==std::filesystem::file_type::not_found,
        "Manifest dispatch operation already exists or is uninspectable",ErrorCode::MediaMismatch);
    source.verifyFile(audit,report,context_.source,pid,file,memory,stop);
}
void SavedManifestDispatchTicket::inspectStaged(const SavedRestorationOwnerView &owner,SavedManifestAudit &audit,
    const SavedManifestSourceTicket &source,ApprovedMediaFile &originalSource,const WaveCheckReport &report,
    std::size_t pid,std::size_t workerPid,const SavedRestorationRecord &record,ResourceLedger memory,
    std::stop_token stop,const std::function<void()> &beforeChunk) const {
    verifyOwner(owner,memory,stop);
    require(pid==checkerPid_ && pid && workerPid && workerPid<=UINT32_MAX && report.workerPid()==pid &&
        report.ownedBy(memory) && audit.ownedBy(memory) && originalSource.ownedBy(memory) && memory.owns(source.lease_),
        "Manifest stage observation scope or PID changed");
    require(source.fingerprint()==originalFingerprint_ && source.context_==context_.source &&
        source.asset_==identity_.asset && source.sourceSha_==identity_.content.sha256 && source.frames_==identity_.frames &&
        source.rate_==identity_.rate && source.layout_==identity_.layout && originalSource.identityStamp()==originalSource_ &&
        source.selectedLeaf_==report.relative() && audit.state().path==root_ && audit.state().stamp==originalRoot_ &&
        audit.state().manifestSha==source.manifestSha_ && bool(audit.state().canonical)==source.canonicalPresent_ &&
        audit.state().canonicalSha==source.canonicalSha_ &&
        (audit.state().canonical ? audit.state().canonical->size() : 0)==source.canonicalBytes_ &&
        audit.manifest().id==identity_.project && audit.asset().id==identity_.asset &&
        audit.canonical()==RestorationMediaObservation::Missing,
        "Manifest observation lost original preparation",ErrorCode::MediaMismatch);
    const auto &audio=report.audio();
    require(audio.sourceBytes==identity_.content.bytes && audio.sourceSha256==identity_.content.sha256 &&
        audio.frames==static_cast<std::uint64_t>(identity_.frames) && audio.rate==identity_.rate &&
        audio.channels==identity_.layout.channels,
        "Manifest observation report changed",ErrorCode::MediaMismatch);
    const SavedRestorationRecordContext current{context_.source,context_.preparationJob,context_.request,context_.job,workerPid};
    verifySavedRestorationRecord(record,identity_.project,audit.asset(),identity_.content,current,identity_.operation,
        SavedRestorationPhase::Planned,memory,stop);
    const auto observedRoot=audit.state().root.directoryStamp();
    require(observedRoot.identity[0]==originalRoot_.identity[0] && observedRoot.identity[1]==originalRoot_.identity[1],
        "Manifest observation original native root changed",ErrorCode::MediaMismatch);
    const auto operationRoot=root_/identity_.operation.str();ApprovedMediaRoot stage(operationRoot,memory,{2});
    const auto observedStage=stage.directoryStamp();
    const auto root=[&]{
        poll(stop);require(audit.state().root.directoryStamp()==observedRoot && stage.directoryStamp()==observedStage,
            "Manifest observation namespace changed during inspection",ErrorCode::MediaMismatch);
        ApprovedMediaRoot named(root_,memory,{1}),namedStage(operationRoot,memory,{1});
        require(named.directoryStamp()==observedRoot && namedStage.directoryStamp()==observedStage,
            "Manifest observation named namespace changed",ErrorCode::MediaMismatch);
    };
    // This narrow friend can recheck original pinned/named files after the child
    // changed the directory. It never changes audit.state().stamp or calls a
    // publication API; ordinary audit/source verification remains strict.
    root();audit.reverifyFiles(stop);root();
    originalSource.verifyUnchanged();
    require(originalSource.size()==identity_.content.bytes && originalSource.digest(stop,beforeChunk)==identity_.content.sha256,
        "Manifest observation original source changed",ErrorCode::MediaMismatch);originalSource.verifyUnchanged();
    auto staged=stage.open("media.partial",identity_.content.bytes,stop);
    auto intentFile=stage.open("intent.json",savedRestorationRecordMaximumBytes,stop);
    require(staged.size()==identity_.content.bytes && staged.digest(stop,beforeChunk)==identity_.content.sha256,
        "Manifest observed stage bytes differ",ErrorCode::MediaMismatch);
    auto wire=read(intentFile,memory,stop);auto intent=decodeSavedRestorationRecord(wire.bytes(),memory,stop);
    verifySavedRestorationRecord(intent,identity_.project,audit.asset(),identity_.content,current,identity_.operation,
        SavedRestorationPhase::Planned,memory,stop);
    const auto intentSha=intentFile.digest(stop,beforeChunk);
    // Both stage-file pins remain live while names, original files and directory
    // change stamps are checked again. An exited record is never a writer proof.
    root();audit.reverifyFiles(stop);
    {auto named=audit.state().root.open(identity_.operation.str()+"/media.partial",identity_.content.bytes,stop);
     require(named.size()==staged.size() && named.digest(stop,beforeChunk)==identity_.content.sha256,
        "Manifest observed staged name changed",ErrorCode::MediaMismatch);named.verifyUnchanged();}
    {auto named=audit.state().root.open(identity_.operation.str()+"/intent.json",savedRestorationRecordMaximumBytes,stop);
     require(named.size()==intentFile.size() && named.digest(stop,beforeChunk)==intentSha,
        "Manifest observed intent name changed",ErrorCode::MediaMismatch);named.verifyUnchanged();}
    require(!optionalFile(audit.state().root,identity_.operation.str()+"/receipt.json",savedRestorationRecordMaximumBytes,stop) &&
        !optionalFile(audit.state().root,identity_.operation.str()+"/receipt.partial",savedRestorationRecordMaximumBytes,stop),
        "Manifest planned observation has a publication receipt",ErrorCode::MediaMismatch);
    originalSource.verifyUnchanged();staged.verifyUnchanged();intentFile.verifyUnchanged();
    root();audit.reverifyFiles(stop);staged.verifyUnchanged();intentFile.verifyUnchanged();root();poll(stop);
}
} // namespace soundcurrent::daw
