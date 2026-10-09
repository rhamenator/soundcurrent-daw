// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include "stretch_controller.hpp"
#include <QDialog>
#include <QCoreApplication>
class QSpinBox;
class QDoubleSpinBox;
class QCheckBox;
class QLabel;
class QPushButton;
namespace soundcurrent::daw::ui {
struct StretchUiState {
    std::shared_ptr<const StretchSnapshot> render;
    QString message;
    bool canRender=false,canApply=false,adopting=false;
};
class StretchDialog : public QDialog {
    Q_DECLARE_TR_FUNCTIONS(StretchDialog)
 public:
    StretchDialog(StretchSettings, QWidget *parent=nullptr);
    std::function<StretchUiState()> read;
    std::function<bool(StretchSettings)> render;
    std::function<bool()> apply;
    std::function<void()> cancel;
    void refresh();
 private:
    QSpinBox *numerator_,*denominator_;
    QDoubleSpinBox *pitch_;
    QCheckBox *formant_;
    QLabel *status_;
    QPushButton *render_,*apply_,*cancel_;
};
} // namespace soundcurrent::daw::ui
