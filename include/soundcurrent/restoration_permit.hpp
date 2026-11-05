// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include "restoration_dispatch.hpp"
#include "restoration_record.hpp"
namespace soundcurrent::daw {
inline constexpr std::size_t restorationPermitMaximumBytes=16384;
inline constexpr std::size_t restorationPermitWorkCharge=restorationPermitMaximumBytes*32;
// Temporary control-channel message. The owner must remain serialized through
// child completion, not merely while this permission is encoded.
OwnedInspectionProtocol encodeRestorationPublicationPermit(const RestorationDispatchTicket &,
    const AssetRestorationRecord &preparedEvidence,const RestorationOwnerView &current,
    std::size_t observedChildPid,ResourceLedger,std::stop_token = {});
class RestorationPublicationPermit {
  public:
    RestorationPublicationPermit(RestorationPublicationPermit &&) noexcept=default;
    RestorationPublicationPermit(const RestorationPublicationPermit &)=delete;
    // Single-use local admission bound to this live child and prepared owner.
    // A replay cannot reconstitute a retired native staging capability.
    MediaCommitOutcome publish(DispatchedAssetRestoration &,const RestorationOwnerView &current,
        std::size_t actualChildPid,std::stop_token = {},const StageObserver & = {});
  private:
    friend RestorationPublicationPermit decodeRestorationPublicationPermit(std::string_view,
        const RestorationDispatchTicket &,const AssetRestorationRecord &,const RestorationOwnerView &,
        std::size_t,ResourceLedger,std::stop_token);
    RestorationPublicationPermit(ResourceLease lease,std::string expected)
        :lease_(std::move(lease)),expected_(std::move(expected)) {}
    ResourceLease lease_;
    std::string expected_;
    bool used_=false;
};
// Exact flat schema, no echoed owner fields trusted as current. The child uses
// its actual PID, prepared evidence and captured owner from its admitted request.
RestorationPublicationPermit decodeRestorationPublicationPermit(std::string_view,
    const RestorationDispatchTicket &,const AssetRestorationRecord &,const RestorationOwnerView &current,
    std::size_t actualChildPid,ResourceLedger,std::stop_token = {});
} // namespace soundcurrent::daw
