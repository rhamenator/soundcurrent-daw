// SPDX-License-Identifier: GPL-3.0-only
#include <soundcurrent/restoration_audit.hpp>
#include <algorithm>
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
    require(s.root.directoryStamp()==s.stamp,"Restoration audit directory changed",ErrorCode::MediaMismatch);poll(stop);
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
} // namespace soundcurrent::daw
