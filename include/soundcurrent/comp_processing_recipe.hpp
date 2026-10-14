// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include "comp_processing_plan.hpp"
#include "project_store.hpp"
#include <stop_token>
namespace soundcurrent::daw {
inline constexpr std::string_view compProcessingRecipeFormat="sc-comp-processing-recipe-v1";
inline constexpr std::string_view compRegenerationRecipeFormat="sc-comp-processing-recipe-v2";
struct CompRecipeExecutor {
    std::string id="soundcurrent.comp-join-quintic-v1",buildSha256;
    bool operator==(const CompRecipeExecutor &) const=default;
};
struct CompRecipeLimits {
    std::size_t maximumBytes=2*1024*1024,codecBytes=64*1024*1024;
    CompHandlePlanLimits planning;
};
class CompProcessingRecipe {
  public:
    CompProcessingRecipe(CompProcessingRecipe &&) noexcept=default;
    CompProcessingRecipe &operator=(CompProcessingRecipe &&)=delete;
    CompProcessingRecipe(const CompProcessingRecipe &)=delete;
    const CompProcessingPlan &plan() const {return plan_;}
    std::string_view bytes() const noexcept {return bytes_;}
    const std::string &digest() const noexcept {return digest_;}
    const Id &operation() const noexcept {return operation_;}
  private:
    friend CompProcessingRecipe decodeCompProcessingRecipe(std::string_view,const Id &,
        const CompHandlePlanContext &,const CompRecipeExecutor &,std::string_view,ResourceLedger,CompRecipeLimits,std::stop_token);
    CompProcessingRecipe(ResourceLease lease,CompProcessingPlan plan,Id op,std::string bytes,std::string digest)
        :lease_(std::move(lease)),plan_(std::move(plan)),operation_(std::move(op)),bytes_(std::move(bytes)),digest_(std::move(digest)){}
    ResourceLease lease_;
    CompProcessingPlan plan_;
    Id operation_;
    std::string bytes_,digest_;
};
CompProcessingRecipe encodeCompProcessingRecipe(const Session &,const CompHandlePlanContext &,
    const CompProcessingPlan &,const Id &,const CompRecipeExecutor &,ResourceLedger,
    CompRecipeLimits={},std::stop_token={});
// Expected digest/operation/executor and fresh context come from the current trusted
// control owner. Saved bytes are metadata only: no root access or media admission.
CompProcessingRecipe decodeCompProcessingRecipe(std::string_view,const Id &,
    const CompHandlePlanContext &,const CompRecipeExecutor &,std::string_view expectedDigest,ResourceLedger,
    CompRecipeLimits={},std::stop_token={});
// Immutable sidecar publication uses existing owned-file/flush/no-overwrite
// infrastructure. These functions neither launch a worker nor attach media.
std::string persistCompProcessingRecipe(const std::filesystem::path &,const Session &,
    const CompHandlePlanContext &,const CompProcessingPlan &,const Id &,
    const CompRecipeExecutor &,ResourceLedger,CompRecipeLimits={},std::stop_token={});
CompProcessingRecipe loadCompProcessingRecipe(const std::filesystem::path &,const Session &,
    const CompHandlePlanContext &,const Id &,const CompRecipeExecutor &,std::string_view expectedDigest,ResourceLedger,
    CompRecipeLimits={},std::stop_token={});
} // namespace soundcurrent::daw
