// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include "restoration_audit.hpp"
namespace soundcurrent::daw {
struct SavedRestorationRecordContext {
    SavedRestorationSourceContext source;
    std::uint64_t preparationJob=0,request=0,job=0,workerPid=0;
    bool operator==(const SavedRestorationRecordContext &) const=default;
};
enum class SavedRestorationPhase {Planned=1,Published=2};
struct SavedRestorationRecordData {
    SavedRestorationRecordData(Id op,Id projectId,Id assetId)
        :operation(std::move(op)),project(std::move(projectId)),asset(std::move(assetId)) {}
    Id operation,project,asset;
    SavedRestorationRecordContext context;
    std::string relativePath;
    CheckedMediaBytes content;
    Frame frames=0;
    std::uint32_t rate=0;
    ChannelLayout layout;
    SavedRestorationPhase phase=SavedRestorationPhase::Planned;
};
inline constexpr std::size_t savedRestorationRecordMaximumBytes=8192;
inline constexpr std::size_t savedRestorationRecordValueCharge=8192;
inline constexpr std::size_t savedRestorationRecordParserCharge=savedRestorationRecordMaximumBytes*32;
// Admitted metadata only, no native handles or current permission. Stored owner
// fields are historical evidence; the caller must independently capture CURRENT
// empty-controller/child ownership, never derive it from this record or a DTO.
class SavedRestorationRecord {
  public:
    static SavedRestorationRecord planned(const SavedRestorationRecordData &,ResourceLedger,std::stop_token = {});
    SavedRestorationRecord(SavedRestorationRecord &&) noexcept=default;
    SavedRestorationRecord(const SavedRestorationRecord &)=delete;
    const SavedRestorationRecordData &data() const noexcept {return data_;}
    bool ownedBy(ResourceLedger memory) const {return memory.owns(lease_);}
    std::size_t chargedBytes() const {return lease_.bytes();}
  private:
    friend SavedRestorationRecord decodeSavedRestorationRecord(std::string_view,ResourceLedger,std::stop_token);
    SavedRestorationRecord(ResourceLease,SavedRestorationRecordData);
    ResourceLease lease_; // All retained IDs/path storage retire before this credit.
    SavedRestorationRecordData data_;
};
OwnedInspectionProtocol encodeSavedRestorationRecord(const SavedRestorationRecord &,SavedRestorationPhase,ResourceLedger,std::stop_token = {});
SavedRestorationRecord decodeSavedRestorationRecord(std::string_view,ResourceLedger,std::stop_token = {});
// Caller supplies the actual current serialized owner and observed writer PID,
// project/asset, independently qualified content extent/hash and fresh operation.
// Equality is not staging/publication/replay
// authority. Standard accepted-model records keep their nonzero-epoch policy.
void verifySavedRestorationRecord(const SavedRestorationRecord &,const Id &project,const Asset &,CheckedMediaBytes expectedContent,
    SavedRestorationRecordContext,const Id &operation,SavedRestorationPhase,ResourceLedger,std::stop_token = {});
} // namespace soundcurrent::daw
