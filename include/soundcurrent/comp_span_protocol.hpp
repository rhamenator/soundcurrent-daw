// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include "comp_processing_recipe.hpp"
#include "comp_span_render.hpp"
#include "comp_protected_span.hpp"
#include <variant>
namespace soundcurrent::daw {
inline constexpr std::string_view compSpanProtocol="sc-comp-span-v1";
inline constexpr std::string_view compProtectedSpanProtocol="sc-comp-span-protected-v1";
inline constexpr std::string_view compProtectedHistorySpanProtocol="sc-comp-span-protected-v2";
inline constexpr std::string_view compProtectedHistorySpanProcessorId="soundcurrent.comp-span-protected-rubberband4-r3-v2";
using CompSpanPayload=std::variant<CompSpanPlan,CompProtectedSpanPlan>;
inline constexpr std::string_view compRegenerationSpanProtocol="sc-comp-span-v2";
inline constexpr std::string_view compRegenerationSpanProcessorId="soundcurrent.comp-span-positioned-rubberband4-r3-v2";
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
    const CompSpanPlan &span() const;
    const CompProtectedSpanPlan &protectedSpan() const;
    bool isProtected() const noexcept {return std::holds_alternative<CompProtectedSpanPlan>(span_);}
    const Asset &source() const;
    Frame outputFrames() const;
    std::uint32_t outputSampleRate() const;
    const Id &operation() const noexcept {return operation_;}
    unsigned fragment() const noexcept {return fragment_;}
    const std::string &digest() const noexcept {return digest_;}
    const std::string &executorSha256() const noexcept {return executor_;}
    std::string_view bytes() const noexcept {return bytes_;}
    bool requiresHistory() const noexcept {return history_;}
    std::string_view protocol() const noexcept {return isProtected()?(history_?compProtectedHistorySpanProtocol:compProtectedSpanProtocol):(history_?compRegenerationSpanProtocol:compSpanProtocol);}
    std::string_view processor() const noexcept {return isProtected()?(history_?compProtectedHistorySpanProcessorId:compProtectedSpanProcessorId):(history_?compRegenerationSpanProcessorId:compSpanProcessorId);}
  private:
    friend CompSpanRequest decodeCompSpanRequest(std::string_view,std::string_view,const CompHandlePlanContext &,std::string_view,ResourceLedger);
    CompSpanRequest(ResourceLease lease,CompProcessingRecipe recipe,CompSpanPayload span,Id operation,unsigned fragment,std::string digest,std::string executor,std::string bytes,bool history)
        :lease_(std::move(lease)),recipe_(std::move(recipe)),span_(std::move(span)),operation_(std::move(operation)),fragment_(fragment),digest_(std::move(digest)),executor_(std::move(executor)),bytes_(std::move(bytes)),history_(history){}
    ResourceLease lease_;
    CompProcessingRecipe recipe_;
    CompSpanPayload span_;
    Id operation_;
    unsigned fragment_;
    std::string digest_,executor_,bytes_;
    bool history_=false;
};
CompSpanRequest encodeCompSpanRequest(const CompProcessingRecipe &,const CompRecipeExecutor &,
    unsigned fragment,const Id &childOperation,std::string_view spanBinarySha256,ResourceLedger);
// Explicit opt-in; the legacy encoder never silently selects protected DSP.
CompSpanRequest encodeProtectedCompSpanRequest(const CompProcessingRecipe &,const CompRecipeExecutor &,
    unsigned fragment,const Id &childOperation,std::string_view spanBinarySha256,ResourceLedger);
struct CompRequestedSpanReport {Frame writtenFrames=0;double floatPeak=0;};
CompRequestedSpanReport renderRequestedCompSpan(const CompSpanRequest &,const CompSpanRead &,
    const CompSpanWrite &,ResourceLedger,const std::function<void()> &poll);
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
// scope/adoption, historical-job freshness, post-SRC representation or quality.
// Uniform/protected history consumers additionally call verifyCompSpanHistory;
// retained completion and file integrity are not permanent history authority.
VerifiedCompSpan verifyOwnedCompSpan(const std::filesystem::path &,const CompSpanRequest &,
    const CompSpanWorkerPolicy &,ResourceLedger,std::stop_token={});
} // namespace soundcurrent::daw
