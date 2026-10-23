// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include "comp_assembly_job.hpp"
#include <memory>
namespace soundcurrent::daw {
// Sidecars are durable; canonical references are not registered or saved yet.
// Trusted caller supplies the helper identity, never a project-selected path.
class PreparedCompTransition {
  public:
    PreparedCompTransition(PreparedCompTransition &&) noexcept=default;
    PreparedCompTransition(const PreparedCompTransition &)=delete;
    const CompPairSelection &selection() const noexcept {return *selection_;}
    const CompAssemblyRequest &request() const noexcept {return *request_;}
    const CompPairSelectionReference &pairReference() const noexcept {return pairReference_;}
    const CompAssemblyReference &assemblyReference() const noexcept {return assemblyReference_;}
    const std::filesystem::path &root() const noexcept {return root_;}
    const Id &project() const noexcept {return project_;}
    const CompHandlePlanContext &context() const noexcept {return context_;}
    bool ownedBy(ResourceLedger ledger) const noexcept {return ledger.owns(lease_);}
  private:
    friend PreparedCompTransition prepareOwnedCompTransition(const std::filesystem::path &,
        const Session &,const CompHandlePlanContext &,const PrepareCompTransition &,Frame,
        std::string,CompSpanWorkerPolicy,CompAssemblyJobPolicy,ResourceLedger,std::stop_token);
    PreparedCompTransition(ResourceLease lease,std::filesystem::path root,Id project,
        CompHandlePlanContext context,std::shared_ptr<const CompPairSelection> selection,
        std::shared_ptr<const CompAssemblyRequest> request,CompPairSelectionReference pairReference,
        CompAssemblyReference assemblyReference)
        :lease_(std::move(lease)),root_(std::move(root)),project_(std::move(project)),context_(std::move(context)),
         selection_(std::move(selection)),request_(std::move(request)),pairReference_(std::move(pairReference)),
         assemblyReference_(std::move(assemblyReference)){}
    ResourceLease lease_;
    std::filesystem::path root_;
    Id project_;
    CompHandlePlanContext context_;
    std::shared_ptr<const CompPairSelection> selection_;
    std::shared_ptr<const CompAssemblyRequest> request_;
    CompPairSelectionReference pairReference_;
    CompAssemblyReference assemblyReference_;
};
// Serialized control/disk work. Generates fresh operation IDs, publishes only
// immutable request sidecars, and preflights paired canonical registration.
// Caller must register against the live owner, save explicitly, then render.
PreparedCompTransition prepareOwnedCompTransition(const std::filesystem::path &,
    const Session &,const CompHandlePlanContext &,const PrepareCompTransition &,Frame maximumBlendFrames,
    std::string workerSha256,CompSpanWorkerPolicy,CompAssemblyJobPolicy,ResourceLedger,std::stop_token={});
} // namespace soundcurrent::daw
