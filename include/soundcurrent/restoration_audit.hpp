// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include "restoration_record.hpp"
#include "project_store.hpp"
namespace soundcurrent::daw {
enum class RestorationMediaObservation { Missing, Verified };
enum class RestorationReceiptObservation { Missing, Partial, Final };
struct RestorationAuditState;
// Read-only I/O-worker snapshot, never publication/resume/rollback authority.
// The manifest is passively admitted; unrelated media need not exist. Strict
// ProjectStore::load and fresh controller ownership remain separate gates.
class RestorationAudit {
  public:
    RestorationAudit(RestorationAudit &&) noexcept;
    ~RestorationAudit();
    RestorationAudit(const RestorationAudit &)=delete;
    const Session &manifest() const;
    std::string_view manifestBytes() const;
    std::array<char,64> manifestSha256() const;
    const AssetRestorationRecordData &intent() const;
    RestorationMediaObservation canonical() const;
    RestorationMediaObservation staged() const;
    RestorationReceiptObservation receipt() const;
    bool ownedBy(ResourceLedger) const;
    // Checks pinned bytes AND current relative-name observations again. Not an
    // atomic namespace snapshot against hostile same-user directory writers.
    void reverify(std::stop_token = {});
  private:
    friend RestorationAudit inspectAssetRestoration(const std::filesystem::path &,const Id &,const Asset &,
        const Id &,std::uint64_t,ResourceLedger,ProjectBudget,std::stop_token);
    explicit RestorationAudit(std::unique_ptr<RestorationAuditState>);
    RestorationAuditState &state() const;
    std::unique_ptr<RestorationAuditState> state_;
};
// Explicit trusted root/project/asset/operation and size budgets. Historical
// intent/receipt tokens are compared as evidence, never used as current tokens.
RestorationAudit inspectAssetRestoration(const std::filesystem::path &,const Id &project,const Asset &,
    const Id &operation,std::uint64_t maximumMediaBytes,ResourceLedger,ProjectBudget = {},std::stop_token = {});
} // namespace soundcurrent::daw
