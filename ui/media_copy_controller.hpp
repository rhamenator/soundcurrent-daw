// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include "wave_check_controller.hpp"
#include <soundcurrent/media_copy_protocol.hpp>
namespace soundcurrent::daw::ui {
enum class MediaCopyPhase {Idle,Queued,Preparing,Running,Committed,Planned,NoEvidence,Canceled,Fault,RecoveryRequired,Closed};
struct MediaCopySelection {
    ResourceLease lease;
    std::shared_ptr<const ImportInspectionReport> inspection;
    std::shared_ptr<const WaveCheckSnapshot> checked;
    std::filesystem::path destination;
    Id operation;
    std::size_t property=0;
    std::uint64_t maximumBytes=0;
    bool recover=false;
    MediaCopySelection(ResourceLease grant,std::filesystem::path target,Id id)
        :lease(std::move(grant)),destination(std::move(target)),operation(std::move(id)) {}
};
struct MediaCopySnapshot {
    MediaCopyPhase phase=MediaCopyPhase::Idle;
    std::shared_ptr<const MediaCopySelection> selection;
    std::shared_ptr<const MediaProvenance> provenance;
    std::size_t childPid=0;
    std::optional<int> childExit;
    std::optional<ErrorCode> error;
    unsigned durability=0;
    bool busy=false,closed=false,canceled=false,timedOut=false,postCommitFlushFailed=false;
};
struct MediaCopyOptions {
    ResourceLedger memory{};
    QString program;
    std::size_t childMemoryBytes=64*1024*1024;
    unsigned deadlineMilliseconds=60000;
    std::function<void()> beforeSpawn;
    std::function<void(const MediaCopyRequestData &)> afterRequestPrepared;
    std::function<void(std::string &,std::size_t)> afterChild;
};
// A stopped/failed child with possible destination access requires recovery.
// No result absence, exit code or canceled flag proves lack of publication.
// All I/O/binding/codec/child operations are on this control worker, not GUI/RT.
class MediaCopyController {
  public:
    explicit MediaCopyController(MediaCopyOptions={});
    ~MediaCopyController();
    Admission copy(std::shared_ptr<const ImportInspectionReport>,std::size_t property,
        std::shared_ptr<const WaveCheckSnapshot>,std::filesystem::path destination);
    Admission recover(std::filesystem::path destination,Id,std::uint64_t maximumBytes);
    void requestCancel() noexcept;
    void requestShutdown() noexcept;
    bool clearResult();
    std::shared_ptr<const MediaCopySnapshot> snapshot() const;
  private:
    struct State;
    std::unique_ptr<State> state_;
    Admission enqueue(std::shared_ptr<MediaCopySelection>);
};
} // namespace soundcurrent::daw::ui
