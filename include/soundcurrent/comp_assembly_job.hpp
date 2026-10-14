// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include "comp_pair_assembly.hpp"
namespace soundcurrent::daw {
inline constexpr std::string_view compAssemblyProtocol="sc-comp-assembly-v1";
struct CompAssemblyJobPolicy {
    CompPairAssemblyLimits assembly;
    std::uint64_t maximumFileBytes=8ULL*1024*1024*1024;
};
class CompAssemblyRequest {
  public:
    CompAssemblyRequest(CompAssemblyRequest &&) noexcept=default;
    CompAssemblyRequest(const CompAssemblyRequest &)=delete;
    const Id &operation() const {return operation_;}
    const auto &outputs() const {return outputs_;}
    const auto &policy() const {return policy_;}
    const auto &executorSha256() const {return executor_;}
    const auto &selectionDigest() const {return selection_;}
    const auto &digest() const {return digest_;}
    std::string_view bytes() const {return bytes_;}
  private:
    friend CompAssemblyRequest prepareCompAssemblyRequest(const Session &,const CompHandlePlanContext &,const CompPairSelection &,Id,const std::array<std::optional<Id>,2> &,std::string,CompAssemblyJobPolicy,ResourceLedger);
    CompAssemblyRequest(ResourceLease credit,Id op,std::array<std::optional<Id>,2> outputs,CompAssemblyJobPolicy policy,std::string executor,std::string selection,std::string digest,std::string bytes)
        :credit_(std::move(credit)),operation_(op),outputs_(std::move(outputs)),policy_(policy),executor_(std::move(executor)),selection_(std::move(selection)),digest_(std::move(digest)),bytes_(std::move(bytes)){}
    ResourceLease credit_;
    Id operation_;
    std::array<std::optional<Id>,2> outputs_;
    CompAssemblyJobPolicy policy_;
    std::string executor_,selection_,digest_,bytes_;
};
// Off GUI/audio. Request digest must be retained by an independent trusted owner.
// No executable path, policy or digest may be recovered by trusting completion.
CompAssemblyRequest prepareCompAssemblyRequest(const Session &,const CompHandlePlanContext &,
    const CompPairSelection &,Id,const std::array<std::optional<Id>,2> &,
    std::string executorSha256,CompAssemblyJobPolicy,ResourceLedger);
CompAssemblyRequest decodeCompAssemblyRequest(std::string_view,std::string_view trustedDigest,
    const Session &,const CompHandlePlanContext &,const CompPairSelection &,ResourceLedger);
// Off GUI/audio. Persist immutable sidecar first, then register/save its returned
// reference through the canonical owner. Removal never deletes job/media files.
CompAssemblyReference referenceCompAssembly(const Session &,const CompHandlePlanContext &,
    const CompPairSelection &,const CompAssemblyRequest &,ResourceLedger);
CompAssemblyReference persistCompAssemblyRequest(const std::filesystem::path &,const Session &,
    const CompHandlePlanContext &,const CompPairSelection &,const CompAssemblyRequest &,
    ResourceLedger,std::stop_token={});
// Uses the independent project reference, never a completion-supplied digest.
CompAssemblyRequest loadReferencedCompAssemblyRequest(const std::filesystem::path &,const Session &,
    const CompHandlePlanContext &,const CompPairSelection &,const Id &,ResourceLedger,std::stop_token={});
// Hash the actual running image using native process identity. Runtime libraries
// still require the application's qualified deployment/dependency contract.
std::string compAssemblyExecutableSha256();
enum class CompAssemblyStage { FileClosed, FilePublished, BeforeCompletion };
using CompAssemblyProgress=std::function<void(CompAssemblyStage,unsigned)>;
// Serialized control/disk operation with bounded payload, cooperative deadlines
// and cancellation. No child-process/OS memory ceiling is claimed. Owns a new job
// and no-overwrite files. Completion is the last commit; failure preserves owned
// evidence and never resumes/replaces an existing job. Callback may throw.
void publishCompAssembly(const std::filesystem::path &,const Session &,const CompHandlePlanContext &,
    const CompPairSelection &,const CompAssemblyRequest &,ResourceLedger,
    CompAssemblyProgress={},std::stop_token={});
class VerifiedCompAssembly {
  public:
    VerifiedCompAssembly(VerifiedCompAssembly &&) noexcept=default;
    VerifiedCompAssembly(const VerifiedCompAssembly &)=delete;
    const auto &outputs() const {return outputs_;}
    const auto &requestDigest() const {return digest_;}
    std::uint64_t comparedSamples() const {return compared_;}
  private:
    friend VerifiedCompAssembly verifyCompAssembly(const std::filesystem::path &,const Session &,const CompHandlePlanContext &,const CompPairSelection &,const CompAssemblyRequest &,ResourceLedger,std::stop_token);
    VerifiedCompAssembly(ResourceLease lease,std::array<std::optional<Asset>,2> outputs,std::string digest,std::uint64_t compared)
        :lease_(std::move(lease)),outputs_(std::move(outputs)),digest_(std::move(digest)),compared_(compared){}
    ResourceLease lease_;
    std::array<std::optional<Asset>,2> outputs_;
    std::string digest_;
    std::uint64_t compared_;
};
// Read-only after writer retirement: exact original intent, full WAV/hash and
// independently recomputed complete PCM, plus separate scope review. This is a
// file-integrity/algorithm result, NOT sonic quality or a model-adoption token.
VerifiedCompAssembly verifyCompAssembly(const std::filesystem::path &,const Session &,const CompHandlePlanContext &,
    const CompPairSelection &,const CompAssemblyRequest &,ResourceLedger,std::stop_token={});
enum class CompAssemblyRecoveryState {Absent,Active,Interrupted,Canceled,Complete,Invalid};
struct CompAssemblyRecovery {
    CompAssemblyRecoveryState state=CompAssemblyRecoveryState::Absent;
    bool cancellationRequested=false;
    std::optional<ErrorCode> error;
    std::optional<VerifiedCompAssembly> result;
};
CompAssemblyRecovery inspectCompAssembly(const std::filesystem::path &,const Session &,const CompHandlePlanContext &,
    const CompPairSelection &,const CompAssemblyRequest &,ResourceLedger,std::stop_token={});
} // namespace soundcurrent::daw
