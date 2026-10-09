// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include "inspection_bundle.hpp"
#include "media_provenance.hpp"

namespace soundcurrent::daw {
struct ProjectImportLimits {
    InspectionBundleLimits inspection;
    std::uint64_t maximumMediaBytes = 8192ULL * 1024 * 1024;
};
// Immutable, admitted source/loss/media evidence. No source-root approval, foreign
// program/plugin execution, Qt, automatic Session conversion or RT work.
class ProjectImportEvidence {
  public:
    ProjectImportEvidence(ProjectImportEvidence &&) noexcept = default;
    ProjectImportEvidence &operator=(ProjectImportEvidence &&) = delete;
    const ImportedProjectSource &source() const noexcept { return source_; }
    const ImportInspectionReport &inspection() const noexcept { return inspection_; }
    std::span<const std::unique_ptr<MediaProvenance>> media() const noexcept { return media_; }
    bool ownedBy(const ResourceLedger &ledger) const;
  private:
    friend ProjectImportEvidence openProjectImportEvidence(const std::filesystem::path &,
        const ValidatedSession &,const Id &,ResourceLedger,ProjectImportLimits,std::stop_token);
    ProjectImportEvidence(ResourceLease lease,ImportedProjectSource source,ImportInspectionReport report)
        :lease_(std::move(lease)),source_(std::move(source)),inspection_(std::move(report)) {}
    ResourceLease lease_;
    ImportedProjectSource source_;
    ImportInspectionReport inspection_;
    std::vector<std::unique_ptr<MediaProvenance>> media_;
};
// Existing trusted project directory. New UUID directory/new file only. Failure
// may leave unreferenced evidence; never deletes it or mutates the Session.
ImportedProjectSource preserveProjectImportInspection(const std::filesystem::path &,
    const ImportInspectionReport &,ResourceLedger,Id sourceId=Id::generate(),
    const InspectionBundleSaveOptions & = {});
// Decode/hash the actual owned asset, bind it to the original occurrence and
// verified receipt, then publish a new portable receipt. No automatic asset edit.
ImportedMediaOrigin preserveProjectImportMedia(const std::filesystem::path &,
    const ImportedProjectSource &,const ImportInspectionReport &,const MediaProvenance &,
    const Asset &,ResourceLedger,std::stop_token={});
// Save/reopen review: full pinned bundle + receipts + original property bindings
// + full bounded owned WAVE decode. Caller owns a serialized control/I/O worker.
ProjectImportEvidence openProjectImportEvidence(const std::filesystem::path &,
    const Session &,const Id &sourceId,ResourceLedger,ProjectImportLimits={},std::stop_token={});
ProjectImportEvidence openProjectImportEvidence(const std::filesystem::path &,
    const ValidatedSession &,const Id &sourceId,ResourceLedger,ProjectImportLimits={},std::stop_token={});
} // namespace soundcurrent::daw
