// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include "comp_span_protocol.hpp"
#include <array>
namespace soundcurrent::daw {
class CompPairSelection {
  public:
    CompPairSelection(CompPairSelection &&) noexcept=default;
    CompPairSelection &operator=(CompPairSelection &&)=delete;
    CompPairSelection(const CompPairSelection &)=delete;
    const CompProcessingRecipe &recipe() const {return recipe_;}
    const std::array<std::optional<CompSpanRequest>,2> &children() const {return children_;}
    const CompSpanWorkerPolicy &policy() const noexcept {return policy_;}
    const CompRecipeExecutor &recipeExecutor() const noexcept {return executor_;}
    const std::string &workerSha256() const noexcept {return worker_;}
    const std::string &digest() const noexcept {return digest_;}
    std::string_view manifest() const noexcept {return manifest_;}
  private:
    friend CompPairSelection prepareCompPairSelection(CompProcessingRecipe,CompRecipeExecutor,std::string,const std::array<Id,2> &,CompSpanWorkerPolicy,ResourceLedger);
    CompPairSelection(ResourceLease lease,CompProcessingRecipe recipe,std::array<std::optional<CompSpanRequest>,2> children,CompSpanWorkerPolicy policy,CompRecipeExecutor executor,std::string worker,std::string digest,std::string manifest)
        :lease_(std::move(lease)),recipe_(std::move(recipe)),children_(std::move(children)),policy_(policy),executor_(std::move(executor)),worker_(std::move(worker)),digest_(std::move(digest)),manifest_(std::move(manifest)){}
    ResourceLease lease_;
    CompProcessingRecipe recipe_;
    std::array<std::optional<CompSpanRequest>,2> children_;
    CompSpanWorkerPolicy policy_;
    CompRecipeExecutor executor_;
    std::string worker_,digest_,manifest_;
};
CompPairSelection prepareCompPairSelection(CompProcessingRecipe,CompRecipeExecutor,std::string workerSha256,
    const std::array<Id,2> &childOperations,CompSpanWorkerPolicy,ResourceLedger);
// Serialized control/disk work only. No worker launch, cleanup, mutation or adoption.
// Save returned digest in an independent trusted owner reference, not its sidecar.
std::string persistCompPairSelection(const std::filesystem::path &,const Session &,
    const CompHandlePlanContext &,const CompPairSelection &,ResourceLedger,std::stop_token={});
CompPairSelection loadCompPairSelection(const std::filesystem::path &,const Session &,
    const CompHandlePlanContext &,const Id &,std::string_view trustedSelectionDigest,
    ResourceLedger,std::stop_token={});
// Original immutable selection, bound to the caller's recorded digest. This
// validates metadata/requests only, not completed audio or current clip state.
CompPairSelection loadCompPairSelectionSnapshot(const std::filesystem::path &,
    const CompHandlePlanContext &,const Id &,std::string_view trustedSelectionDigest,
    ResourceLedger,std::stop_token={});
// Derive from the accepted selection; register only after sidecar publication.
CompPairSelectionReference referenceCompPairSelection(const Session &,
    const CompHandlePlanContext &,const CompPairSelection &);
CompPairSelection loadReferencedCompPairSelection(const std::filesystem::path &,
    const Session &,const CompHandlePlanContext &,const Id &,ResourceLedger,std::stop_token={});
enum class CompPairChildState { Unneeded, Absent, Active, WaitingForStart, Interrupted, Canceled, Complete, Stale, Invalid };
struct CompPairChildRecovery {
    CompPairChildState state=CompPairChildState::Unneeded;
    bool cancellationRequested=false;
    std::optional<ErrorCode> error;
    std::optional<VerifiedCompSpan> candidate;
};
struct CompPairRecovery {
    std::array<CompPairChildRecovery,2> children;
    bool staleSelection=false;
    bool candidatesReady() const noexcept;
};
// Candidate readiness validates span files and current metadata only. Original
// assembly ancestry is reviewed by high-level assembly publication/verification.
// This result does not authorize canonical adoption.
CompPairRecovery inspectCompPair(const std::filesystem::path &,const Session &,
    const CompHandlePlanContext &,const CompPairSelection &,ResourceLedger,std::stop_token={});
// Verify readiness, fresh model/context and original intent before the start ack.
// Repeated identical control markers are idempotent; differing bytes are refused.
void startCompPairChild(const std::filesystem::path &,const Session &,const CompHandlePlanContext &,
    const CompPairSelection &,unsigned fragment,std::string_view ready,ResourceLedger,std::stop_token={});
// Cancel remains available after the model has changed. Active stays Active until
// reaping; a valid completed marker takes precedence over a late cancel request.
void cancelCompPair(const std::filesystem::path &,const CompPairSelection &,ResourceLedger,std::stop_token={});
} // namespace soundcurrent::daw
