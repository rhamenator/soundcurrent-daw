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
class HistoryResourcesDialog : public QDialog {
    Q_DECLARE_TR_FUNCTIONS(HistoryResourcesDialog)
  public:
    HistoryResourcesDialog(ProjectController &, std::uint64_t &requestSequence,
                           std::function<void(HistoryBudget)> accepted, QWidget *parent);
    void reject() override;

  protected:
    void closeEvent(QCloseEvent *) override;

  private:
    std::uint64_t pending_ = 0;
};
} // namespace soundcurrent::daw::ui
