// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include "project_controller.hpp"
#include <soundcurrent/wave_report.hpp>
#include <QString>
namespace soundcurrent::daw::ui {
enum class WaveCheckPhase {Idle,Queued,Checking,Complete,Canceled,Fault,Closed};
struct WaveSelection {
    ResourceLease lease; // Retires after the immutable selected names.
    std::filesystem::path root;
    std::string relative;
    std::uint64_t maximumBytes;
    bool selectedFilename;
    WaveSelection(ResourceLease grant,std::filesystem::path path,std::string name,std::uint64_t maximum,bool selected)
        :lease(std::move(grant)),root(std::move(path)),relative(std::move(name)),maximumBytes(maximum),selectedFilename(selected) {}
};
// Bounded identity only: project paths and native evidence are not copied or
// treated as candidate-file authority. The ticket and metadata share the owner ledger.
struct WaveAssetReplacement {
    ResourceLease lease;
    Asset identity;
    AssetReplacementContext context;
    AssetReplacementTicket ticket;
    WaveAssetReplacement(const Asset &,AssetReplacementContext,ResourceLedger);
};
struct WaveCheckSnapshot {
    WaveCheckPhase phase=WaveCheckPhase::Idle;
    std::uint64_t job=0;
    std::shared_ptr<const WaveSelection> selection;
    std::shared_ptr<const WaveCheckReport> report;
    std::shared_ptr<const WaveAssetReplacement> replacement;
    std::size_t childPid=0;
    std::optional<int> childExit;
    std::optional<ErrorCode> error;
    bool busy=false,closed=false,timedOut=false;
};
struct WaveCheckOptions {
    ResourceLedger memory{};
    QString program;
    std::uint64_t maximumSourceBytes=1024ULL*1024*1024;
    unsigned deadlineMilliseconds=20000;
    // Trusted off-thread qualification hooks, never selected by foreign data.
    std::function<void()> beforeSpawn;
    std::function<void(std::string &,std::size_t)> afterChild;
};
// Single flight, control/I/O thread only. Child owns approval/opening of the
// explicitly selected root at job execution, then pins the plain file. A GUI
// path string is not claimed to be an already-open parent directory capability.
class WaveCheckController {
  public:
    explicit WaveCheckController(WaveCheckOptions = {});
    ~WaveCheckController();
    Admission submit(std::filesystem::path approvedRoot,std::string relative,std::uint64_t maximumBytes=0);
    Admission submitReplacement(std::filesystem::path explicitlySelectedFile,std::uint64_t maximumBytes=0);
    // Captures the accepted asset identity; assigns the actual admitted job atomically.
    // Complete means exact content match, not permission to restore a file.
    Admission submitAssetReplacement(std::filesystem::path explicitlySelectedFile,
        const ControllerSnapshot &,const Id &asset,std::uint64_t request,std::uint64_t maximumBytes=0);
    void requestCancel() noexcept;
    void requestShutdown() noexcept;
    bool clearResult();
    std::shared_ptr<const WaveCheckSnapshot> snapshot() const;
  private:
    struct State;
    std::unique_ptr<State> state_;
    Admission enqueue(std::filesystem::path,std::string,std::uint64_t,bool,
        const Asset * = nullptr,AssetReplacementContext = {});
};
// Control-side consumption guard using the CURRENT accepted model and request.
// No file IO or publication; callers must revalidate before any later adoption.
void verifyWaveAssetReplacement(const WaveCheckSnapshot &,const ControllerSnapshot &,
    const Id &asset,std::uint64_t request,ResourceLedger);
} // namespace soundcurrent::daw::ui
