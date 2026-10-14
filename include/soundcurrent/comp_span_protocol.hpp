// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include "comp_processing_recipe.hpp"
#include "comp_span_render.hpp"
namespace soundcurrent::daw {
inline constexpr std::string_view compSpanProtocol="sc-comp-span-v1";
inline constexpr std::size_t compSpanRequestMaximum=4*1024*1024+16384;
struct CompSpanWorkerPolicy {
    std::uint64_t memoryBytes=256ULL*1024*1024,maximumSourceBytes=8ULL*1024*1024*1024,
        maximumOutputBytes=8ULL*1024*1024*1024,deadlineMilliseconds=60000;
};
class CompSpanRequest {
  public:
    CompSpanRequest(CompSpanRequest &&) noexcept=default;
    CompSpanRequest &operator=(CompSpanRequest &&)=delete;
    CompSpanRequest(const CompSpanRequest &)=delete;
    const CompProcessingRecipe &recipe() const {return recipe_;}
    const CompSpanPlan &span() const {return span_;}
    const Id &operation() const noexcept {return operation_;}
    unsigned fragment() const noexcept {return fragment_;}
    const std::string &digest() const noexcept {return digest_;}
    const std::string &executorSha256() const noexcept {return executor_;}
    std::string_view bytes() const noexcept {return bytes_;}
  private:
    friend CompSpanRequest decodeCompSpanRequest(std::string_view,std::string_view,const CompHandlePlanContext &,std::string_view,ResourceLedger);
    CompSpanRequest(ResourceLease lease,CompProcessingRecipe recipe,CompSpanPlan span,Id operation,unsigned fragment,std::string digest,std::string executor,std::string bytes)
        :lease_(std::move(lease)),recipe_(std::move(recipe)),span_(std::move(span)),operation_(std::move(operation)),fragment_(fragment),digest_(std::move(digest)),executor_(std::move(executor)),bytes_(std::move(bytes)){}
    ResourceLease lease_;
    CompProcessingRecipe recipe_;
    CompSpanPlan span_;
    Id operation_;
    unsigned fragment_;
    std::string digest_,executor_,bytes_;
};
CompSpanRequest encodeCompSpanRequest(const CompProcessingRecipe &,const CompRecipeExecutor &,
    unsigned fragment,const Id &childOperation,std::string_view spanBinarySha256,ResourceLedger);
// Expected request digest and executable hash are trusted control arguments.
CompSpanRequest decodeCompSpanRequest(std::string_view,std::string_view expectedDigest,
    const CompHandlePlanContext &,std::string_view actualBinarySha256,ResourceLedger);
std::string compSpanIntent(const CompSpanRequest &,const CompSpanWorkerPolicy &,ResourceLedger);
void verifyCompSpanReady(std::string_view,const CompSpanRequest &,const CompSpanWorkerPolicy &,ResourceLedger);
void admitCompSpanSources(const std::filesystem::path &,const CompSpanRequest &,
    const CompSpanWorkerPolicy &,ResourceLedger,const std::function<void()> &poll);
class VerifiedCompSpan {
  public:
    VerifiedCompSpan(VerifiedCompSpan &&) noexcept=default;
    VerifiedCompSpan &operator=(VerifiedCompSpan &&)=delete;
    const Asset &asset() const noexcept {return asset_;}
    const std::string &requestDigest() const noexcept {return digest_;}
  private:
    friend VerifiedCompSpan verifyOwnedCompSpan(const std::filesystem::path &,const CompSpanRequest &,const CompSpanWorkerPolicy &,ResourceLedger,std::stop_token);
    VerifiedCompSpan(ResourceLease lease,Asset asset,std::string digest):lease_(std::move(lease)),asset_(std::move(asset)),digest_(std::move(digest)){}
    ResourceLease lease_;
    Asset asset_;
    std::string digest_;
};
// After process reaping. Candidate integrity/completion only: no blend/curve,
// scope/adoption, post-SRC representation or sound-quality qualification.
VerifiedCompSpan verifyOwnedCompSpan(const std::filesystem::path &,const CompSpanRequest &,
    const CompSpanWorkerPolicy &,ResourceLedger,std::stop_token={});
} // namespace soundcurrent::daw
