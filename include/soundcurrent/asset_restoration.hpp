// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include "media_staging.hpp"
#include "wave_validation.hpp"

namespace soundcurrent::daw {
struct AssetRestorationContext {
    std::uint64_t projectEpoch=0,modelRevision=0,request=0;
    bool operator==(const AssetRestorationContext &) const=default;
};
struct AssetRestorationState;
// Serialized control/I/O owner only. The caller owns the current accepted model
// through publish(); child reports and saved source paths grant no file access.
class PreparedAssetRestoration {
  public:
    static PreparedAssetRestoration prepare(ApprovedMediaFile &,const std::filesystem::path &projectRoot,
        const Asset &,AssetRestorationContext,std::uint64_t maximumBytes,WaveValidationLimits = {},
        Id operation=Id::generate(),std::stop_token = {},const StageObserver & = {});
    PreparedAssetRestoration(PreparedAssetRestoration &&) noexcept;
    ~PreparedAssetRestoration();
    PreparedAssetRestoration(const PreparedAssetRestoration &)=delete;
    const Id &operation() const;
    const std::string &stagedRelativePath() const;
    std::size_t chargedBytes() const;
    // One attempt. Refuses stale context/asset and existing target names. Returns
    // publication truth even if later flush/cancellation fails. No Session edit,
    // project transaction or durable restoration receipt is claimed by this API.
    MediaCommitOutcome publish(const Asset &current,AssetRestorationContext,
        std::stop_token = {},const StageObserver & = {});
  private:
    explicit PreparedAssetRestoration(std::unique_ptr<AssetRestorationState>);
    std::unique_ptr<AssetRestorationState> state_;
};
} // namespace soundcurrent::daw
