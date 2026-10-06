// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include "project_controller.hpp"
#include <soundcurrent/export.hpp>
namespace soundcurrent::daw::ui {
struct ExportRequest {
    explicit ExportRequest(ExportSpec value) : spec(std::move(value)) {}
    ExportSpec spec;
    std::filesystem::path root, destination;
    std::shared_ptr<const Session> session;
    std::uint64_t modelRevision = 0;
};
enum class ExportPhase {
    Idle,
    Queued,
    Inspecting,
    AwaitingConfirmation,
    Rendering,
    Complete,
    Canceled,
    Fault,
    Closing,
    Closed
};
struct ExportSnapshot {
    ExportPhase phase = ExportPhase::Idle;
    std::uint64_t job = 0, modelRevision = 0, errorSerial = 0;
    std::filesystem::path root, destination;
    std::optional<Id> projectId, trackId;
    Frame written = 0, maximum = 0;
    std::optional<ExportDestination> replacement;
    std::shared_ptr<const ExportResult> result;
    std::optional<ErrorCode> errorCode;
    std::string diagnostic;
    bool busy = false, closed = false, cancelRequested = false;
};
struct ExportControllerOptions {
    // Worker-only qualification hooks. Normal dispatch always uses the real export core.
    std::function<void()> beforeInspect;
    std::function<void(const ExportRequest &)> beforeRender;
    ExportOptions render;
};
// One admitted job, including inspection/consent/render. No overwritten queued jobs.
// GUI only submits/polls/decides. Cancellation/close bypass the single-flight admission.
class ExportController {
  public:
    explicit ExportController(ExportControllerOptions = {});
    ~ExportController();
    ExportController(const ExportController &) = delete;
    Admission submit(ExportRequest);
    bool confirm(std::uint64_t job, bool replace);
    void requestCancel() noexcept;
    void requestShutdown() noexcept;
    std::shared_ptr<const ExportSnapshot> snapshot() const;

  private:
    struct State;
    std::unique_ptr<State> state_;
};
} // namespace soundcurrent::daw::ui
