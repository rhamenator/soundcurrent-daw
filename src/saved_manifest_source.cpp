// SPDX-License-Identifier: GPL-3.0-only
#include <soundcurrent/saved_manifest_source.hpp>
#include <algorithm>
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
    root();s.manifestFile.verifyUnchanged();require(s.manifestFile.digest(stop)==s.manifestSha,"Pinned saved manifest changed",ErrorCode::MediaMismatch);
    {auto named=s.root.open("project.json",s.manifestFile.size(),stop);
     require(named.size()==s.manifestFile.size() && named.digest(stop)==s.manifestSha,"Named saved manifest changed",ErrorCode::MediaMismatch);}
    if(s.canonical){s.canonical->verifyUnchanged();require(s.canonical->digest(stop)==s.canonicalSha,"Pinned canonical changed",ErrorCode::MediaMismatch);}
    auto named=optionalFile(s.root,asset().relativePath,s.maximum,stop);
    require(bool(named)==bool(s.canonical),"Saved canonical visibility changed",ErrorCode::MediaMismatch);
    if(named)require(named->size()==s.canonical->size() && named->digest(stop)==s.canonicalSha,"Named canonical changed",ErrorCode::MediaMismatch);
    root();poll(stop);
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
} // namespace soundcurrent::daw
