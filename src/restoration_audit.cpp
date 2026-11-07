// SPDX-License-Identifier: GPL-3.0-only
#include <soundcurrent/restoration_audit.hpp>
#include <soundcurrent/saved_restoration_dispatch.hpp>
#include <algorithm>
#include <nlohmann/json.hpp>
#include <soundcurrent/saved_restoration_stage.hpp>
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
bool sameAsset(const Asset &a,const Asset &b) {
    return a.id==b.id && a.relativePath==b.relativePath && a.sha256==b.sha256 && a.frames==b.frames &&
        a.sampleRate==b.sampleRate && a.layout==b.layout && a.nativeEvidence==b.nativeEvidence;
}
void binds(const AssetRestorationRecordData &d,const Asset &a,const Id &operation,AssetRestorationPhase phase) {
    require(d.operation==operation && d.phase==phase && d.asset==a.id && d.relativePath==a.relativePath &&
        std::string_view(d.content.sha256.data(),64)==a.sha256 && d.frames==a.frames && d.rate==a.sampleRate &&
        d.layout==a.layout,"Restoration audit record identity mismatch");
}
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
struct RestorationAuditState {
    ResourceLease lease; // Bytes/native owners retire before this state grant.
    ResourceLedger memory;
    ApprovedMediaRoot root;
    ApprovedDirectoryStamp stamp;
    std::array<std::string,6> names;
    std::array<std::uint64_t,6> maximum{};
    std::array<std::optional<ApprovedMediaFile>,6> files;
    std::array<std::array<char,64>,6> hashes{};
    std::optional<OwnedInspectionProtocol> manifestBytes;
    std::shared_ptr<const Session> manifest;
    std::optional<AssetRestorationRecord> intent;
    RestorationReceiptObservation receipt=RestorationReceiptObservation::Missing;
    RestorationAuditState(ResourceLease grant,ResourceLedger resources,const std::filesystem::path &path)
        :lease(std::move(grant)),memory(resources),root(path,resources,{8}),stamp(root.directoryStamp()) {}
};
RestorationAudit::RestorationAudit(std::unique_ptr<RestorationAuditState> state):state_(std::move(state)) {}
RestorationAudit::RestorationAudit(RestorationAudit &&) noexcept=default;
RestorationAudit::~RestorationAudit()=default;
RestorationAuditState &RestorationAudit::state() const {require(bool(state_),"Retired restoration audit");return *state_;}
const Session &RestorationAudit::manifest() const {return *state().manifest;}
std::string_view RestorationAudit::manifestBytes() const {return state().manifestBytes->bytes();}
std::array<char,64> RestorationAudit::manifestSha256() const {return state().hashes[0];}
const AssetRestorationRecordData &RestorationAudit::intent() const {return state().intent->data();}
RestorationMediaObservation RestorationAudit::canonical() const {
    return state().files[5] ? RestorationMediaObservation::Verified : RestorationMediaObservation::Missing;
}
RestorationMediaObservation RestorationAudit::staged() const {
    return state().files[4] ? RestorationMediaObservation::Verified : RestorationMediaObservation::Missing;
}
RestorationReceiptObservation RestorationAudit::receipt() const {return state().receipt;}
bool RestorationAudit::ownedBy(ResourceLedger memory) const {return memory.owns(state().lease);}
void RestorationAudit::reverify(std::stop_token stop) {
    auto &s=state();poll(stop);
    require(s.root.directoryStamp()==s.stamp,"Restoration audit directory changed",ErrorCode::MediaMismatch);
    reverifyFiles(stop);
    require(s.root.directoryStamp()==s.stamp,"Restoration audit directory changed",ErrorCode::MediaMismatch);poll(stop);
}
void RestorationAudit::reverifyFiles(std::stop_token stop) {
    auto &s=state();poll(stop);
    for(std::size_t i=0;i<s.files.size();++i) {
        poll(stop);
        if(s.files[i]) {
            s.files[i]->verifyUnchanged();
            require(s.files[i]->digest(stop)==s.hashes[i],"Restoration audit pinned bytes changed",ErrorCode::MediaMismatch);
        }
        auto now=optionalFile(s.root,s.names[i],s.maximum[i],stop);
        require(bool(now)==bool(s.files[i]),"Restoration audit name visibility changed",ErrorCode::MediaMismatch);
        if(now)require(now->size()==s.files[i]->size() && now->digest(stop)==s.hashes[i],
            "Restoration audit named bytes changed",ErrorCode::MediaMismatch);
    }
}
void RestorationAudit::reverifyAfterOwnedStaging(const PreparedAssetRestoration &prepared,std::stop_token stop) {
    auto &s=state();poll(stop);const auto &record=prepared.record();
    require(record.ownedBy(s.memory),"Owned restoration stage admission scope mismatch");
    const auto &fresh=record.data();const auto &original=intent();
    require(fresh.operation!=original.operation && fresh.asset==original.asset && fresh.relativePath==original.relativePath &&
        fresh.content.bytes==original.content.bytes && fresh.content.sha256==original.content.sha256 &&
        fresh.frames==original.frames && fresh.rate==original.rate && fresh.layout==original.layout,
        "Owned restoration stage differs from original saved asset",ErrorCode::MediaMismatch);
    auto planned=encodeAssetRestorationRecord(record,AssetRestorationPhase::Planned,s.memory,stop);
    const auto verify=[&]{prepared.stageForAudit().verifyRestorationParentTransition(s.stamp,s.root.directoryStamp(),
        original.content,planned.bytes(),s.memory,stop);};
    verify();reverifyFiles(stop);verify();poll(stop);
    // The original stamp/fingerprint stays unchanged. A generic reverify must
    // still reject the root mutation, even after this typed proof succeeds.
}
namespace {
std::unique_ptr<RestorationAuditState> inspectState(const std::filesystem::path &root,const Id &project,const Id &asset,
    const Asset *bound,const Id &operation,std::uint64_t maximum,ResourceLedger memory,ProjectBudget budget,std::stop_token stop) {
    poll(stop);require(maximum>0 && maximum<=static_cast<std::uint64_t>(INT64_MAX),"Invalid restoration audit media budget",ErrorCode::InvalidParameter);
    require(budget.encodedBytes>0 && budget.encodedBytes<=maxProjectBytes,"Invalid restoration audit manifest budget",ErrorCode::InvalidParameter);
    // Bound copied name banks before allocation; all record/path validation is
    // still performed by native opens and strict decoders below.
    if(bound)require(bound->relativePath.size()<=1024,"Restoration audit path limit",ErrorCode::ResourceLimit);
    auto grant=memory.reserve(sizeof(RestorationAuditState)+8192);
    auto s=std::make_unique<RestorationAuditState>(std::move(grant),memory,root);
    const auto prefix=operation.str()+"/";
    s->names={"project.json",prefix+"intent.json",prefix+"receipt.partial",prefix+"receipt.json",prefix+"media.partial",""};
    s->maximum={budget.encodedBytes,assetRestorationRecordMaximumBytes,assetRestorationRecordMaximumBytes,
        assetRestorationRecordMaximumBytes,maximum,maximum};
    s->files[0].emplace(s->root.open(s->names[0],s->maximum[0],stop));
    s->manifestBytes.emplace(read(*s->files[0],memory,stop));
    {
        SessionSnapshots snapshots(memory,budget.state);auto stateGrant=snapshots.reserveLoad();
        auto parser=memory.reserve(budget.parserBytes);poll(stop);
        s->manifest=snapshots.adopt(decodeProject(s->manifestBytes->bytes(),budget),std::move(stateGrant));poll(stop);
    }
    require(s->manifest->id==project,"Restoration audit project identity mismatch");
    const auto found=std::find_if(s->manifest->assets.begin(),s->manifest->assets.end(),[&](const Asset &a){return a.id==asset;});
    require(found!=s->manifest->assets.end(),"Restoration audit saved asset is absent");
    if(bound)require(sameAsset(*found,*bound),"Restoration audit saved asset mismatch");
    const auto &expected=*found; // Owned by the admitted passive manifest, never the intent record.
    require(expected.relativePath.size()<=1024,"Restoration audit path limit",ErrorCode::ResourceLimit);
    s->names[5]=expected.relativePath;
    s->files[1].emplace(s->root.open(s->names[1],s->maximum[1],stop));
    {
        auto raw=read(*s->files[1],memory,stop);s->intent.emplace(decodeAssetRestorationRecord(raw.bytes(),memory,stop));
        binds(s->intent->data(),expected,operation,AssetRestorationPhase::Planned);
    }
    const auto &intent=s->intent->data();
    require(intent.content.bytes<=maximum,"Restoration audit extent exceeds policy",ErrorCode::ResourceLimit);
    for(std::size_t i=2;i<4;++i) {
        auto file=optionalFile(s->root,s->names[i],s->maximum[i],stop);
        if(!file)continue;
        s->files[i].emplace(std::move(*file));auto raw=read(*s->files[i],memory,stop);
        auto record=decodeAssetRestorationRecord(raw.bytes(),memory,stop);binds(record.data(),expected,operation,AssetRestorationPhase::Published);
        // Historical consistency only, explicitly NOT live-owner authorization.
        require(record.data().context==intent.context && record.data().content.bytes==intent.content.bytes,
            "Restoration audit receipt disagrees with intent");
        require(s->receipt==RestorationReceiptObservation::Missing,"Conflicting restoration receipt names");
        s->receipt=i==2 ? RestorationReceiptObservation::Partial : RestorationReceiptObservation::Final;
    }
    for(std::size_t i=4;i<6;++i) {
        auto file=optionalFile(s->root,s->names[i],maximum,stop);if(!file)continue;
        s->files[i].emplace(std::move(*file));
        require(s->files[i]->size()==intent.content.bytes && s->files[i]->digest(stop)==intent.content.sha256,
            "Restoration audit media bytes mismatch",ErrorCode::MediaMismatch);
    }
    for(std::size_t i=0;i<s->files.size();++i)if(s->files[i])s->hashes[i]=s->files[i]->digest(stop);
    return s;
}
} // namespace
RestorationAudit inspectAssetRestoration(const std::filesystem::path &root,const Id &project,const Asset &expected,
    const Id &operation,std::uint64_t maximum,ResourceLedger memory,ProjectBudget budget,std::stop_token stop) {
    RestorationAudit audit(inspectState(root,project,expected.id,&expected,operation,maximum,memory,budget,stop));
    audit.reverify(stop);return audit;
}
RestorationAudit inspectSavedAssetRestoration(const std::filesystem::path &root,const Id &project,const Id &asset,
    const Id &operation,std::uint64_t maximum,ResourceLedger memory,ProjectBudget budget,std::stop_token stop) {
    RestorationAudit audit(inspectState(root,project,asset,nullptr,operation,maximum,memory,budget,stop));
    audit.reverify(stop);return audit;
}
SavedRestorationSourceTicket::SavedRestorationSourceTicket(RestorationAudit &audit,std::string_view relative,
    SavedRestorationSourceContext context,ResourceLedger memory,std::stop_token stop)
    :lease_(memory.reserve(sizeof(SavedRestorationSourceTicket)+4096)),context_(context) {
    poll(stop);require(context.request && context.job,"Invalid saved source request owner");
    require(!relative.empty() && relative.size()<=1024 && relative.find('\0')==relative.npos && validUtf8(relative),
        "Invalid explicit saved source selection",ErrorCode::InvalidParameter);
    require(audit.ownedBy(memory),"Saved source audit admission scope mismatch");audit.reverify(stop);
    const auto &s=audit.state();root_=s.stamp;hashes_=s.hashes;
    for(std::size_t i=0;i<present_.size();++i)present_[i]=bool(s.files[i]);
    const auto &intent=audit.intent();sourceSha256_=intent.content.sha256;sourceBytes_=intent.content.bytes;
    frames_=intent.frames;rate_=intent.rate;channels_=intent.layout.channels;
    selectedRelative_=relative;poll(stop);
}
void verifySavedRestorationSourceReport(const SavedRestorationSourceTicket &ticket,RestorationAudit &audit,
    const WaveCheckReport &report,SavedRestorationSourceContext context,std::size_t pid,ResourceLedger memory,std::stop_token stop) {
    poll(stop);require(memory.owns(ticket.lease_) && audit.ownedBy(memory) && report.ownedBy(memory),
        "Saved source report admission scope mismatch");
    require(context==ticket.context_,"Stale saved source request owner");
    require(pid && report.workerPid()==pid && report.relative()==ticket.selectedRelative_,
        "Saved source checker identity mismatch");
    audit.reverify(stop);const auto &s=audit.state();
    require(s.stamp==ticket.root_ && s.hashes==ticket.hashes_,"Saved source audit changed since selection");
    for(std::size_t i=0;i<ticket.present_.size();++i)
        require(bool(s.files[i])==ticket.present_[i],"Saved source audit visibility changed");
    const auto &a=report.audio();
    require(a.sourceBytes==ticket.sourceBytes_ && a.sourceSha256==ticket.sourceSha256_ && a.frames==ticket.frames_ &&
        a.rate==ticket.rate_ && a.channels==ticket.channels_,"Saved source report differs from original asset",ErrorCode::MediaMismatch);
    poll(stop);
}
OwnedInspectionProtocol copySavedRestorationSourceProtocol(const SavedRestorationSourceTicket &ticket,RestorationAudit &audit,
    const WaveCheckReport &report,SavedRestorationSourceContext context,std::size_t pid,ResourceLedger memory,std::stop_token stop) {
    verifySavedRestorationSourceReport(ticket,audit,report,context,pid,memory,stop);
    return report.copyProtocol(memory,stop);
}
void verifySavedRestorationSourceFile(const SavedRestorationSourceTicket &ticket,RestorationAudit &audit,
    const WaveCheckReport &report,SavedRestorationSourceContext context,std::size_t pid,ApprovedMediaFile &source,
    ResourceLedger memory,std::stop_token stop,const std::function<void()> &beforeChunk) {
    poll(stop);require(source.ownedBy(memory),"Saved source file admission scope mismatch");
    verifySavedRestorationSourceReport(ticket,audit,report,context,pid,memory,stop);
    const auto &audio=report.audio();
    require(source.size()==audio.sourceBytes,"Saved source file extent differs from checked source",ErrorCode::MediaMismatch);
    source.verifyUnchanged();
    require(source.digest(stop,beforeChunk)==audio.sourceSha256,
        "Saved source file differs from checked content",ErrorCode::MediaMismatch);
    source.verifyUnchanged();
    // The bounded read can take time. Check current saved evidence and owner
    // again after it, rather than relying on the audit from before the read.
    verifySavedRestorationSourceReport(ticket,audit,report,context,pid,memory,stop);
    source.verifyUnchanged();poll(stop);
}
} // namespace soundcurrent::daw

namespace soundcurrent::daw {
struct SavedRestorationStageState {
    ResourceLease lease;
    ResourceLedger memory;
    RestorationAudit audit;
    SavedRestorationRecord record;
    ApprovedMediaFile source;
    StagedMedia stage;
    bool attempted=false;
    SavedRestorationStageState(ResourceLease grant,ResourceLedger resources,RestorationAudit original,
        SavedRestorationRecord evidence,ApprovedMediaFile selected,StagedMedia prepared)
        :lease(std::move(grant)),memory(resources),audit(std::move(original)),record(std::move(evidence)),
         source(std::move(selected)),stage(std::move(prepared)) {}
};
SavedRestorationStage::SavedRestorationStage(std::unique_ptr<SavedRestorationStageState> state):state_(std::move(state)) {}
SavedRestorationStage::SavedRestorationStage(SavedRestorationStage &&) noexcept=default;
SavedRestorationStage::~SavedRestorationStage()=default;
const SavedRestorationRecord &SavedRestorationStage::record() const {
    require(bool(state_),"Retired saved restoration stage");return state_->record;
}
const std::string &SavedRestorationStage::relativePath() const {record();return state_->stage.relativePath();}
SavedRestorationStage SavedRestorationStage::prepare(ApprovedMediaFile source,const std::filesystem::path &root,RestorationAudit audit,
    const SavedRestorationSourceTicket &ticket,const WaveCheckReport &report,std::size_t observedCheckerPid,
    SavedRestorationRecordContext current,Id operation,std::uint64_t maximum,WaveValidationLimits limits,
    std::stop_token stop,const StageObserver &observer) {
    poll(stop);const auto memory=source.resourceLedger();require(audit.ownedBy(memory),"Saved staging audit scope mismatch");
#ifdef _WIN32
    const auto actualPid=static_cast<std::uint64_t>(GetCurrentProcessId());
#else
    const auto actualPid=static_cast<std::uint64_t>(getpid());
#endif
    require(current.workerPid==actualPid,"Saved staging writer is not this native process");
    require(operation!=audit.intent().operation,"Saved staging cannot reuse historical operation");
    require(maximum>0 && maximum<=static_cast<std::uint64_t>(INT64_MAX) && source.size()<=maximum,
        "Invalid saved staging source budget",ErrorCode::ResourceLimit);
    auto grant=memory.reserve(sizeof(SavedRestorationStageState)+4096);
    verifySavedRestorationSourceFile(ticket,audit,report,current.source,observedCheckerPid,source,memory,stop);
    const auto &original=audit.intent();const auto &manifest=audit.manifest();
    const auto found=std::find_if(manifest.assets.begin(),manifest.assets.end(),[&](const Asset &a){return a.id==original.asset;});
    require(found!=manifest.assets.end(),"Saved staging asset disappeared");
    const auto checked=verifyAssetReplacement(source,*found,limits,stop);
    SavedRestorationRecordData data{operation,manifest.id,found->id};data.context=current;data.relativePath=found->relativePath;
    data.content={checked.sourceBytes,checked.sourceSha256};data.frames=found->frames;data.rate=found->sampleRate;data.layout=found->layout;
    require(data.content.bytes==original.content.bytes && data.content.sha256==original.content.sha256 &&
        data.frames==original.frames && data.rate==original.rate && data.layout==original.layout,
        "Fresh saved decode differs from original audit",ErrorCode::MediaMismatch);
    auto record=SavedRestorationRecord::planned(data,memory,stop);
    // A fresh full finite decode is separate from a previous checker report.
    // Recheck that original report/fingerprint and the same native source again.
    verifySavedRestorationSourceFile(ticket,audit,report,current.source,observedCheckerPid,source,memory,stop);
    auto intent=encodeSavedRestorationRecord(record,SavedRestorationPhase::Planned,memory,stop);
    auto stage=StagedMedia::prepareSavedRestorationStage(source,root,data.content,maximum,std::move(operation),intent,
        audit.state().stamp,stop,observer);
    SavedRestorationStage result(std::make_unique<SavedRestorationStageState>(std::move(grant),memory,std::move(audit),
        std::move(record),std::move(source),std::move(stage)));
    result.reverify(stop);return result;
}
void SavedRestorationStage::reverify(std::stop_token stop) {
    record();auto &s=*state_;poll(stop);const auto &original=s.audit.intent();const auto &d=s.record.data();
    require(s.record.ownedBy(s.memory) && s.audit.ownedBy(s.memory) && s.source.ownedBy(s.memory),"Saved stage scope mismatch");
    require(d.operation!=original.operation && d.asset==original.asset && d.relativePath==original.relativePath &&
        d.content.bytes==original.content.bytes && d.content.sha256==original.content.sha256 &&
        d.frames==original.frames && d.rate==original.rate && d.layout==original.layout,"Saved stage changed original identity");
    auto intent=encodeSavedRestorationRecord(s.record,SavedRestorationPhase::Planned,s.memory,stop);
    const auto verify=[&]{s.stage.verifyRestorationParentTransition(s.audit.state().stamp,s.audit.state().root.directoryStamp(),
        original.content,intent.bytes(),s.memory,stop);};
    verify();s.audit.reverifyFiles(stop);s.source.verifyUnchanged();
    require(s.source.digest(stop)==d.content.sha256 && s.source.size()==d.content.bytes,"Saved stage source changed",ErrorCode::MediaMismatch);
    verify();poll(stop);
}
MediaCommitOutcome SavedRestorationStage::publishForPermit(const std::filesystem::path &root,
    std::stop_token stop,const StageObserver &observer) {
    record();auto &s=*state_;require(!s.attempted,"Saved stage already attempted");s.attempted=true;
    reverify(stop); // Exact ORIGINAL native transition BEFORE publication creates any directories.
    auto intent=encodeSavedRestorationRecord(s.record,SavedRestorationPhase::Planned,s.memory,stop);
    auto receipt=encodeSavedRestorationRecord(s.record,SavedRestorationPhase::Published,s.memory,stop);
    const auto guard=[&](StageBoundary at,std::uint64_t bytes) {
        if(observer)observer(at,bytes);
        if(at==StageBoundary::BeforeCommit) {
            // Canonical directory creation legitimately changes the parent stamp.
            // Keep the original root OBJECT and all original pinned/named facts;
            // never replace the original audit with a newly captured fingerprint.
            poll(stop);ApprovedMediaRoot named(root,s.memory);
            const auto original=s.audit.state().stamp,held=s.audit.state().root.directoryStamp(),now=named.directoryStamp();
            require(original.identity[0]==held.identity[0] && original.identity[1]==held.identity[1] &&
                now.identity[0]==held.identity[0] && now.identity[1]==held.identity[1],"Saved publication root replaced",ErrorCode::MediaMismatch);
            s.stage.verifyRestorationPublicationNames(s.memory,stop);
            s.audit.reverifyFiles(stop);s.source.verifyUnchanged();
            require(s.source.size()==s.record.data().content.bytes && s.source.digest(stop)==s.record.data().content.sha256,
                "Saved publication source changed",ErrorCode::MediaMismatch);
            s.stage.verifyRestorationPublicationNames(s.memory,stop);poll(stop);
        }
    };
    return s.stage.restoreMissing(s.record.data().relativePath,intent,receipt,stop,guard);
}
} // namespace soundcurrent::daw

namespace soundcurrent::daw {
namespace {
std::string controlHex(std::string_view bytes){constexpr char digits[]="0123456789abcdef";std::string out;out.reserve(bytes.size()*2);
    for(unsigned char c:bytes){out+=digits[c>>4];out+=digits[c&15];}return out;}
std::string controlUnhex(const nlohmann::json &j,std::size_t maximum){require(j.is_string(),"Invalid saved control hex type");const auto &v=j.get_ref<const std::string &>();
    require(v.size()<=maximum*2 && v.size()%2==0,"Saved control hex limit");auto digit=[](char c)->unsigned {
        if(c>='0'&&c<='9')return unsigned(c-'0');
        if(c>='a'&&c<='f')return unsigned(c-'a'+10);
        throw ProjectError(ErrorCode::InvalidState,"Invalid saved control hex");};
    std::string out;out.reserve(v.size()/2);for(std::size_t n=0;n<v.size();n+=2)out+=char((digit(v[n])<<4)|digit(v[n+1]));return out;}
}
void SavedRestorationSourceTicket::verifyRootStamp(ApprovedDirectoryStamp actual,ResourceLedger memory) const {
    require(memory.owns(lease_),"Saved source root stamp scope mismatch");
    require(actual==root_,"Saved source original destination changed",ErrorCode::MediaMismatch);
}
OwnedInspectionProtocol SavedRestorationSourceTicket::copyControlProtocol(RestorationAudit &audit,const WaveCheckReport &report,
    SavedRestorationSourceContext current,std::size_t pid,ResourceLedger memory,std::stop_token stop) const {
    poll(stop);verifySavedRestorationSourceReport(*this,audit,report,current,pid,memory,stop);
    auto work=memory.reserve(savedSourceControlMaximumBytes*32);auto output=memory.reserve(savedSourceControlMaximumBytes+1024);
    std::string fingerprint;fingerprint.reserve(441);
    for(auto word:root_.identity)for(unsigned n=0;n<8;++n)fingerprint+=char((word>>(n*8))&255);
    for(const auto &h:hashes_)fingerprint.append(h.data(),h.size());
    unsigned mask=0;
    for(unsigned n=0;n<6;++n)if(present_[n])mask|=1U<<n;
    fingerprint+=char(mask);
    auto bytes=nlohmann::json({{"schema","sc-saved-source-control-v1"},{"projectEpoch",context_.projectEpoch},{"modelRevision",context_.modelRevision},
        {"request",context_.request},{"job",context_.job},{"rootAndFilesHex",controlHex(fingerprint)},{"selectedRelativeHex",controlHex(selectedRelative_)},
        {"sourceSha256",std::string(sourceSha256_.data(),sourceSha256_.size())},{"sourceBytes",sourceBytes_},
        {"frames",frames_},{"rate",rate_},{"channels",channels_}}).dump();
    require(bytes.size()<=savedSourceControlMaximumBytes,"Saved source control frame exceeds bank");
    verifySavedRestorationSourceReport(*this,audit,report,current,pid,memory,stop);poll(stop);return {std::move(output),std::move(bytes)};
}
SavedRestorationSourceTicket SavedRestorationSourceTicket::fromControlProtocol(std::string_view bytes,RestorationAudit &audit,
    const WaveCheckReport &report,SavedRestorationSourceContext current,std::size_t pid,ResourceLedger memory,std::stop_token stop) {
    poll(stop);require(!bytes.empty()&&bytes.size()<=savedSourceControlMaximumBytes,"Saved source control frame exceeds bank");
    for(unsigned char c:bytes)require(c&&c<128,"Saved source control must be ASCII JSON");
    auto work=memory.reserve(savedSourceControlMaximumBytes*32);
    constexpr std::array<std::string_view,12> fields{"schema","projectEpoch","modelRevision","request","job","rootAndFilesHex","selectedRelativeHex","sourceSha256","sourceBytes","frames","rate","channels"};
    std::array<bool,12> seen{};
    try {
        const auto callback=[&](int depth,nlohmann::json::parse_event_t event,nlohmann::json &v){poll(stop);
            require(depth<=1&&event!=nlohmann::json::parse_event_t::array_start,"Nested saved source control refused");
            if(event==nlohmann::json::parse_event_t::key){const auto &key=v.get_ref<const std::string &>();const auto it=std::find(fields.begin(),fields.end(),key);
                require(it!=fields.end(),"Unknown saved source control key");const auto n=static_cast<std::size_t>(it-fields.begin());require(!seen[n],"Duplicate saved source control key");seen[n]=true;}return true;};
        const auto j=nlohmann::json::parse(bytes.begin(),bytes.end(),callback);require(j.is_object()&&std::all_of(seen.begin(),seen.end(),[](bool x){return x;})&&j.at("schema")=="sc-saved-source-control-v1","Incomplete saved source control");
        const auto integer=[&](const char *key){const auto &v=j.at(key);require(v.is_number_unsigned(),"Invalid saved source control integer");return v.get<std::uint64_t>();};
        require((SavedRestorationSourceContext{integer("projectEpoch"),integer("modelRevision"),integer("request"),integer("job")}==current),"Stale saved source control context");
        const auto name=controlUnhex(j.at("selectedRelativeHex"),1024),fingerprint=controlUnhex(j.at("rootAndFilesHex"),441);
        require(fingerprint.size()==441,"Invalid saved source fingerprint extent");
        SavedRestorationSourceTicket fresh(audit,name,current,memory,stop);std::size_t at=0;
        for(auto word:fresh.root_.identity){std::uint64_t value=0;for(unsigned n=0;n<8;++n)value|=std::uint64_t(static_cast<unsigned char>(fingerprint[at++]))<<(n*8);
            require(word==value,"Original saved control root changed",ErrorCode::MediaMismatch);}
        for(const auto &h:fresh.hashes_)for(char c:h)require(c==fingerprint[at++],"Original saved control pinned content changed",ErrorCode::MediaMismatch);
        const auto mask=static_cast<unsigned char>(fingerprint[at]);require(mask<64,"Invalid saved control presence mask");
        for(unsigned n=0;n<6;++n)require(fresh.present_[n]==bool(mask&(1U<<n)),"Original saved control visibility changed",ErrorCode::MediaMismatch);
        require(j.at("sourceSha256").is_string()&&j.at("sourceSha256").get_ref<const std::string &>()==std::string_view(fresh.sourceSha256_.data(),64)&&
            integer("sourceBytes")==fresh.sourceBytes_&&integer("frames")==fresh.frames_&&integer("rate")==fresh.rate_&&integer("channels")==fresh.channels_,
            "Original saved control audio changed",ErrorCode::MediaMismatch);
        verifySavedRestorationSourceReport(fresh,audit,report,current,pid,memory,stop);poll(stop);return fresh;
    }catch(const nlohmann::json::exception &){throw ProjectError(ErrorCode::InvalidState,"Malformed saved source control");}
}
} // namespace soundcurrent::daw

namespace soundcurrent::daw {
namespace {
void savedDispatchOwner(const SavedRestorationOwnerView &owner,ResourceLedger memory,std::stop_token stop) {
    poll(stop);const auto &c=owner.context;
    require(!owner.acceptedProject&&!owner.closing&&!owner.ioActive&&!owner.publicationAmbiguous &&
        c.source.request && c.source.job && c.preparationJob && c.request && c.job,
        "No current idle saved restoration owner");
    require(owner.root.is_absolute() && owner.root.native().size()<=4096 &&
        owner.root.native().find(std::filesystem::path::value_type(0))==owner.root.native().npos,
        "Invalid saved dispatch root");
#ifdef _WIN32
    // Reject lone UTF-16 surrogates before toolchain-dependent path conversion.
    const auto &units=owner.root.native();
    for(std::size_t i=0;i<units.size();++i) {
        const auto unit=static_cast<unsigned>(units[i]);
        if(unit>=0xd800 && unit<=0xdbff) {
            require(i+1<units.size(),"Unpaired saved dispatch high surrogate");
            const auto low=static_cast<unsigned>(units[++i]);
            require(low>=0xdc00 && low<=0xdfff,"Invalid saved dispatch surrogate pair");
        } else require(unit<0xdc00 || unit>0xdfff,"Unpaired saved dispatch low surrogate");
    }
#endif
    auto conversionCredit=memory.reserve(16384); // Admit worst-case UTF-8 conversion before allocation.
    std::u8string u;
    try {u=owner.root.u8string();}
    catch(const std::bad_alloc &) {throw ProjectError(ErrorCode::ResourceLimit,"Saved dispatch root conversion allocation");}
    catch(const std::exception &) {throw ProjectError(ErrorCode::InvalidState,"Invalid saved dispatch native encoding");}
    require(u.size()<=4096 && validUtf8(std::string_view(reinterpret_cast<const char *>(u.data()),u.size())),
        "Invalid saved dispatch root encoding");
}
void savedDispatchRoot(const SavedRestorationOwnerView &owner,const SavedRestorationSourceTicket &source,
    ResourceLedger memory,std::stop_token stop) {
    poll(stop);ApprovedMediaRoot root(owner.root,memory);source.verifyRootStamp(root.directoryStamp(),memory);poll(stop);
}
}
SavedRestorationDispatchTicket::SavedRestorationDispatchTicket(ResourceLease lease,OwnedInspectionProtocol proof,
    const SavedRestorationOwnerView &owner,const RestorationAudit &audit,Id operation,std::size_t pid)
    :lease_(std::move(lease)),originalProof_(std::move(proof)),root_(owner.root),context_(owner.context),
     history_(audit.intent().operation),operation_(std::move(operation)),checkerPid_(pid),
     identity_(operation_,audit.manifest().id,audit.intent().asset) {
    const auto &a=audit.intent();identity_.relativePath=a.relativePath;identity_.content=a.content;
    identity_.frames=a.frames;identity_.rate=a.rate;identity_.layout=a.layout;
    identity_.context={owner.context.source,owner.context.preparationJob,owner.context.request,owner.context.job,0};
}
const Id &SavedRestorationDispatchTicket::operation() const {
    require(lease_.bytes()!=0 && originalProof_.chargedBytes()!=0,"Retired saved dispatch ticket");return operation_;
}
SavedRestorationDispatchTicket SavedRestorationDispatchTicket::bind(const SavedRestorationOwnerView &owner,
    RestorationAudit &audit,const SavedRestorationSourceTicket &source,const WaveCheckReport &report,
    std::size_t pid,Id operation,ResourceLedger memory,std::stop_token stop) {
    savedDispatchOwner(owner,memory,stop);
    auto lease=memory.reserve(sizeof(SavedRestorationDispatchTicket)+32768); // Both UTF-8 and native path storage.
    require(operation!=audit.intent().operation,"Saved dispatch reused historical operation");
    savedDispatchRoot(owner,source,memory,stop);
    auto proof=source.copyControlProtocol(audit,report,owner.context.source,pid,memory,stop);
    poll(stop);return {std::move(lease),std::move(proof),owner,audit,std::move(operation),pid};
}
void SavedRestorationDispatchTicket::verifyOwner(const SavedRestorationOwnerView &owner,ResourceLedger memory,std::stop_token stop) const {
    poll(stop);require(memory.owns(lease_) && originalProof_.ownedBy(memory),"Saved dispatch scope mismatch");
    savedDispatchOwner(owner,memory,stop);
    require(owner.root.native()==root_.native() && owner.context==context_,"Stale saved dispatch owner");
}
void SavedRestorationDispatchTicket::verifyReady(const SavedRestorationRecord &ready,const SavedRestorationOwnerView &owner,
    std::size_t pid,ResourceLedger memory,std::stop_token stop) const {
    verifyOwner(owner,memory,stop);require(pid && pid<=UINT32_MAX && ready.ownedBy(memory),"Saved READY PID/scope mismatch");
    auto expected=identity_.context;expected.workerPid=pid;const auto &d=ready.data();
    require(d.context==expected && d.operation==operation_ && d.project==identity_.project && d.asset==identity_.asset &&
        d.phase==SavedRestorationPhase::Planned && d.relativePath==identity_.relativePath &&
        d.content.bytes==identity_.content.bytes && d.content.sha256==identity_.content.sha256 &&
        d.frames==identity_.frames && d.rate==identity_.rate && d.layout==identity_.layout,"Saved READY differs from original dispatch");
    poll(stop);
}
void SavedRestorationDispatchTicket::verify(const SavedRestorationOwnerView &owner,RestorationAudit &audit,
    const SavedRestorationSourceTicket &source,const WaveCheckReport &report,std::size_t pid,
    ResourceLedger memory,std::stop_token stop) const {
    poll(stop);require(memory.owns(lease_) && originalProof_.ownedBy(memory),"Saved dispatch scope mismatch");
    savedDispatchOwner(owner,memory,stop);
    require(owner.root.native()==root_.native() && owner.context==context_ && pid==checkerPid_ &&
        audit.intent().operation==history_,"Stale saved dispatch owner");
    savedDispatchRoot(owner,source,memory,stop);
    auto proof=source.copyControlProtocol(audit,report,owner.context.source,pid,memory,stop);
    require(proof.bytes()==originalProof_.bytes(),"Saved dispatch original fingerprint changed",ErrorCode::MediaMismatch);
    poll(stop);
}
} // namespace soundcurrent::daw
