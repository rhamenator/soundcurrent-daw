// SPDX-License-Identifier: GPL-3.0-only
#include "stretch_dialog.hpp"
#include <QSpinBox>
#include <QDoubleSpinBox>
#include <QCheckBox>
#include <QLabel>
#include <QPushButton>
#include <QFormLayout>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QTimer>
#include <QWheelEvent>
#include <QLocale>
#include <cmath>
namespace soundcurrent::daw::ui {
namespace {
template<class Base> class FocusSpin : public Base {
    void wheelEvent(QWheelEvent *event) override {
        if(this->hasFocus())Base::wheelEvent(event);else event->ignore();
    }
};
}
StretchDialog::StretchDialog(StretchSettings value,QWidget *parent):QDialog(parent) {
    setObjectName("clipStretchDialog");setWindowTitle(tr("Clip pitch and stretch"));
    setAttribute(Qt::WA_DeleteOnClose);setModal(false);
    auto *body=new QVBoxLayout(this);auto *form=new QFormLayout;
    numerator_=new FocusSpin<QSpinBox>;denominator_=new FocusSpin<QSpinBox>;
    numerator_->setObjectName("stretchNumerator");denominator_->setObjectName("stretchDenominator");
    for(auto *field:{numerator_,denominator_})field->setRange(1,1000000);
    numerator_->setValue(int(value.timeNumerator));denominator_->setValue(int(value.timeDenominator));
    form->addRow(tr("Duration multiplier numerator"),numerator_);
    form->addRow(tr("Duration multiplier denominator"),denominator_);
    pitch_=new FocusSpin<QDoubleSpinBox>;pitch_->setObjectName("stretchPitch");
    pitch_->setRange(-24,24);pitch_->setDecimals(5);pitch_->setSingleStep(.1);
    pitch_->setValue(double(value.pitchMilliCents)/100000.0);
    form->addRow(tr("Pitch (semitones)"),pitch_);
    formant_=new QCheckBox(tr("Preserve formants"));formant_->setObjectName("stretchFormant");
    formant_->setChecked(value.formantPreserved);form->addRow(formant_);body->addLayout(form);
    auto *help=new QLabel(tr("This dialog stays bound to the clip selected when it opened. Close it before choosing another clip. Duration can be 0.25 to 4 times the source duration. Pitch and duration are independent. Rendering starts from the retained original audio. Linked playback speed remains a separate control. Closing this dialog leaves rendering in the background."));
    help->setWordWrap(true);help->setTextFormat(Qt::PlainText);body->addWidget(help);
    status_=new QLabel;status_->setObjectName("stretchStatus");status_->setWordWrap(true);status_->setTextFormat(Qt::PlainText);body->addWidget(status_);
    auto *buttons=new QHBoxLayout;
    render_=new QPushButton(tr("Render"));render_->setObjectName("renderStretch");
    apply_=new QPushButton(tr("Apply verified result"));apply_->setObjectName("applyStretch");
    cancel_=new QPushButton(tr("Cancel render"));cancel_->setObjectName("cancelStretch");
    auto *close=new QPushButton(tr("Close"));close->setObjectName("closeStretch");
    for(auto *button:{render_,apply_,cancel_,close})buttons->addWidget(button);
    body->addLayout(buttons);
    connect(render_,&QPushButton::clicked,this,[this]{
        numerator_->interpretText();denominator_->interpretText();pitch_->interpretText();
        const StretchSettings settings{std::uint32_t(numerator_->value()),std::uint32_t(denominator_->value()),std::int32_t(std::llround(pitch_->value()*100000.0)),formant_->isChecked()};
        if(render)render(settings);
        refresh();
    });
    connect(apply_,&QPushButton::clicked,this,[this]{if(apply)apply();refresh();});
    connect(cancel_,&QPushButton::clicked,this,[this]{if(cancel)cancel();refresh();});
    connect(close,&QPushButton::clicked,this,&QDialog::close);
    auto *timer=new QTimer(this);connect(timer,&QTimer::timeout,this,&StretchDialog::refresh);timer->start(100);
    resize(540,360);refresh();
}
void StretchDialog::refresh() {
    const auto state=read?read():StretchUiState{};const auto s=state.render;
    const bool busy=s && s->busy;
    for(auto *field:{numerator_,denominator_})field->setEnabled(!busy && !state.adopting);
    pitch_->setEnabled(!busy && !state.adopting);formant_->setEnabled(!busy && !state.adopting);
    const auto ratio=double(numerator_->value())/denominator_->value();
    render_->setEnabled(state.canRender && ratio>=.25 && ratio<=4);
    apply_->setEnabled(state.canApply);cancel_->setEnabled(busy && !s->canceled);
    const bool ambiguous=s && (s->canceled || s->timedOut || s->abnormalExit);
    apply_->setText(ambiguous?tr("Apply reviewed completed result"):tr("Apply verified result"));
    QString text;
    if(state.adopting)text=tr("Applying the verified result as one undoable edit…");
    else if(!state.message.isEmpty())text=state.message;
    else if(s)switch(s->phase){
      case StretchPhase::Idle:text=tr("Choose settings, render, then apply the verified result.");break;
      case StretchPhase::Queued:case StretchPhase::Preparing:text=tr("Preparing the original audio and reserving render resources…");break;
      case StretchPhase::Running:text=tr("Rendering pitch and duration in the background…");break;
      case StretchPhase::Verifying:text=tr("Checking the completed audio before it can be applied…");break;
      case StretchPhase::Complete:text=ambiguous?tr("A complete result was verified after cancellation, a deadline, or an abnormal exit. Review these settings before choosing Apply. No project edit has been made."):tr("Render verified. Choose Apply to replace this clip as one undoable edit.");break;
      case StretchPhase::Canceled:text=tr("Canceled before rendering. No project edit was made.");break;
      case StretchPhase::Fault:text=tr("Rendering could not start. Check the source, helper installation, settings, and available memory. No project edit was made.");break;
      case StretchPhase::RecoveryRequired:text=tr("The renderer stopped without a verified completed result. Its owned job files were retained for inspection. No project edit was made.");break;
    }
    if(ratio<.25 || ratio>4)text=tr("The duration multiplier must be between 0.25 and 4.");
    if(s && s->result && s->selection){
        const auto settings=s->selection->settings;
        text+=QStringLiteral("\n")+tr("Verified result: duration %1/%2, pitch %3 semitones, formants %4.")
            .arg(QLocale().toString(settings.timeNumerator),QLocale().toString(settings.timeDenominator),
                 QLocale().toString(double(settings.pitchMilliCents)/100000.0,'f',5),
                 settings.formantPreserved?tr("preserved"):tr("shifted"));
    }
    status_->setText(text);
}
} // namespace soundcurrent::daw::ui
