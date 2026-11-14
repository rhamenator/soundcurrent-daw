// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include "project_controller.hpp"
#include <soundcurrent/comp_pair_recovery.hpp>
#include <array>
#include <QString>
namespace soundcurrent::daw::ui {
enum class CompPairPhase {Idle,Queued,Preparing,Running,Verifying,Complete,RecoveryRequired,Fault,Canceled};
struct CompPairProcessLog {
    ResourceLease lease;
    std::string output,errors;
    CompPairProcessLog(ResourceLease credit,std::string out,std::string err)
        :lease(std::move(credit)),output(std::move(out)),errors(std::move(err)){}
};
struct CompPairChildProcess {
    std::size_t pid=0;
    std::optional<int> exitCode;
    bool ready=false,abnormal=false,reaped=false;
    std::shared_ptr<const CompPairProcessLog> log;
};
struct CompPairSnapshot {
    CompPairPhase phase=CompPairPhase::Idle;
    std::optional<Id> operation;
    std::shared_ptr<const ControllerSnapshot> project;
    std::shared_ptr<const CompPairSelection> selection;
    std::shared_ptr<const CompPairRecovery> recovery;
    std::array<CompPairChildProcess,2> children;
    std::optional<ErrorCode> error;
    bool busy=false,closed=false,canceled=false,timedOut=false,stale=false;
};
struct CompPairControllerOptions {
    ResourceLedger memory{512*1024*1024,"Paired comp processes"};
    QString program; // Trusted application configuration, never from project state.
    // Thread-safe immutable snapshot provider, invoked on the control worker.
    // The project owner must outlive this controller's asynchronous close.
    std::function<std::shared_ptr<const ControllerSnapshot>()> project;
    std::function<void(unsigned)> beforeSpawn,afterReady,afterChild; // Qualification only.
};
// One bounded operation; at most one child process at a time. Uses saved project
// references. Publishes candidates only, never audio/model adoption. All disk,
// protocol and QProcess work belongs to the control thread, not GUI/audio.
class CompPairController {
 public:
    explicit CompPairController(CompPairControllerOptions);
    ~CompPairController(); // Fallback joins; normal GUI polls closed asynchronously.
    CompPairController(const CompPairController &)=delete;
    Admission render(Id operation); // Render missing children; reuse verified completions.
    Admission review(Id operation); // Read-only recovery, never resumes an interrupted job.
    void requestCancel() noexcept;
    void requestShutdown() noexcept;
    bool clearResult();
    std::shared_ptr<const CompPairSnapshot> snapshot() const;
 private:
    struct State;std::unique_ptr<State> state_;
    Admission submit(Id,bool);
};
} // namespace soundcurrent::daw::ui
