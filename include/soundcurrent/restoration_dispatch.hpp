// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include "asset_restoration.hpp"
#include "wave_report.hpp"
namespace soundcurrent::daw {
struct RestorationOwnerContext {
    AssetReplacementContext checked;
    std::uint64_t dispatchJob=0,autosaveToken=0;
    bool operator==(const RestorationOwnerContext &) const=default;
};
// Borrowed CURRENT accepted owner view. The control owner must serialize model,
// root, I/O and autosave-inhibition ownership through each operation below.
struct RestorationOwnerView {
    const Session &session;
    const std::filesystem::path &root;
    RestorationOwnerContext context;
    bool closing=false,ioActive=false;
};
class RestorationDispatchTicket {
  public:
    RestorationDispatchTicket(const RestorationOwnerView &,const Id &asset,
        const AssetReplacementTicket &,const WaveCheckReport &,std::size_t observedCheckPid,
        Id operation,std::uint64_t maximumBytes,ResourceLedger,std::stop_token = {});
    RestorationDispatchTicket(RestorationDispatchTicket &&) noexcept=default;
    RestorationDispatchTicket(const RestorationDispatchTicket &)=delete;
    const Id &operation() const {return operation_;}
    const Id &assetId() const {return asset_.id;}
    std::uint64_t checkedSourceBytes() const {return sourceBytes_;}
    std::size_t chargedBytes() const {return lease_.bytes();}
  private:
    friend void verifyRestorationDispatch(const RestorationDispatchTicket &,const RestorationOwnerView &,ResourceLedger,std::stop_token);
    friend class DispatchedAssetRestoration;
    ResourceLease lease_;
    Id project_,operation_;
    Asset asset_;
    std::filesystem::path root_;
    RestorationOwnerContext context_;
    std::uint64_t sourceBytes_=0,maximumBytes_=0;
};
// Metadata only: never grants source/destination access or restart authority.
void verifyRestorationDispatch(const RestorationDispatchTicket &,const RestorationOwnerView &,ResourceLedger,std::stop_token = {});
class DispatchedAssetRestoration {
  public:
    // Isolated I/O worker only: fully decodes and stages through approved handles.
    static DispatchedAssetRestoration prepare(RestorationDispatchTicket,ApprovedMediaFile &,
        const RestorationOwnerView &,WaveValidationLimits = {},std::stop_token = {},const StageObserver & = {});
    DispatchedAssetRestoration(DispatchedAssetRestoration &&) noexcept=default;
    DispatchedAssetRestoration(const DispatchedAssetRestoration &)=delete;
    const Id &operation() const {return prepared_.operation();}
    const AssetRestorationRecord &record() const {return prepared_.record();}
    // One attempt; caller keeps the current accepted owner serialized throughout.
    MediaCommitOutcome publish(const RestorationOwnerView &,std::stop_token = {},const StageObserver & = {});
  private:
    friend class RestorationPublicationPermit;
    DispatchedAssetRestoration(RestorationDispatchTicket,PreparedAssetRestoration,ResourceLedger);
    RestorationDispatchTicket ticket_;
    PreparedAssetRestoration prepared_;
    ResourceLedger memory_;
    bool attempted_=false;
};
} // namespace soundcurrent::daw
