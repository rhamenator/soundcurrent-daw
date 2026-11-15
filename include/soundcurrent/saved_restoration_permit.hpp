// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include "saved_restoration_dispatch.hpp"
#include "saved_restoration_stage.hpp"
namespace soundcurrent::daw {
inline constexpr std::size_t savedRestorationPermitMaximumBytes=65536;
inline constexpr std::size_t savedRestorationPermitWorkCharge=savedRestorationPermitMaximumBytes*32;
inline constexpr std::size_t savedRestorationReadyMaximumBytes=32768;
inline constexpr std::size_t savedRestorationReadyWorkCharge=savedRestorationReadyMaximumBytes*32;
class SavedRestorationPublicationPermit;
// Native I/O only: keeps the SAME preverified original dispatch and live stage.
// No construction from a saved record, DTO, stdout or exited stage.
class DispatchedSavedRestoration {
  public:
    static DispatchedSavedRestoration prepare(SavedRestorationDispatchTicket,ApprovedMediaFile,
        RestorationAudit,const SavedRestorationSourceTicket &,const WaveCheckReport &,std::size_t actualCheckerPid,
        const SavedRestorationOwnerView &,std::size_t actualWorkerPid,std::uint64_t maximumBytes,
        WaveValidationLimits = {},std::stop_token = {},const StageObserver & = {});
    DispatchedSavedRestoration(DispatchedSavedRestoration &&) noexcept=default;
    DispatchedSavedRestoration(const DispatchedSavedRestoration &)=delete;
    const SavedRestorationRecord &record() const {return stage_.record();}
    // v2 groundwork: reverify the same live native stage before advertising it.
    // No EOF/IPC loop or permission is implied; v1 stays stage-only.
    OwnedInspectionProtocol ready(const SavedRestorationOwnerView &,std::size_t actualWorkerPid,std::stop_token = {});
  private:
    friend class SavedRestorationPublicationPermit;
    DispatchedSavedRestoration(SavedRestorationDispatchTicket,SavedRestorationStage,ResourceLedger);
    MediaCommitOutcome publish(const SavedRestorationOwnerView &,std::size_t,std::stop_token,const StageObserver &);
    SavedRestorationDispatchTicket ticket_;
    SavedRestorationStage stage_;
    ResourceLedger memory_;
    bool attempted_=false;
};
// Planned metadata only. Parent supplies original dispatch/current owner/actual
// observed PID; decoded READY cannot recreate a live stage or authorize publish.
SavedRestorationRecord decodeSavedRestorationReady(std::string_view,const SavedRestorationDispatchTicket &,
    const SavedRestorationOwnerView &,std::size_t observedWorkerPid,ResourceLedger,std::stop_token = {});
// Parent supplies its CURRENT serialized owner and independently observed child
// PID. Encoding after READY checks captured metadata, not an unchanged root stamp
// invalidated by this child's own staging. Live child revalidates native proof.
OwnedInspectionProtocol encodeSavedRestorationPublicationPermit(const SavedRestorationDispatchTicket &,
    const SavedRestorationRecord &,const SavedRestorationOwnerView &,std::size_t observedWorkerPid,
    ResourceLedger,std::stop_token = {});
class SavedRestorationPublicationPermit {
  public:
    SavedRestorationPublicationPermit(SavedRestorationPublicationPermit &&) noexcept=default;
    SavedRestorationPublicationPermit(const SavedRestorationPublicationPermit &)=delete;
    MediaCommitOutcome publish(DispatchedSavedRestoration &,const SavedRestorationOwnerView &,
        std::size_t actualWorkerPid,std::stop_token = {},const StageObserver & = {});
  private:
    friend SavedRestorationPublicationPermit decodeSavedRestorationPublicationPermit(std::string_view,
        const SavedRestorationDispatchTicket &,const SavedRestorationRecord &,const SavedRestorationOwnerView &,
        std::size_t,ResourceLedger,std::stop_token);
    SavedRestorationPublicationPermit(ResourceLease lease,std::string wire):lease_(std::move(lease)),expected_(std::move(wire)) {}
    ResourceLease lease_;
    std::string expected_;
    bool used_=false;
};
SavedRestorationPublicationPermit decodeSavedRestorationPublicationPermit(std::string_view,
    const SavedRestorationDispatchTicket &,const SavedRestorationRecord &,const SavedRestorationOwnerView &,
    std::size_t actualWorkerPid,ResourceLedger,std::stop_token = {});
} // namespace soundcurrent::daw
