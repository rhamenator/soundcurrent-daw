// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include "wave_check_controller.hpp"
#include <soundcurrent/restoration_permit.hpp>
#include <atomic>
#include <QMutex>
namespace soundcurrent::daw::ui {
// Immutable admitted checker owners, not a file reopening capability.
struct AssetRestorationInput {
    std::shared_ptr<const WaveCheckSnapshot> checked;
    Id asset;
    std::uint64_t request=0;
};
struct RestorationProcessResult {
    std::optional<Id> operation;
    std::size_t childPid=0;
    std::optional<int> childExit;
    bool permitSent=false,canonicalVerified=false,needsReconciliation=false;
    std::optional<bool> published; // Unknown after ambiguous permission/termination.
    bool receiptPublished=false,postCommitFlushFailed=false;
    unsigned durability=0;
    std::optional<ErrorCode> error;
};
// Owned ProjectController I/O worker only. The control worker freezes all
// mutation/I/O until return, including exception cleanup and child retirement.
RestorationProcessResult runRestorationProcess(const AssetRestorationInput &,
    const Session &,const std::filesystem::path &,RestorationOwnerContext,
    const ControllerOptions &,ResourceLedger,const std::atomic<bool> &closing,
    const std::atomic<bool> &cancel,QMutex &permissionMutex);
SavedStagingCompletion runSavedStagingProcess(const RestorationInspectionRequest &original,
    const std::filesystem::path &root,const std::filesystem::path &source,
    const SavedRestorationSourceTicket &,const WaveCheckSnapshot &,std::uint64_t preparationJob,
    std::uint64_t request,std::uint64_t job,const ControllerOptions &,ResourceLedger,
    const std::atomic<bool> &closing,const std::atomic<bool> &cancel);
}
