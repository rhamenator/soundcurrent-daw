// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include "project_controller.hpp"
#include <soundcurrent/comp_assembly_job.hpp>

namespace soundcurrent::daw::ui {
enum class CompAssemblyPhase {Idle,Queued,Loading,Publishing,Verifying,Complete,RecoveryRequired,Fault,Canceled};
struct CompAssemblySnapshot {
    CompAssemblyPhase phase=CompAssemblyPhase::Idle;
    std::optional<Id> operation;
    std::shared_ptr<const ControllerSnapshot> project;
    std::shared_ptr<const CompPairSelection> selection;
    std::shared_ptr<const CompAssemblyRequest> request;
    std::shared_ptr<const CompAssemblyRecovery> recovery;
    std::optional<ErrorCode> error;
    bool busy=false,closed=false,canceled=false,stale=false,published=false;
};
struct CompAssemblyControllerOptions {
    ResourceLedger memory{512*1024*1024,"Desktop comp assembly"};
    // Thread-safe immutable snapshots. Owner outlives asynchronous close.
    std::function<std::shared_ptr<const ControllerSnapshot>()> project;
    std::function<void()> afterLoad,afterPublication; // Qualification only.
    CompAssemblyProgress progress; // Qualification only, on disk/control thread.
};
// One bounded queued operation. Requires a saved trusted reference; it never
// edits the model or activates audio. All file/PCM work runs on its own thread.
// Cancellation/deadlines are cooperative library guarantees, not OS isolation.
class CompAssemblyController {
  public:
    explicit CompAssemblyController(CompAssemblyControllerOptions);
    ~CompAssemblyController(); // Fallback join; GUI normally waits for closed.
    CompAssemblyController(const CompAssemblyController &)=delete;
    Admission render(Id); // Only a new absent job; complete jobs are reviewed.
    Admission review(Id); // Read-only recovery; never resumes/replaces files.
    void requestCancel() noexcept;
    void requestShutdown() noexcept;
    bool clearResult();
    std::shared_ptr<const CompAssemblySnapshot> snapshot() const;
  private:
    struct State;std::unique_ptr<State> state_;
    Admission submit(Id,bool);
};
} // namespace soundcurrent::daw::ui
