// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include "project_controller.hpp"
#include <soundcurrent/import_inspection_report.hpp>
#include <QString>
namespace soundcurrent::daw::ui {
enum class InspectionPhase { Idle, Queued, Capturing, Inspecting, Decoding, Complete, Canceled, Fault, Closed };
struct InspectionSnapshot {
    InspectionPhase phase=InspectionPhase::Idle;
    std::uint64_t job=0;
    std::filesystem::path path;
    std::shared_ptr<const ImportInspectionReport> report;
    std::optional<int> childExit;
    std::size_t childPid=0;
    std::optional<ErrorCode> error;
    std::string messageId;
    bool busy=false, closed=false, canceled=false, timedOut=false;
};
struct InspectionOptions {
    ResourceLedger memory{};
    QString program; // Trusted absolute package path; empty chooses app directory.
    std::size_t maximumInputBytes=16*1024*1024, maximumReportBytes=64*1024*1024;
    std::size_t childMemoryBytes=64*1024*1024;
    unsigned deadlineMilliseconds=20000;
    // Off-thread qualification hooks only; no foreign data selects them.
    std::function<void()> beforeSpawn;
    std::function<void(std::string &,std::size_t)> afterChild;
};
// Single admitted request. No GUI waits/I/O/decoding; exact child handle survives
// cancel/deadline until terminal exit is confirmed. Results retain their grants
// across GUI borrowers and may outlive this controller.
class ImportInspectionController {
  public:
    explicit ImportInspectionController(InspectionOptions = {});
    ~ImportInspectionController();
    Admission submit(std::filesystem::path);
    void requestCancel() noexcept;
    void requestShutdown() noexcept;
    std::shared_ptr<const InspectionSnapshot> snapshot() const;
  private:
    struct State;
    std::unique_ptr<State> state_;
};
} // namespace soundcurrent::daw::ui
