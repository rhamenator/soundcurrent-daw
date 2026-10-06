// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include <soundcurrent/recording.hpp>
namespace soundcurrent::daw::ui {
struct RecoveryScanSnapshot {
    std::filesystem::path root;
    std::uint64_t projectEpoch = 0, serial = 0;
    bool running = false, closed = false;
    std::shared_ptr<const RecordingDiscovery> discovery;
    std::optional<ErrorCode> errorCode;
    std::string diagnostic;
};
struct RecoveryScanOptions {
    RecordingDiscoveryOptions discovery;
    std::function<void()> beforeScan;
};
// Non-RT latest-request slot. New requests cancel obsolete scans at I/O boundaries.
// Separate from the audio preparation owner; no graph/device actions.
class RecoveryController {
  public:
    explicit RecoveryController(RecoveryScanOptions = {});
    ~RecoveryController();
    bool scan(std::filesystem::path, std::shared_ptr<const Session>, std::uint64_t projectEpoch);
    void cancel() noexcept;
    void requestShutdown() noexcept;
    std::shared_ptr<const RecoveryScanSnapshot> snapshot() const;

  private:
    struct State;
    std::unique_ptr<State> state_;
};
} // namespace soundcurrent::daw::ui
