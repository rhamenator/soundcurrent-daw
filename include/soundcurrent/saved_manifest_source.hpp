// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include "restoration_audit.hpp"
namespace soundcurrent::daw {
struct SavedManifestAuditState;
class SavedManifestSourceTicket;
// I/O-only passive manifest/selected-asset snapshot. No journal, accepted model,
// source reopen, staging or publication authority is synthesized.
class SavedManifestAudit {
  public:
    SavedManifestAudit(SavedManifestAudit &&) noexcept;
    ~SavedManifestAudit();
    SavedManifestAudit(const SavedManifestAudit &)=delete;
    const Session &manifest() const;
    const Asset &asset() const;
    std::array<char,64> manifestSha256() const;
    RestorationMediaObservation canonical() const;
    bool ownedBy(ResourceLedger) const;
    void reverify(std::stop_token = {});
  private:
    friend class SavedManifestSourceTicket;
    friend SavedManifestAudit inspectSavedManifestAsset(const std::filesystem::path &,const Id &,const Id &,
        std::uint64_t,ResourceLedger,ProjectBudget,std::stop_token);
    explicit SavedManifestAudit(std::unique_ptr<SavedManifestAuditState>);
    SavedManifestAuditState &state() const;
    std::unique_ptr<SavedManifestAuditState> state_;
};
SavedManifestAudit inspectSavedManifestAsset(const std::filesystem::path &,const Id &project,const Id &asset,
    std::uint64_t maximumMediaBytes,ResourceLedger,ProjectBudget = {},std::stop_token = {});
// Metadata captured BEFORE the actual checker dispatch, not a source capability.
// Checker context/PID must come from the serialized owner, never a saved record.
class SavedManifestSourceTicket {
  public:
    SavedManifestSourceTicket(SavedManifestAudit &,std::string_view selectedLeaf,SavedRestorationSourceContext,
        ResourceLedger,std::stop_token = {});
    SavedManifestSourceTicket(SavedManifestSourceTicket &&) noexcept=default;
    SavedManifestSourceTicket(const SavedManifestSourceTicket &)=delete;
    void verify(SavedManifestAudit &,const WaveCheckReport &,SavedRestorationSourceContext,
        std::size_t actualPid,ResourceLedger,std::stop_token = {}) const;
    void verifyFile(SavedManifestAudit &,const WaveCheckReport &,SavedRestorationSourceContext,
        std::size_t actualPid,ApprovedMediaFile &,ResourceLedger,std::stop_token = {},
        const std::function<void()> &beforeChunk = {}) const;
  private:
    ResourceLease lease_;
    Id asset_;
    std::array<char,64> sourceSha_{};
    Frame frames_=0;
    std::uint32_t rate_=0;
    ChannelLayout layout_;
    SavedRestorationSourceContext context_;
    ApprovedDirectoryStamp root_;
    std::array<char,64> manifestSha_{},canonicalSha_{};
    std::uint64_t canonicalBytes_=0;
    bool canonicalPresent_=false;
    std::string selectedLeaf_;
};
} // namespace soundcurrent::daw
