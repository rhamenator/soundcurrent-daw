// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include "manual_recording_controller.hpp"
#include "project_controller.hpp"
#include <QGroupBox>
#include <QCoreApplication>
#include <set>
class QPushButton;
class QLabel;
class QListWidget;
class QSpinBox;
class QComboBox;
class QGridLayout;
namespace soundcurrent::daw::ui {
// GUI owns intent and previews; endpoint, file work and joins belong to the worker.
class ManualRecordingPanel final : public QGroupBox {
  public:
    ManualRecordingPanel(ProjectController &, std::uint64_t &barrierSequence,
                         std::function<bool()> mayPrepare, ManualControlOptions, QWidget *);
    void poll();
    bool busy() const;
    void stop(bool cancel = false);
    void beginClose();
    // -1: user chose to review; 0: still draining; 1: results resolved.
    int closeReadiness();
    void cancelClose();
    void requestShutdown();
    std::shared_ptr<const ManualControlSnapshot> snapshot() const;
    static QString tr(const char *s, const char *comment = nullptr, int n = -1) {
        return QCoreApplication::translate("ManualRecordingPanel", s, comment, n);
    }

  private:
    ProjectController &project_;
    std::uint64_t &barrierSequence_;
    std::function<bool()> mayPrepare_;
    ManualRecordingController worker_;
    QPushButton *prepare_, *play_, *prepareTake_, *punchIn_, *punchOut_, *stop_, *cancel_;
    QPushButton *add_, *addPartial_, *keep_;
    QLabel *status_, *receipt_, *preview_;
    QListWidget *arms_;
    QSpinBox *seconds_, *reserve_;
    QComboBox *groups_;
    QGridLayout *routes_;
    std::vector<QComboBox *> inputs_, outputs_;
    std::optional<ManualControlPreparation> prepared_;
    std::vector<Id> barrierArms_;
    int barrierSeconds_ = 0, barrierReserve_ = 0;
    std::uint64_t barrier_ = 0, pending_ = 0, nextTake_ = 0, activeTake_ = 0;
    std::uint64_t punchPending_ = 0, stopToken_ = 0, epoch_ = 0, errorSeen_ = 0;
    std::uint64_t followed_ = 0, inventoryRevision_ = 0;
    std::vector<PipeWirePort> inventory_;
    using GroupKey = std::pair<std::uint64_t, std::uint64_t>;
    std::set<GroupKey> consumed_;
    std::optional<ManualControlGroup> attaching_;
    std::vector<Id> attachmentAssets_;
    std::uint64_t attachmentCount_ = 0, attachmentRequest_ = 0;
    bool closingIntent_ = false, prompting_ = false;
    bool send(ManualControlCommand);
    void prepare();
    void activate();
    void punch(ManualPunchAction);
    void refreshRoutes(const ControllerSnapshot &, const ManualControlSnapshot &);
    std::vector<PipeWirePort> selectedPorts(bool output) const;
    void setRoute(bool output, std::size_t channel, QComboBox *);
    void refreshGroups(const ManualControlSnapshot &);
    std::optional<ManualControlGroup> selectedGroup() const;
    void adopt(bool partial);
    void keep();
};
} // namespace soundcurrent::daw::ui
