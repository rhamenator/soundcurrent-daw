// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include "saved_restoration_record.hpp"
namespace soundcurrent::daw {
struct SavedPublicationAuditState;
// Fresh read-only current facts. These do not attribute historical publication,
// reconstruct a live stage, release a controller guard or adopt a Session.
class SavedPublicationAudit {
  public:
    SavedPublicationAudit(SavedPublicationAudit &&) noexcept;
    ~SavedPublicationAudit();
    SavedPublicationAudit(const SavedPublicationAudit &)=delete;
    const Session &manifest() const;
    std::array<char,64> manifestSha256() const;
    const SavedRestorationRecordData &intent() const;
    RestorationMediaObservation canonical() const;
    RestorationMediaObservation staged() const;
    RestorationReceiptObservation receipt() const;
    bool ownedBy(ResourceLedger) const;
    // Retains native pins and checks current root/name/byte observations again.
    // Not an atomic snapshot against hostile concurrent namespace writers.
    void reverify(std::stop_token = {});
  private:
    friend SavedPublicationAudit inspectSavedPublication(const std::filesystem::path &,const Id &,const Asset &,
        CheckedMediaBytes,SavedRestorationRecordContext,const Id &,std::uint64_t,ResourceLedger,ProjectBudget,std::stop_token);
    explicit SavedPublicationAudit(std::unique_ptr<SavedPublicationAuditState>);
    SavedPublicationAuditState &state() const;
    std::unique_ptr<SavedPublicationAuditState> state_;
};
// Expected asset/content/context/operation must come from the retained private
// caller owner, NEVER from the journal or an untrusted completion DTO. Missing
// media and receipts are observations, not proof of a historical absence.
SavedPublicationAudit inspectSavedPublication(const std::filesystem::path &,const Id &project,const Asset &expected,
    CheckedMediaBytes,SavedRestorationRecordContext,const Id &operation,std::uint64_t maximumMediaBytes,
    ResourceLedger,ProjectBudget = {},std::stop_token = {});
} // namespace soundcurrent::daw
