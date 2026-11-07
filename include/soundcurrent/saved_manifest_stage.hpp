// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include "saved_manifest_source.hpp"
#include "saved_restoration_record.hpp"
#include "wave_validation.hpp"
namespace soundcurrent::daw {
struct SavedManifestStageState;
// Serialized I/O in an isolated worker only. Owns the original native audit,
// fresh source and exclusive stage. No controller or publication permission.
class SavedManifestStage {
  public:
    static SavedManifestStage prepare(ApprovedMediaFile,const std::filesystem::path &,SavedManifestAudit,
        const SavedManifestSourceTicket &,const WaveCheckReport &,std::size_t observedCheckerPid,
        SavedRestorationRecordContext current,Id freshOperation,std::uint64_t maximumBytes,
        WaveValidationLimits = {},std::stop_token = {},const StageObserver & = {});
    SavedManifestStage(SavedManifestStage &&) noexcept;
    ~SavedManifestStage();
    SavedManifestStage(const SavedManifestStage &)=delete;
    const SavedRestorationRecord &record() const;
    const std::string &relativePath() const;
    void reverify(std::stop_token = {});
  private:
    explicit SavedManifestStage(std::unique_ptr<SavedManifestStageState>);
    std::unique_ptr<SavedManifestStageState> state_;
};
} // namespace soundcurrent::daw
