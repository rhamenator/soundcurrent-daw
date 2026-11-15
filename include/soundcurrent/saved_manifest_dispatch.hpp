// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include "saved_manifest_source.hpp"
#include "saved_restoration_dispatch.hpp"
namespace soundcurrent::daw {
// Volatile trusted-parent metadata, bound BEFORE worker launch to the original
// private preparation. The caller retains native audit/source pins through
// actual reap. No process launch, mutation, publication or adoption authority.
class SavedManifestDispatchTicket {
  public:
    static SavedManifestDispatchTicket bind(const SavedRestorationOwnerView &,SavedManifestAudit &,
        const SavedManifestSourceTicket &,ApprovedMediaFile &originalSource,std::string_view originalControl,const WaveCheckReport &,
        std::size_t observedCheckerPid,Id freshOperation,ResourceLedger,std::stop_token = {});
    SavedManifestDispatchTicket(SavedManifestDispatchTicket &&) noexcept=default;
    SavedManifestDispatchTicket(const SavedManifestDispatchTicket &)=delete;
    void verifyBeforeLaunch(const SavedRestorationOwnerView &,SavedManifestAudit &,
        const SavedManifestSourceTicket &,ApprovedMediaFile &originalSource,const WaveCheckReport &,std::size_t observedCheckerPid,
        ResourceLedger,std::stop_token = {}) const;
    // Read-only evidence AFTER the caller has reaped the independently observed
    // child. Accepts the resulting namespace change only for observation: the
    // original audit's strict root stamp is neither changed nor rebased. This
    // cannot reconstruct a live SavedManifestStage or issue a publication permit.
    void inspectStaged(const SavedRestorationOwnerView &,SavedManifestAudit &,
        const SavedManifestSourceTicket &,ApprovedMediaFile &originalSource,const WaveCheckReport &,
        std::size_t observedCheckerPid,std::size_t reapedWorkerPid,const SavedRestorationRecord &,
        ResourceLedger,std::stop_token = {},const std::function<void()> &beforeChunk = {}) const;
  private:
    SavedManifestDispatchTicket(ResourceLease,OwnedInspectionProtocol,const SavedRestorationOwnerView &,
        const SavedManifestAudit &,const SavedManifestSourceTicket &,ApprovedMediaFile &,const WaveCheckReport &,std::size_t,Id);
    void verifyOwner(const SavedRestorationOwnerView &,ResourceLedger,std::stop_token) const;
    ResourceLease lease_;
    OwnedInspectionProtocol originalControl_;
    std::filesystem::path root_;
    SavedRestorationDispatchContext context_;
    ApprovedDirectoryStamp originalRoot_;
    ApprovedFileStamp originalSource_;
    std::array<char,193> originalFingerprint_;
    SavedRestorationRecordData identity_;
    std::size_t checkerPid_=0;
};
} // namespace soundcurrent::daw
