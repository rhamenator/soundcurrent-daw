// SPDX-License-Identifier: GPL-3.0-only
#include "stretch_dialog.hpp"
#include <QSpinBox>
#include <QTableWidget>
#include <QHeaderView>
#include <QStyledItemDelegate>
#include <QLineEdit>
#include <QScrollArea>
#include <QScreen>
#include <charconv>
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
#include <algorithm>
namespace soundcurrent::daw::ui {
namespace {
class MarkerDelegate : public QStyledItemDelegate {
    QWidget *createEditor(QWidget *parent,const QStyleOptionViewItem &option,const QModelIndex &index) const override {
        auto *editor=QStyledItemDelegate::createEditor(parent,option,index);
        if(auto *line=qobject_cast<QLineEdit *>(editor))line->setMaxLength(80);
        return editor;
    }
};
template<class Base> class FocusSpin : public Base {
    void wheelEvent(QWheelEvent *event) override {
        if(this->hasFocus())Base::wheelEvent(event);else event->ignore();
    }
};
QString coordinate(SourcePosition p){
    auto text=QString::number(p.frame);
    if(p.fraction)text+=QStringLiteral("+")+QString::number(qulonglong(p.fraction))+QStringLiteral("/")+QString::number(qulonglong(p.denominator));
    return text;
}
SourcePosition coordinate(const QString &text){
    const auto bytes=text.toLatin1();const std::string_view value(bytes.constData(),std::size_t(bytes.size()));
    auto number=[](std::string_view value){std::uint64_t n=0;const auto parsed=std::from_chars(value.data(),value.data()+value.size(),n);
        if(value.empty() || parsed.ec!=std::errc{} || parsed.ptr!=value.data()+value.size())throw ProjectError(ErrorCode::InvalidParameter,"Use exact source-frame coordinates");
        return n;};
    const auto plus=value.find('+');const auto frame=number(value.substr(0,plus));
    if(frame>1000000000)throw ProjectError(ErrorCode::InvalidParameter,"Marker frame exceeds admission");
    SourcePosition p{Frame(frame),0,1};
    if(plus!=std::string_view::npos){const auto slash=value.find('/',plus+1);if(slash==std::string_view::npos)throw ProjectError(ErrorCode::InvalidParameter,"Expected frame+fraction/denominator");p.fraction=number(value.substr(plus+1,slash-plus-1));p.denominator=number(value.substr(slash+1));}
    return scaleSourcePosition(p,1,1);
}
}
StretchDialog::StretchDialog(StretchSettings value,QWidget *parent,std::optional<StretchContext> region,const std::optional<WarpSettings> &warp,ResourceLedger memory):QDialog(parent),markerMemory_(std::move(memory)),markerLease_(markerMemory_.reserve(16384)) {
    setObjectName("clipStretchDialog");setWindowTitle(tr("Clip pitch and stretch"));
    setAttribute(Qt::WA_DeleteOnClose);setModal(false);
    auto *outer=new QVBoxLayout(this);auto *scroll=new QScrollArea;scroll->setWidgetResizable(true);
    auto *content=new QWidget;auto *body=new QVBoxLayout(content);scroll->setWidget(content);outer->addWidget(scroll);
    auto *form=new QFormLayout;
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
    formant_->setChecked(value.formantPreserved);form->addRow(formant_);
    context_=new QCheckBox(tr("Use neighboring source context"));context_->setObjectName("stretchContextEnabled");context_->setChecked(bool(region));form->addRow(context_);
    before_=new FocusSpin<QSpinBox>;after_=new FocusSpin<QSpinBox>;
    before_->setObjectName("stretchContextBefore");after_->setObjectName("stretchContextAfter");
    for(auto *field:{before_,after_})field->setRange(0,1000000000);
    before_->setValue(int(region?region->before:0));after_->setValue(int(region?region->after:0));
    form->addRow(tr("Context before (source frames)"),before_);form->addRow(tr("Context after (source frames)"),after_);body->addLayout(form);
    protection_=warp?warp->protection:WarpProtection{};
    warp_=new QCheckBox(tr("Experimental protected stretch markers"));warp_->setObjectName("stretchWarpEnabled");warp_->setChecked(bool(warp));body->addWidget(warp_);
    markers_=new QTableWidget(0,3);markers_->setObjectName("stretchMarkers");markers_->setAccessibleName(tr("Stretch markers in retained original source frames"));
    markers_->setItemDelegate(new MarkerDelegate);markers_->itemDelegate()->setParent(markers_);
    markers_->setHorizontalHeaderLabels({tr("Marker"),tr("Source position"),tr("Target position")});
    markers_->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);markers_->setMinimumHeight(130);markers_->setMaximumHeight(220);body->addWidget(markers_);
    auto *markerActions=new QHBoxLayout;addMarker_=new QPushButton(tr("Add marker"));removeMarker_=new QPushButton(tr("Remove selected marker"));
    addMarker_->setObjectName("addStretchMarker");removeMarker_->setObjectName("removeStretchMarker");markerActions->addWidget(addMarker_);markerActions->addWidget(removeMarker_);body->addLayout(markerActions);
    if(warp)for(const auto &m:warp->markers)appendMarker(m);
    auto *warpHelp=new QLabel(tr("Markers use positions relative to the retained original span, at its physical sample rate. Moving a target protects the attack around that marker. Source and target positions must remain ordered. This first renderer requires integer positions, zero pitch and no neighboring context. Sustained multichannel phase quality is still unqualified."));
    warpHelp->setWordWrap(true);warpHelp->setTextFormat(Qt::PlainText);body->addWidget(warpHelp);
    auto *policy=new QLabel(tr("Protected region: %1 source frames before and %2 after each marker; %3-frame blending boundaries. Fades follow the visible clip length.").arg(QLocale().toString(qlonglong(protection_.before)),QLocale().toString(qlonglong(protection_.after)),QLocale().toString(qlonglong(protection_.halo))));policy->setWordWrap(true);body->addWidget(policy);
    connect(addMarker_,&QPushButton::clicked,this,[this]{
        if(markers_->rowCount()>=1023 || rawFrames_<=protection_.before+protection_.after)return;
        const Frame source=std::clamp<Frame>(rawFrames_/2,protection_.before,rawFrames_-protection_.after);
        const auto output=scaleSourcePosition({source,0,1},std::uint64_t(numerator_->value()),std::uint64_t(denominator_->value()));
        try {appendMarker({Id::generate(),{source,0,1},output});refresh();}
        catch(const ProjectError &){status_->setText(tr("Not enough Project resources to add a marker. No marker was added."));}
    });
    connect(removeMarker_,&QPushButton::clicked,this,[this]{if(markers_->currentRow()>=0){markers_->removeRow(markers_->currentRow());markerLease_.resize(16384+std::size_t(markers_->rowCount())*8192);}refresh();});
    connect(warp_,&QCheckBox::toggled,this,[this](bool on){if(on){pitch_->setValue(0);formant_->setChecked(true);context_->setChecked(false);refresh();if(!markers_->rowCount())addMarker_->click();}refresh();});
    auto *help=new QLabel(tr("This dialog stays bound to the clip selected when it opened. Close it before choosing another clip. Duration can be 0.25 to 4 times the source duration. Pitch and duration are independent. Rendering starts from the retained original audio. Linked playback speed remains a separate control. Closing this dialog leaves rendering in the background."));
    help->setWordWrap(true);help->setTextFormat(Qt::PlainText);body->addWidget(help);
    auto *contextHelp=new QLabel(tr("Context extends the retained original span with real neighboring audio at the source sample rate. The visible duration follows the requested multiplier. Prepare audition, choose playback outputs and play the verified result before Apply. Stop audition to apply it as one undoable edit."));
    contextHelp->setWordWrap(true);contextHelp->setTextFormat(Qt::PlainText);body->addWidget(contextHelp);
    status_=new QLabel;status_->setObjectName("stretchStatus");status_->setWordWrap(true);status_->setTextFormat(Qt::PlainText);body->addWidget(status_);
    auto *buttons=new QHBoxLayout;
    render_=new QPushButton(tr("Render"));render_->setObjectName("renderStretch");
    apply_=new QPushButton(tr("Apply verified result"));apply_->setObjectName("applyStretch");
    cancel_=new QPushButton(tr("Cancel render"));cancel_->setObjectName("cancelStretch");
    auto *close=new QPushButton(tr("Close"));close->setObjectName("closeStretch");
    audition_=new QPushButton(tr("Prepare audition"));audition_->setObjectName("auditionStretch");
    stopAudition_=new QPushButton(tr("Stop audition"));stopAudition_->setObjectName("stopStretchAudition");
    for(auto *button:{render_,audition_,stopAudition_,apply_})buttons->addWidget(button);
    outer->addLayout(buttons);auto *closing=new QHBoxLayout;closing->addWidget(cancel_);closing->addStretch();closing->addWidget(close);outer->addLayout(closing);
    connect(render_,&QPushButton::clicked,this,[this]{
        numerator_->interpretText();denominator_->interpretText();pitch_->interpretText();before_->interpretText();after_->interpretText();
        const StretchSettings settings{std::uint32_t(numerator_->value()),std::uint32_t(denominator_->value()),std::int32_t(std::llround(pitch_->value()*100000.0)),formant_->isChecked()};
        const auto region=context_->isChecked()?std::optional(StretchContext{before_->value(),after_->value()}):std::nullopt;
        try {
            std::optional<WarpSettings> warp;
            if(warp_->isChecked()){
                warp.emplace();warp->protection=protection_;
                for(int row=0;row<markers_->rowCount();++row)warp->markers.push_back({Id(markers_->item(row,0)->data(Qt::UserRole).toString().toStdString()),coordinate(markers_->item(row,1)->text()),coordinate(markers_->item(row,2)->text())});
                std::sort(warp->markers.begin(),warp->markers.end(),[](const auto &a,const auto &b){return sourcePositionLess(a.source,b.source);});
                validateWarpSettings(*warp);
                for(const auto &m:warp->markers)if(m.source.fraction || m.output.fraction)throw ProjectError(ErrorCode::UnsupportedSchema,"This acoustic renderer requires integer markers");
            }
            if(render)render(settings,region,warp);
        }catch(const ProjectError &){status_->setText(tr("Check marker positions: use integer source frames, ordered targets and nonoverlapping protected regions. No project edit was made."));return;}
        refresh();
    });
    connect(audition_,&QPushButton::clicked,this,[this]{if(audition)audition();refresh();});
    connect(stopAudition_,&QPushButton::clicked,this,[this]{if(stopAudition)stopAudition();refresh();});
    connect(apply_,&QPushButton::clicked,this,[this]{if(apply)apply();refresh();});
    connect(cancel_,&QPushButton::clicked,this,[this]{if(cancel)cancel();refresh();});
    connect(close,&QPushButton::clicked,this,&QDialog::close);
    connect(context_,&QCheckBox::toggled,this,&StretchDialog::refresh);
    auto *timer=new QTimer(this);connect(timer,&QTimer::timeout,this,&StretchDialog::refresh);timer->start(100);
    const auto available=screen()->availableGeometry();resize(std::min(760,available.width()),std::min(680,available.height()));refresh();
}
StretchDialog::~StretchDialog(){delete markers_;}
void StretchDialog::appendMarker(const WarpAnchor &marker){
    if(markers_->rowCount()>=1023)throw ProjectError(ErrorCode::ResourceLimit,"Marker editor exceeds row admission");
    const auto row=markers_->rowCount();markerLease_.resize(16384+std::size_t(row+1)*8192);markers_->insertRow(row);
    auto *identity=new QTableWidgetItem(QString::fromStdString(marker.id.str().substr(0,8)));identity->setData(Qt::UserRole,QString::fromStdString(marker.id.str()));identity->setToolTip(QString::fromStdString(marker.id.str()));identity->setFlags(identity->flags() & ~Qt::ItemIsEditable);markers_->setItem(row,0,identity);
    markers_->setItem(row,1,new QTableWidgetItem(coordinate(marker.source)));markers_->setItem(row,2,new QTableWidgetItem(coordinate(marker.output)));
}
void StretchDialog::setWarpBounds(Frame frames,bool integerOrigin){rawFrames_=frames;integerOrigin_=integerOrigin;refresh();}
void StretchDialog::setContextBounds(Frame before,Frame after){
    before_->setMaximum(int(std::clamp<Frame>(before,0,1000000000)));after_->setMaximum(int(std::clamp<Frame>(after,0,1000000000)));refresh();
}
void StretchDialog::refresh() {
    const auto state=read?read():StretchUiState{};const auto s=state.render;
    const bool busy=s && s->busy;
    for(auto *field:{numerator_,denominator_})field->setEnabled(!busy && !state.adopting && !state.audition);
    const bool editing=!busy && !state.adopting && !state.audition;
    pitch_->setEnabled(editing && !warp_->isChecked());formant_->setEnabled(editing && !warp_->isChecked());
    warp_->setEnabled(editing);markers_->setEnabled(editing && warp_->isChecked());
    addMarker_->setEnabled(editing && warp_->isChecked() && markers_->rowCount()<1023 && rawFrames_>protection_.before+protection_.after);
    removeMarker_->setEnabled(editing && warp_->isChecked() && markers_->rowCount()>0);
    context_->setEnabled(editing && !warp_->isChecked());
    for(auto *field:{before_,after_})field->setEnabled(editing && context_->isChecked());
    const auto ratio=double(numerator_->value())/denominator_->value();
    render_->setEnabled(state.canRender && ratio>=.25 && ratio<=4 && (!warp_->isChecked() || (integerOrigin_ && markers_->rowCount()>0)));
    audition_->setEnabled(state.canAudition && (!state.audition || state.auditionReady));
    audition_->setText(state.auditionReady?tr("Play audition"):tr("Prepare audition"));stopAudition_->setEnabled(state.audition);
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
    if(warp_->isChecked() && !integerOrigin_)text=tr("This protected renderer requires an integer retained raw origin. Constant pitch/stretch remains available for this clip.");
    if(ratio<.25 || ratio>4)text=tr("The duration multiplier must be between 0.25 and 4.");
    if(s && s->result && s->selection){
        const auto settings=s->selection->settings;
        text+=QStringLiteral("\n")+tr("Verified result: duration %1/%2, pitch %3 semitones, formants %4.")
            .arg(QLocale().toString(settings.timeNumerator),QLocale().toString(settings.timeDenominator),
                 QLocale().toString(double(settings.pitchMilliCents)/100000.0,'f',5),
                 settings.formantPreserved?tr("preserved"):tr("shifted"));
        if(s->selection->warp)text+=QStringLiteral("\n")+tr("Verified protected marker plan: %n marker(s).",nullptr,int(s->selection->warp->markers.size()));
        if(s->selection->context)text+=QStringLiteral("\n")+tr("Verified source context: %1 frames before, %2 frames after.")
            .arg(QLocale().toString(qlonglong(s->selection->context->before)),QLocale().toString(qlonglong(s->selection->context->after)));
    }
    status_->setText(text);
}
} // namespace soundcurrent::daw::ui
