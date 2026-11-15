// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include "comp_assembly_job.hpp"
#include "comp_assembly_dependencies.hpp"

namespace soundcurrent::daw {
// Immutable file-verified edit bound to one live owner and joined memory grant.
// Preparation performs disk/PCM work; consuming the edit does not. This is not
// an independent sonic-quality certificate or permission to discard originals.
class VerifiedCompAssemblyAdoption {
  public:
    VerifiedCompAssemblyAdoption(VerifiedCompAssemblyAdoption &&) noexcept=default;
    VerifiedCompAssemblyAdoption(const VerifiedCompAssemblyAdoption &)=delete;
    const ApplyCompAssembly &edit() const noexcept {return edit_;}
    const Id &project() const noexcept {return project_;}
    const std::filesystem::path &root() const noexcept {return root_;}
    const CompHandlePlanContext &context() const noexcept {return context_;}
    bool ownedBy(ResourceLedger ledger) const noexcept {return ledger.owns(lease_) && edit_.dependencies && edit_.dependencies->ownedBy(ledger);}
  private:
    friend VerifiedCompAssemblyAdoption verifyCompAssemblyAdoption(const std::filesystem::path &,
        const Session &,const CompHandlePlanContext &,const CompPairSelection &,
        const CompAssemblyRequest &,ResourceLedger,std::stop_token);
    VerifiedCompAssemblyAdoption(ResourceLease lease,Id project,std::filesystem::path root,
        CompHandlePlanContext context,ApplyCompAssembly edit)
        :lease_(std::move(lease)),project_(std::move(project)),root_(std::move(root)),
         context_(std::move(context)),edit_(std::move(edit)){}
    ResourceLease lease_;
    Id project_;
    std::filesystem::path root_;
    CompHandlePlanContext context_;
    ApplyCompAssembly edit_;
};
VerifiedCompAssemblyAdoption verifyCompAssemblyAdoption(const std::filesystem::path &,
    const Session &,const CompHandlePlanContext &,const CompPairSelection &,
    const CompAssemblyRequest &,ResourceLedger,std::stop_token={});
} // namespace soundcurrent::daw
