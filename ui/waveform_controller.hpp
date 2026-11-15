// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include "project_controller.hpp"
#include <soundcurrent/waveform_protocol.hpp>
#include <QString>
#include <chrono>

namespace soundcurrent::daw::ui {
struct WaveformSelection {
    WaveformTicket ticket;
    std::filesystem::path root;
    std::string relative;
    bool operator==(const WaveformSelection &) const = default;
};
struct OwnedWaveformSelection {
    ResourceLease lease;
    WaveformSelection value;
    std::chrono::steady_clock::time_point admittedAt;
};
enum class WaveformPhase {Idle, Queued, Running, Complete, Canceled, Stale, Fault, Closed};
struct WaveformSnapshot {
    WaveformPhase phase = WaveformPhase::Idle;
    std::shared_ptr<const OwnedWaveformSelection> selection;
    std::shared_ptr<const Waveform> waveform;
    std::uint64_t childPid = 0;
    std::optional<int> childExit;
    std::optional<ErrorCode> error;
    bool busy = false, closed = false, timedOut = false;
    // A snapshot can outlive its project/view. GUI consumers must match their
    // current selection at use, even after the controller's final freshness check.
    std::shared_ptr<const Waveform> resultFor(const WaveformSelection &current) const {
        return phase == WaveformPhase::Complete && selection && selection->value == current ? waveform : nullptr;
    }
};
struct WaveformControllerOptions {
    ResourceLedger memory{512 * 1024 * 1024, "Waveform controller"};
    QString program;
    std::uint64_t maximumSourceBytes = 1024ULL * 1024 * 1024;
    std::size_t childMemoryBytes = 160 * 1024 * 1024;
    unsigned deadlineMilliseconds = 20000;
    // Required thread-safe, bounded, memory-only selection provider. Return null
    // on replacement/close. Never access GUI objects or perform I/O here.
    std::function<std::optional<WaveformSelection>()> current;
    // Trusted test hooks on the control thread, never project-selected code.
    std::function<void()> beforeSpawn;
    std::function<void(std::span<char>, std::uint64_t)> afterChild;
};
// One admitted operation, no hidden backlog. Qt/process/codec work stays on a
// low-priority control thread. Child and receive/model credits remain owned until
// their respective terminal/released states. Deadline starts at admission and
// initiates termination;
// OS kill/retirement latency and Qt allocator/RSS behavior are not hard bounded.
// No installed GUI integration or media-file watching is implied by this adapter.
class WaveformController {
  public:
    explicit WaveformController(WaveformControllerOptions);
    ~WaveformController(); // Fallback join; GUI must normally wait for closed.
    Admission submit(WaveformSelection);
    void requestCancel() noexcept;
    void requestShutdown() noexcept;
    bool clearResult();
    std::shared_ptr<const WaveformSnapshot> snapshot() const;
  private:
    struct State;
    std::unique_ptr<State> state_;
};
} // namespace soundcurrent::daw::ui
