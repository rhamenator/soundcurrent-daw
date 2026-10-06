// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include "project_controller.hpp"
#include <QMainWindow>
#include <QCoreApplication>
#include <vector>
class QLabel;
class QDoubleSpinBox;
class QSlider;
class QAction;
class QTimer;
class QGroupBox;
namespace soundcurrent::daw::ui {
class StudioWindow : public QMainWindow {
  public:
    explicit StudioWindow(QWidget *parent = nullptr);
    void openProject(const std::filesystem::path &);
    bool submitEdit(ProjectCommand); // Shared entry for bindings/UI acceptance.
    std::shared_ptr<const ControllerSnapshot> snapshot() const;
    static QString tr(const char *source, const char *comment = nullptr, int count = -1) {
        return QCoreApplication::translate("StudioWindow", source, comment, count);
    }

  protected:
    void closeEvent(QCloseEvent *) override;

  private:
    ProjectController controller_;
    std::shared_ptr<const ControllerSnapshot> shown_;
    QLabel *project_, *track_, *state_, *notice_;
    QGroupBox *eq_;
    QTimer *timer_;
    QAction *new_, *open_, *save_, *undo_, *redo_;
    struct BandEditors {
        QDoubleSpinBox *frequency = nullptr, *gain = nullptr, *q = nullptr;
        QSlider *slider = nullptr;
        std::optional<ParameterAddress> frequencyAddress, gainAddress, qAddress;
        std::uint64_t frequencyGesture = 0, gainGesture = 0, qGesture = 0, sliderGesture = 0;
    };
    std::vector<BandEditors> bands_;
    std::uint64_t nextGesture_ = 1, lastError_ = 0;
    bool closing_ = false, closeAfterSave_ = false, closeRequested_ = false;
    bool closeSaveSubmitted_ = false;
    std::uint64_t closeBarrier_ = 0;
    void poll();
    void newProject();
    void confirmClose();
    void rebuildBands(const Session &);
    void updateBands(const Session &);
    void edit(std::size_t band, BandParameter, double, bool final = false, bool slider = false);
};
} // namespace soundcurrent::daw::ui
