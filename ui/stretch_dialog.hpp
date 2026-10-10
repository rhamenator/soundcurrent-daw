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
class QTableWidget;
namespace soundcurrent::daw::ui {
struct StretchUiState {
    std::shared_ptr<const StretchSnapshot> render;
    QString message;
    bool canRender=false,canApply=false,adopting=false;
    bool canAudition=false,audition=false,auditionReady=false;
};
class StretchDialog : public QDialog {
    Q_DECLARE_TR_FUNCTIONS(StretchDialog)
 public:
    StretchDialog(StretchSettings, QWidget *parent=nullptr,std::optional<StretchContext> = {},const std::optional<WarpSettings> & = {},ResourceLedger = ResourceLedger{});
    ~StretchDialog() override;
    void setContextBounds(Frame before,Frame after);
    void setWarpBounds(Frame rawFrames,bool integerOrigin);
    std::function<StretchUiState()> read;
    std::function<bool(StretchSettings,std::optional<StretchContext>,const std::optional<WarpSettings> &)> render;
    std::function<bool()> audition;
    std::function<void()> stopAudition;
    std::function<bool()> apply;
    std::function<void()> cancel;
    void refresh();
 private:
    ResourceLedger markerMemory_;
    ResourceLease markerLease_;
    QSpinBox *numerator_,*denominator_;
    QSpinBox *before_,*after_;
    QDoubleSpinBox *pitch_;
    QCheckBox *formant_,*context_,*warp_;
    QTableWidget *markers_;
    QPushButton *addMarker_,*removeMarker_,*audition_,*stopAudition_;
    WarpProtection protection_;
    Frame rawFrames_=0;
    bool integerOrigin_=false;
    void appendMarker(const WarpAnchor &);
    bool inputsMatch(const std::shared_ptr<const StretchSelection> &);
    std::shared_ptr<const StretchSelection> comparedSelection_;
    bool inputsDirty_=true,matchedInputs_=false,comparisonUnavailable_=false;
    QLabel *status_;
    QPushButton *render_,*apply_,*cancel_;
};
} // namespace soundcurrent::daw::ui
