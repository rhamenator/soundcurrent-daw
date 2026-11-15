// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include "saved_restoration_record.hpp"
#include "wave_validation.hpp"
namespace soundcurrent::daw {
struct SavedRestorationStageState;
// Serialized non-audio I/O only. Retains the original native audit and freshly
// approved source. A live stage is not controller or publication permission.
class SavedRestorationStage {
  public:
    static SavedRestorationStage prepare(ApprovedMediaFile,const std::filesystem::path &explicitRoot,RestorationAudit,
        const SavedRestorationSourceTicket &,const WaveCheckReport &,std::size_t observedCheckerPid,
        SavedRestorationRecordContext current,Id operation,std::uint64_t maximumBytes,
        WaveValidationLimits = {},std::stop_token = {},const StageObserver & = {});
    SavedRestorationStage(SavedRestorationStage &&) noexcept;
    ~SavedRestorationStage();
    SavedRestorationStage(const SavedRestorationStage &)=delete;
    const SavedRestorationRecord &record() const;
    const std::string &relativePath() const;
    // Rechecks the original six pinned/named facts, native owned transition,
    // source, planned intent and staged bytes. Never rebases the original audit.
    void reverify(std::stop_token = {});
  private:
    friend class DispatchedSavedRestoration;
    MediaCommitOutcome publishForPermit(const std::filesystem::path &,std::stop_token,const StageObserver &);
    explicit SavedRestorationStage(std::unique_ptr<SavedRestorationStageState>);
    std::unique_ptr<SavedRestorationStageState> state_;
};
} // namespace soundcurrent::daw
