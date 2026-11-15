// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include "comp_processing_recipe.hpp"
#include <array>
namespace soundcurrent::daw {
struct CompMediaReviewLimits {
    std::uint64_t maximumSourceBytes=8ULL*1024*1024*1024;
    std::uint64_t maximumResultBytes=8ULL*1024*1024*1024;
    std::uint64_t deadlineMilliseconds=60000;
};
struct CompMediaReviewCounts {
    std::uint64_t physicalSamplesCompared=0,projectSamplesCompared=0;
};
// Independent disk/control review, not a worker-completion or model-edit token.
// Files must stay immutable under the existing owned-filesystem contract.
class ReviewedCompMedia {
  public:
    ReviewedCompMedia(ReviewedCompMedia &&) noexcept=default;
    ReviewedCompMedia &operator=(ReviewedCompMedia &&)=delete;
    ReviewedCompMedia(const ReviewedCompMedia &)=delete;
    const auto &outputs() const noexcept {return outputs_;}
    const auto &counts() const noexcept {return counts_;}
    const CompHandlePlanContext &context() const noexcept {return context_;}
    const std::string &recipeDigest() const noexcept {return digest_;}
  private:
    friend ReviewedCompMedia reviewCompMedia(const std::filesystem::path &,const Session &,
        const CompHandlePlanContext &,const CompProcessingRecipe &,
        const std::array<std::optional<Asset>,2> &,ResourceLedger,CompMediaReviewLimits,std::stop_token);
    ReviewedCompMedia(ResourceLease lease,std::string digest,CompHandlePlanContext context,std::array<std::optional<Asset>,2> outputs,CompMediaReviewCounts counts)
        :lease_(std::move(lease)),digest_(std::move(digest)),context_(std::move(context)),outputs_(std::move(outputs)),counts_(counts){}
    ResourceLease lease_;
    std::string digest_;
    CompHandlePlanContext context_;
    std::array<std::optional<Asset>,2> outputs_;
    CompMediaReviewCounts counts_;
};
// Hash/validate actual raw/current/nominal/retained files and declared companion
// bytes (companion semantics require their own native qualification). Physical results must
// retain decoded current PCM outside the exact inner blend intervals; both
// domains must retain original project PCM outside the requested transition.
// Output paths are media/comp-results/<recipe operation>/<asset ID>.wav.
// This checks integrity/scope, not quality, curve correctness, producer identity,
// job intent/completion ownership, or suitability for automatic adoption.
ReviewedCompMedia reviewCompMedia(const std::filesystem::path &,const Session &,
    const CompHandlePlanContext &,const CompProcessingRecipe &,
    const std::array<std::optional<Asset>,2> &,ResourceLedger,
    CompMediaReviewLimits={},std::stop_token={});
} // namespace soundcurrent::daw
