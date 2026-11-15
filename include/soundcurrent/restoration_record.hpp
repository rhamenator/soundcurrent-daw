// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include "asset_restoration.hpp"
#include "import_inspection_report.hpp"
namespace soundcurrent::daw {
enum class AssetRestorationPhase {Planned=1,Published=2};
struct AssetRestorationRecordData {
    AssetRestorationRecordData(Id operationId,Id assetId):operation(std::move(operationId)),asset(std::move(assetId)) {}
    Id operation,asset;
    AssetRestorationContext context;
    std::string relativePath;
    CheckedMediaBytes content;
    Frame frames=0;
    std::uint32_t rate=0;
    ChannelLayout layout;
    AssetRestorationPhase phase=AssetRestorationPhase::Planned;
};
// Admitted immutable evidence. Decoding a record grants no source/destination
// access, no proof of publication and no permission to resume an old request.
class AssetRestorationRecord {
  public:
    AssetRestorationRecord(AssetRestorationRecord &&) noexcept=default;
    AssetRestorationRecord(const AssetRestorationRecord &)=delete;
    const AssetRestorationRecordData &data() const noexcept {return data_;}
    bool ownedBy(ResourceLedger memory) const {return memory.owns(lease_);}
    std::size_t chargedBytes() const {return lease_.bytes();}
  private:
    friend class PreparedAssetRestoration;
    friend AssetRestorationRecord decodeAssetRestorationRecord(std::string_view,ResourceLedger,std::stop_token);
    AssetRestorationRecord(ResourceLease,AssetRestorationRecordData);
    ResourceLease lease_;
    AssetRestorationRecordData data_;
};
inline constexpr std::size_t assetRestorationRecordMaximumBytes=8192;
inline constexpr std::size_t assetRestorationRecordValueCharge=4096;
inline constexpr std::size_t assetRestorationRecordParserCharge=assetRestorationRecordMaximumBytes*32;
OwnedInspectionProtocol encodeAssetRestorationRecord(const AssetRestorationRecord &,AssetRestorationPhase,ResourceLedger,std::stop_token = {});
AssetRestorationRecord decodeAssetRestorationRecord(std::string_view,ResourceLedger,std::stop_token = {});
// Live-owner metadata binding only. An on-disk receipt still needs independent
// canonical media verification; old process tokens are not restart authority.
void verifyAssetRestorationRecord(const AssetRestorationRecord &,const Asset &,AssetRestorationContext,
    const Id &operation,AssetRestorationPhase,ResourceLedger,std::stop_token = {});
} // namespace soundcurrent::daw
