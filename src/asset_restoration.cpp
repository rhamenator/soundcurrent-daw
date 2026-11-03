// SPDX-License-Identifier: GPL-3.0-only
#include <soundcurrent/asset_restoration.hpp>
#include <soundcurrent/restoration_record.hpp>
namespace soundcurrent::daw {
namespace {
void require(bool ok,const char *message,ErrorCode code=ErrorCode::InvalidState) {
    if(!ok)throw ProjectError(code,message);
}
void ownedPath(std::string_view path) {
    validateRelativeMediaPath(path);
    require(path.starts_with("media/") && path.back()!='/',"Restoration requires an owned media path",ErrorCode::InvalidParameter);
    std::size_t from=0,count=0;
    while(from<path.size()) {
        const auto end=path.find('/',from);const auto part=path.substr(from,end==path.npos ? end : end-from);
        require(part.size()<=255 && ++count<=64,"Restoration path limit",ErrorCode::InvalidParameter);
        if(end==path.npos)break;
        from=end+1;
    }
}
}
struct AssetRestorationState {
    ResourceLease lease;
    Asset identity;
    AssetRestorationContext context;
    AssetRestorationRecord record;
    ResourceLedger resources;
    StagedMedia stage;
    bool attempted=false;
    AssetRestorationState(ResourceLease grant,Asset asset,AssetRestorationContext value,AssetRestorationRecord evidence,ResourceLedger memory,StagedMedia media)
        :lease(std::move(grant)),identity(std::move(asset)),context(value),record(std::move(evidence)),resources(memory),stage(std::move(media)) {}
};
PreparedAssetRestoration::PreparedAssetRestoration(std::unique_ptr<AssetRestorationState> state):state_(std::move(state)) {}
PreparedAssetRestoration::PreparedAssetRestoration(PreparedAssetRestoration &&) noexcept=default;
PreparedAssetRestoration::~PreparedAssetRestoration()=default;
PreparedAssetRestoration PreparedAssetRestoration::prepare(ApprovedMediaFile &source,const std::filesystem::path &root,
    const Asset &expected,AssetRestorationContext context,std::uint64_t maximum,WaveValidationLimits limits,
    Id operation,std::stop_token stop,const StageObserver &observer) {
    require(!stop.stop_requested(),"Asset restoration canceled",ErrorCode::Canceled);
    require(context.projectEpoch && context.request,"Invalid asset restoration context");
    ownedPath(expected.relativePath);
    require(expected.sha256.size()==64,"Invalid asset restoration hash length");
    require(maximum>0 && maximum<=static_cast<std::uint64_t>(INT64_MAX),"Invalid restoration byte budget",ErrorCode::InvalidParameter);
    require(source.size()<=maximum,"Restoration source exceeds byte budget",ErrorCode::ResourceLimit);
    auto grant=source.resourceLedger().reserve(sizeof(AssetRestorationState)+4096);
    Asset identity{expected.id,expected.relativePath,expected.sha256,expected.sampleRate,expected.layout,expected.frames,{}};
    const auto verified=verifyAssetReplacement(source,identity,limits,stop);
    const auto memory=source.resourceLedger();auto recordGrant=memory.reserve(assetRestorationRecordValueCharge);
    AssetRestorationRecordData data{operation,identity.id};data.context=context;data.relativePath=identity.relativePath;
    data.content={verified.sourceBytes,verified.sourceSha256};data.frames=identity.frames;data.rate=identity.sampleRate;data.layout=identity.layout;
    AssetRestorationRecord record(std::move(recordGrant),std::move(data));
    auto intent=encodeAssetRestorationRecord(record,AssetRestorationPhase::Planned,memory,stop);
    auto stage=StagedMedia::prepareRestorationStage(source,root,{verified.sourceBytes,verified.sourceSha256},maximum,
        std::move(operation),intent,stop,observer);
    return PreparedAssetRestoration(std::make_unique<AssetRestorationState>(std::move(grant),std::move(identity),context,std::move(record),memory,std::move(stage)));
}
const Id &PreparedAssetRestoration::operation() const {
    require(bool(state_),"Retired asset restoration");return state_->stage.operation();
}
const std::string &PreparedAssetRestoration::stagedRelativePath() const {operation();return state_->stage.relativePath();}
std::size_t PreparedAssetRestoration::chargedBytes() const {operation();return state_->lease.bytes()+state_->record.chargedBytes()+state_->stage.chargedBytes();}
const AssetRestorationRecord &PreparedAssetRestoration::record() const {operation();return state_->record;}
MediaCommitOutcome PreparedAssetRestoration::publish(const Asset &current,AssetRestorationContext context,
    std::stop_token stop,const StageObserver &observer) {
    operation();auto &s=*state_;require(!s.attempted,"Asset restoration already attempted");s.attempted=true;
    const auto &a=s.identity;
    require(context==s.context && current.id==a.id && current.relativePath==a.relativePath && current.sha256==a.sha256 &&
        current.frames==a.frames && current.sampleRate==a.sampleRate && current.layout==a.layout,"Stale asset restoration context/identity");
    auto intent=encodeAssetRestorationRecord(s.record,AssetRestorationPhase::Planned,s.resources,stop);
    auto receipt=encodeAssetRestorationRecord(s.record,AssetRestorationPhase::Published,s.resources,stop);
    return s.stage.restoreMissing(a.relativePath,intent,receipt,stop,observer);
}
} // namespace soundcurrent::daw
