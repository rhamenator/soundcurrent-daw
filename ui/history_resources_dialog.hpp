// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include "project_controller.hpp"
#include <QDialog>
#include <QCoreApplication>
#include <functional>
class QCloseEvent;
namespace soundcurrent::daw::ui {
HistoryBudget loadHistoryPreferences();
void saveHistoryPreferences(HistoryBudget);
struct MemoryPreferences {
    std::size_t totalBytes = 1024 * 1024 * 1024, snapshotBytes = 256 * 1024 * 1024;
    bool operator==(const MemoryPreferences &) const = default;
};
MemoryPreferences loadMemoryPreferences();
void saveMemoryPreferences(MemoryPreferences);
class HistoryResourcesDialog : public QDialog {
    Q_DECLARE_TR_FUNCTIONS(HistoryResourcesDialog)
  public:
    HistoryResourcesDialog(ProjectController &, std::uint64_t &requestSequence,
                           std::function<void(HistoryBudget)> accepted, QWidget *parent,
                           std::function<void(MemoryPreferences)> memoryAccepted = {});
    void reject() override;

  protected:
    void closeEvent(QCloseEvent *) override;

  private:
    std::uint64_t pending_ = 0;
    std::uint64_t memoryPending_ = 0;
};
} // namespace soundcurrent::daw::ui
