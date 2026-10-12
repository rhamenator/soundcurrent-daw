// SPDX-License-Identifier: GPL-3.0-only
#include "native_monitor_panel.hpp"
#include <QPushButton>
#include <QLabel>
#include <QSpinBox>
#include <QCheckBox>
#include <QGridLayout>
#include <QTimer>
#include <QCloseEvent>
#include <QSignalBlocker>
#include <limits>
#include <algorithm>

namespace soundcurrent::daw::ui {
namespace {
struct OwnedPreparation {ResourceLease lease;NativeMonitorPreparation value;};
QString deviceName(const std::vector<AudioPort> &ports) {
    if(ports.empty())return {};
    return QString::fromUtf8(ports.front().nodeName.data(),qsizetype(ports.front().nodeName.size()));
}
}
NativeMonitorPanel::NativeMonitorPanel(NativeMonitorControllerOptions o,QWidget *parent,ResourceLedger memory)
    :QGroupBox(parent),memory_(std::move(memory)),widgetCredit_(memory_.reserve(32768)),controller_(std::move(o)) {
    setTitle(tr("Recording"));setObjectName("nativeRecordingPanel");
    auto *layout=new QGridLayout(this);
    devices_=new QLabel(this);devices_->setTextFormat(Qt::PlainText);devices_->setWordWrap(true);
    layout->addWidget(devices_,0,0,1,5);
    seconds_=new QSpinBox(this);seconds_->setRange(1,3600);seconds_->setValue(10);seconds_->setObjectName("nativeMonitorSeconds");
    auto *duration=new QLabel(tr("Monitoring length (seconds)"),this);duration->setBuddy(seconds_);layout->addWidget(duration,1,0,1,2);layout->addWidget(seconds_,1,2);
    eq_=new QCheckBox(tr("Monitor EQ"),this);eq_->setObjectName("nativeMonitorEq");layout->addWidget(eq_,1,3,1,2);
    prepare_=new QPushButton(tr("Prepare"),this);start_=new QPushButton(tr("Record"),this);cancel_=new QPushButton(tr("Cancel"),this);inspect_=new QPushButton(tr("Inspect saved take"),this);keep_=new QPushButton(tr("Keep take and reset"),this);
    const std::pair<QPushButton *,const char *> buttons[]={{prepare_,"nativePrepare"},{start_,"nativeRecord"},{cancel_,"nativeCancel"},{inspect_,"nativeInspect"},{keep_,"nativeKeep"}};
    for(int n=0;n<5;++n){buttons[n].first->setObjectName(buttons[n].second);layout->addWidget(buttons[n].first,2,n);}
    status_=new QLabel(this);status_->setObjectName("nativeStatus");status_->setTextFormat(Qt::PlainText);status_->setWordWrap(true);
    progress_=new QLabel(this);progress_->setObjectName("nativeProgress");receipt_=new QLabel(this);receipt_->setObjectName("nativeReceipt");receipt_->setTextFormat(Qt::PlainText);receipt_->setWordWrap(true);
    layout->addWidget(status_,3,0,1,5);layout->addWidget(progress_,4,0,1,5);layout->addWidget(receipt_,5,0,1,5);
    connect(prepare_,&QPushButton::clicked,this,[this]{
        if(!preparation_ || nextGeneration_==UINT64_MAX)return;
        try {
            NativeMonitorCommand shape;shape.preparation=preparation_;
            auto lease=memory_.reserve(nativeMonitorCommandPayloadBytes(shape,memory_.usage().limitBytes));
            auto copy=std::make_shared<OwnedPreparation>(std::move(lease),*preparation_);
            copy->value.options.route.runGeneration=++nextGeneration_;copy->value.options.run=Id::generate();copy->value.options.raw.asset=Id::generate();
            auto c=command(NativeMonitorCommandKind::Prepare);c.generation=nextGeneration_;c.preparation=std::shared_ptr<const NativeMonitorPreparation>(copy,&copy->value);if(send(c))nextRevision_=0;
        } catch(const ProjectError &){admission_=tr("Recording settings exceed the panel memory limit.");poll();}
    });
    connect(start_,&QPushButton::clicked,this,[this]{
        const auto s=snapshot();if(!s->declared)return;auto c=command(NativeMonitorCommandKind::Activate);
        const auto frames=std::uint64_t(seconds_->value())*s->declared->render.sampleRate;
        if(frames>UINT64_MAX-s->declared->monitorFirst.value){admission_=tr("Monitoring range is too large.");poll();return;}
        c.end={s->declared->monitorFirst.value+frames};send(c);
    });
    connect(cancel_,&QPushButton::clicked,this,[this]{controller_.requestCancel();admission_.clear();});
    connect(inspect_,&QPushButton::clicked,this,[this]{send(command(NativeMonitorCommandKind::Inspect));});
    connect(keep_,&QPushButton::clicked,this,[this]{auto c=command(NativeMonitorCommandKind::ReleaseResult);c.resultSequence=snapshot()->resultSequence;send(c);});
    connect(eq_,&QCheckBox::clicked,this,[this](bool enabled){
        if(nextRevision_==UINT64_MAX)return;
        auto c=command(NativeMonitorCommandKind::Equalizer);c.enabled=enabled;c.revision=nextRevision_+1;if(send(c))++nextRevision_;
    });
    timer_=new QTimer(this);timer_->setInterval(25);connect(timer_,&QTimer::timeout,this,[this]{poll();});timer_->start();poll();
}
NativeMonitorPanel::~NativeMonitorPanel(){timer_->stop();controller_.requestShutdown();}
NativeMonitorCommand NativeMonitorPanel::command(NativeMonitorCommandKind kind) const {NativeMonitorCommand c;c.kind=kind;c.generation=snapshot()->generation;return c;}
std::shared_ptr<const NativeMonitorSnapshot> NativeMonitorPanel::snapshot() const{return controller_.snapshot();}
bool NativeMonitorPanel::send(NativeMonitorCommand c) {
    const auto result=controller_.submit(c);
    if(result==NativeMonitorAdmission::Accepted){admission_.clear();return true;}
    switch(result){
    case NativeMonitorAdmission::Full:admission_=tr("Recording controls are busy. Try again.");break;
    case NativeMonitorAdmission::Closing:admission_=tr("Recording is closing.");break;
    case NativeMonitorAdmission::Invalid:admission_=tr("Recording settings are invalid.");break;
    case NativeMonitorAdmission::ResourceLimit:admission_=tr("Recording settings exceed the control memory limit.");break;
    default:break;
    }poll();return false;
}
bool NativeMonitorPanel::setPreparation(const NativeMonitorPreparation &p) {
    const auto s=snapshot();if(closing_ || s->phase==NativeMonitorPhase::Preparing || s->phase==NativeMonitorPhase::Ready || s->phase==NativeMonitorPhase::Recording || s->phase==NativeMonitorPhase::Finishing || s->result)return false;
    try{
        NativeMonitorCommand c;c.preparation=std::shared_ptr<const NativeMonitorPreparation>(std::shared_ptr<void>{},&p);
        const auto bytes=nativeMonitorCommandPayloadBytes(c,memory_.usage().limitBytes);
        auto lease=memory_.reserve(bytes);auto owned=std::make_shared<OwnedPreparation>(std::move(lease),p);
        preparation_=std::shared_ptr<const NativeMonitorPreparation>(owned,&owned->value);
        {QSignalBlocker block(eq_);eq_->setChecked(p.options.route.equalizer && p.options.route.equalizer->settings.enabled);}
        devices_->setText(tr("Input: %1 · Output: %2").arg(deviceName(p.inputs),deviceName(p.outputs)));admission_.clear();poll();return true;
    }catch(const ProjectError &e){admission_=e.code()==ErrorCode::ResourceLimit?tr("Recording settings exceed the panel memory limit."):tr("Recording settings are invalid.");poll();return false;}
}
bool NativeMonitorPanel::parameter(const EqSettings &settings,const ParameterAddress &address) {
    if(closing_ || nextRevision_==UINT64_MAX)return false;
    auto c=command(NativeMonitorCommandKind::Equalizer);c.equalizer=settings;c.parameter=address;c.revision=nextRevision_+1;
    if(!send(c))return false;
    ++nextRevision_;return true;
}
void NativeMonitorPanel::poll() {
    const auto s=snapshot();const bool editable=!closing_;
    const bool idle=s->phase==NativeMonitorPhase::Idle || (s->phase==NativeMonitorPhase::Fault && !s->result);
    prepare_->setEnabled(editable && preparation_ && idle);start_->setEnabled(editable && s->phase==NativeMonitorPhase::Ready);
    seconds_->setEnabled(editable && (idle || s->phase==NativeMonitorPhase::Ready));
    cancel_->setEnabled(editable && (s->phase==NativeMonitorPhase::Ready || s->phase==NativeMonitorPhase::Recording || s->phase==NativeMonitorPhase::Preparing || s->phase==NativeMonitorPhase::Finishing));
    inspect_->setEnabled(editable && bool(s->result));keep_->setEnabled(editable && bool(s->result));
    eq_->setEnabled(editable && s->declared && s->declared->equalizer && (s->phase==NativeMonitorPhase::Ready || s->phase==NativeMonitorPhase::Recording));
    QString phase;
    switch(s->phase){
    case NativeMonitorPhase::Unsupported:phase=tr("Recording is unavailable.");break;
    case NativeMonitorPhase::Idle:phase=tr("Choose recording settings, then prepare.");break;
    case NativeMonitorPhase::Preparing:phase=tr("Preparing recording…");break;
    case NativeMonitorPhase::Ready:phase=tr("Ready to record.");break;
    case NativeMonitorPhase::Recording:phase=tr("Recording and monitoring…");break;
    case NativeMonitorPhase::Finishing:phase=tr("Finishing and saving the original take…");break;
    case NativeMonitorPhase::Result:phase=s->result && s->result->disposition==NativeMonitorRecordingDisposition::Complete?tr("Original take saved."):tr("Recording cancelled. The retained take is available for inspection.");break;
    case NativeMonitorPhase::Fault:phase=tr("Recording stopped with an error. Inspect the retained take.");break;
    case NativeMonitorPhase::Closed:phase=tr("Recording closed.");break;
    }
    if(s->errorSerial!=errorSeen_){errorSeen_=s->errorSerial;if(s->error)admission_=tr("Recording error (%1).").arg(int(*s->error));}
    status_->setText(admission_.isEmpty()?phase:admission_);status_->setToolTip(QString::fromUtf8(s->diagnostic.data(),qsizetype(std::min<std::size_t>(s->diagnostic.size(),4096))));
    progress_->setText(tr("Original frames: %1 · Monitored frames: %2").arg(qulonglong(s->progress.rawAcceptedFrames)).arg(qulonglong(s->progress.deliveredMonitorFrames)));
    QString details=tr("EQ accepted: %1 · Applied: %2").arg(qulonglong(s->acceptedEqRevision)).arg(qulonglong(s->appliedEqRevision));
    if(s->reopened)details+=tr(" · Saved frames verified: %1").arg(qulonglong(s->reopened->raw.committedFrames));
    receipt_->setText(details);
    if(windowClose_ && closeReady())close();
}
void NativeMonitorPanel::beginClose(){if(closing_)return;closing_=true;controller_.requestShutdown();poll();}
bool NativeMonitorPanel::closeReady() const{return snapshot()->phase==NativeMonitorPhase::Closed;}
void NativeMonitorPanel::closeEvent(QCloseEvent *event){if(closeReady()){event->accept();return;}windowClose_=true;beginClose();event->ignore();}
} // namespace soundcurrent::daw::ui
