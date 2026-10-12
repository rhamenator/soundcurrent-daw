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
#include <QComboBox>
#include <QStandardItemModel>
#include <QLocale>
#include <QFileDialog>
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
    :QGroupBox(parent),memory_(std::move(memory)),widgetCredit_(memory_.reserve(32768)),controller_([&]{interactive_=bool(o.inventory);return std::move(o);}()) {
    setTitle(tr("Recording"));setObjectName("nativeRecordingPanel");
    auto *layout=new QGridLayout(this);
    if(interactive_) {
        auto *row=new QWidget(this);auto *choices=new QGridLayout(row);
        inputDevice_=new QComboBox(row);outputDevice_=new QComboBox(row);mapping_=new QComboBox(row);
        for(auto *combo:{inputDevice_,outputDevice_}){combo->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);combo->setMinimumContentsLength(16);}
        inputDevice_->setObjectName("nativeInputDevice");outputDevice_->setObjectName("nativeOutputDevice");mapping_->setObjectName("nativeChannelMapping");
        inputDevice_->addItem(tr("Choose a device"));outputDevice_->addItem(tr("Choose a device"));
        inputDevice_->setAccessibleName(tr("Capture source"));outputDevice_->setAccessibleName(tr("Monitoring output"));
        mapping_->addItem(tr("Same native channel order"));mapping_->addItem(tr("Duplicate mono to stereo"));mapping_->setAccessibleName(tr("Channel mapping"));
        auto *inputLabel=new QLabel(tr("Capture source"),row);inputLabel->setBuddy(inputDevice_);
        auto *outputLabel=new QLabel(tr("Monitoring output"),row);outputLabel->setBuddy(outputDevice_);
        choices->addWidget(inputLabel,0,0);choices->addWidget(inputDevice_,0,1);
        choices->addWidget(outputLabel,1,0);choices->addWidget(outputDevice_,1,1);
        refresh_=new QPushButton(tr("Refresh devices"),row);refresh_->setObjectName("nativeRefreshDevices");
        choices->addWidget(refresh_,0,2);choices->addWidget(mapping_,1,2);layout->addWidget(row,6,0,1,5);
        connect(refresh_,&QPushButton::clicked,this,[this]{
            const auto state=snapshot();auto c=command(NativeMonitorCommandKind::RefreshDevices);
            if(send(c)){refreshing_=true;refreshCompleted_=state->completedCommands;refreshCancel_=state->cancelAcknowledged;poll();}
        });
        for(auto *choice:{inputDevice_,outputDevice_,mapping_})connect(choice,qOverload<int>(&QComboBox::currentIndexChanged),this,[this]{poll();});
    }
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
    export_=new QPushButton(tr("Export original WAV…"),this);export_->setObjectName("nativeExportOriginal");
    export_->setToolTip(tr("Export the verified captured WAV at its native rate. EQ and timeline alignment are not printed."));
    layout->addWidget(export_,7,0,1,5);
    open_=new QPushButton(tr("Open saved native take…"),this);open_->setObjectName("nativeOpenOriginal");
    open_->setToolTip(tr("Choose an original take directory in this project's media folder. Inspection does not activate audio devices."));
    layout->addWidget(open_,8,0,1,5);
    connect(open_,&QPushButton::clicked,this,[this]{
        if(!project_ || (reopenGuard_ && !reopenGuard_()))return;
        const auto folder=(projectRoot_/"media").u8string();
        const auto name=QFileDialog::getExistingDirectory(this,tr("Open saved native take"),
            QString::fromUtf8(reinterpret_cast<const char *>(folder.data()),qsizetype(folder.size())),QFileDialog::ShowDirsOnly | QFileDialog::DontResolveSymlinks);
        if(name.isEmpty())return;
#ifdef _WIN32
        reopenOriginal(std::filesystem::path(name.toStdWString()));
#else
        reopenOriginal(utf8Path(name.toUtf8().toStdString()));
#endif
    });
    connect(export_,&QPushButton::clicked,this,[this]{
        const auto name=QFileDialog::getSaveFileName(this,tr("Export original native take"),{},tr("WAV audio (*.wav)"));
        if(name.isEmpty())return;
#ifdef _WIN32
        exportOriginal(std::filesystem::path(name.toStdWString()));
#else
        exportOriginal(utf8Path(name.toUtf8().toStdString()));
#endif
    });
    connect(prepare_,&QPushButton::clicked,this,[this]{
        if(!preparation_ || nextGeneration_==UINT64_MAX || (interactive_ && !chooseDevices()))return;
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
    if(!setProject(p.root,p.options.raw.project))return false;
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
bool NativeMonitorPanel::setProject(const std::filesystem::path &root,const Id &project) {
    if(project_ && *project_==project && projectRoot_==root)return true;
    if(closing_ || busy())return false;
    try {
        if(root.empty() || root.native().size()>4096 || root.native().find(std::filesystem::path::value_type{})!=std::filesystem::path::string_type::npos)return false;
        PayloadCharge charge("Native saved project context",memory_.usage().limitBytes);charge.add(4096);charge.add(root.native().size()+1,sizeof(std::filesystem::path::value_type));
        auto credit=memory_.reserve(charge.bytes());auto ownedRoot=root;auto ownedProject=project;
        projectRoot_=std::move(ownedRoot);project_=std::move(ownedProject);projectCredit_=std::move(credit);return true;
    }catch(const ProjectError &){return false;}
}
void NativeMonitorPanel::clearProject(){if(busy())return;projectRoot_=std::filesystem::path{};project_.reset();projectCredit_={};}
void NativeMonitorPanel::reopenGuard(std::function<bool()> guard){reopenGuard_=std::move(guard);}
bool NativeMonitorPanel::reopenOriginal(const std::filesystem::path &path) {
    if(closing_ || busy() || !project_ || nextGeneration_==UINT64_MAX || (reopenGuard_ && !reopenGuard_()))return false;
    const auto s=snapshot();auto c=command(NativeMonitorCommandKind::ReopenOriginal);
    c.root=projectRoot_;c.job=path;c.project=project_;c.generation=nextGeneration_+1;
    if(!send(std::move(c)))return false;
    ++nextGeneration_;reopeningPending_=true;reopenCompleted_=s->completedCommands;reopenCancel_=s->cancelAcknowledged;poll();return true;
}
bool NativeMonitorPanel::exportOriginal(const std::filesystem::path &path) {
    const auto s=snapshot();if(closing_ || (!s->result && !s->reopened))return false;
    auto c=command(NativeMonitorCommandKind::ExportOriginal);c.destination=path;c.resultSequence=s->resultSequence;return send(c);
}
bool NativeMonitorPanel::interactiveDevices() const{return interactive_;}
void NativeMonitorPanel::preparationGuard(std::function<bool(const NativeMonitorPreparation &)> guard){preparationGuard_=std::move(guard);}
void NativeMonitorPanel::updateDevices(const NativeMonitorSnapshot &s) {
    if(!s.ports || s.inventoryRevision==shownInventoryRevision_)return;
    const auto key=[&](QComboBox *combo)->const AudioPort * {
        if(!shownPorts_ || combo->currentIndex()<=0)return nullptr;
        const auto i=combo->currentData().toULongLong();if(i>=shownPorts_->size())return nullptr;return &(*shownPorts_)[std::size_t(i)];
    };
    const auto input=key(inputDevice_),output=key(outputDevice_);
    try {
        PayloadCharge charge("Native device choices",memory_.usage().limitBytes);
        charge.add(nativeMonitorPortsPayloadBytes(*s.ports,memory_.usage().limitBytes),2);
        auto credit=memory_.reserve(charge.bytes());
        // Declare old credit before old models, so model destruction retires first.
        auto oldCredit=std::move(modelCredit_);
        auto inputs=std::make_unique<QStandardItemModel>(),outputs=std::make_unique<QStandardItemModel>();
        inputs->appendRow(new QStandardItem(tr("Choose a device")));outputs->appendRow(new QStandardItem(tr("Choose a device")));
        int inputIndex=0,outputIndex=0;
        for(std::size_t i=0;i<s.ports->size();) {
            const auto &first=(*s.ports)[i];const auto count=first.nativeChannels;
            bool complete=first.backendId=="wasapi" && !first.loopback && first.portId==0 && count && count<=256 && count<=s.ports->size()-i;
            for(std::size_t j=0;complete && j<count;++j) {
                const auto &port=(*s.ports)[i+j];complete=port.deviceIdentity==first.deviceIdentity && port.backendId==first.backendId && port.input==first.input && !port.loopback && port.nativeChannels==count && port.sampleRate==first.sampleRate && port.portId==j;
            }
            if(!complete){++i;continue;}
            auto *model=first.input?outputs.get():inputs.get();
            auto *item=new QStandardItem(tr("%1 · %n channel(s) · %2 Hz",nullptr,int(count)).arg(QString::fromUtf8(first.nodeName.data(),qsizetype(first.nodeName.size())),QLocale().toString(first.sampleRate)));
            item->setData(QVariant::fromValue(qulonglong(i)),Qt::UserRole);model->appendRow(item);
            const auto &prior=first.input?output:input;
            if(prior && prior->backendId==first.backendId && prior->deviceIdentity==first.deviceIdentity && prior->nativeChannels==count && prior->sampleRate==first.sampleRate)
                (first.input?outputIndex:inputIndex)=model->rowCount()-1;
            i+=count;
        }
        QSignalBlocker blockInput(inputDevice_),blockOutput(outputDevice_);
        inputDevice_->setModel(inputs.get());outputDevice_->setModel(outputs.get());
        inputModel_.swap(inputs);outputModel_.swap(outputs);modelCredit_=std::move(credit);
        shownPorts_=s.ports;shownInventoryRevision_=s.inventoryRevision;
        inputDevice_->setCurrentIndex(inputIndex);outputDevice_->setCurrentIndex(outputIndex);
    }catch(const ProjectError &){admission_=tr("Device choices exceed the panel memory limit.");}
}
bool NativeMonitorPanel::chooseDevices() {
    if(!preparation_ || !shownPorts_ || inputDevice_->currentIndex()<=0 || outputDevice_->currentIndex()<=0)return false;
    try {
        const auto inputs=std::size_t(inputDevice_->currentData().toULongLong()),outputs=std::size_t(outputDevice_->currentData().toULongLong());
        if(inputs>=shownPorts_->size() || outputs>=shownPorts_->size())return false;
        const auto inCount=(*shownPorts_)[inputs].nativeChannels,outCount=(*shownPorts_)[outputs].nativeChannels;
        if(inCount>shownPorts_->size()-inputs || outCount>shownPorts_->size()-outputs || inCount!=preparation_->options.raw.layout.channels) {
            admission_=tr("The capture device width must match the selected track.");return false;
        }
        const bool duplicate=mapping_->currentIndex()==1;
        if((duplicate && (inCount!=1 || outCount!=2)) || (!duplicate && inCount!=outCount)) {
            admission_=tr("Choose a matching output width or explicit mono-to-stereo mapping.");return false;
        }
        const auto in=std::span(*shownPorts_).subspan(inputs,inCount),out=std::span(*shownPorts_).subspan(outputs,outCount);
        NativeMonitorCommand shape;shape.preparation=preparation_;
        PayloadCharge charge("Selected native context",memory_.usage().limitBytes);
        charge.add(nativeMonitorCommandPayloadBytes(shape,memory_.usage().limitBytes));charge.add(nativeMonitorPortsPayloadBytes(in,memory_.usage().limitBytes));charge.add(nativeMonitorPortsPayloadBytes(out,memory_.usage().limitBytes));
        auto lease=memory_.reserve(charge.bytes());auto owned=std::make_shared<OwnedPreparation>(std::move(lease),*preparation_);
        owned->value.inputs.assign(in.begin(),in.end());owned->value.outputs.assign(out.begin(),out.end());
        owned->value.options.route.channelMap=duplicate?std::optional(monoToStereoMonitor()):std::nullopt;
        if(preparationGuard_)return preparationGuard_(owned->value);
        return setPreparation(owned->value);
    }catch(const ProjectError &){admission_=tr("Selected devices exceed the panel memory limit.");return false;}
}
void NativeMonitorPanel::clearPreparation(){if(busy())return;preparation_.reset();devices_->clear();poll();}
bool NativeMonitorPanel::enabled(bool value) {
    if(closing_ || nextRevision_==UINT64_MAX)return false;
    auto c=command(NativeMonitorCommandKind::Equalizer);c.enabled=value;c.revision=nextRevision_+1;
    if(!send(c))return false;
    ++nextRevision_;QSignalBlocker blocked(eq_);eq_->setChecked(value);return true;
}
bool NativeMonitorPanel::parameter(const EqSettings &settings,const ParameterAddress &address) {
    if(closing_ || nextRevision_==UINT64_MAX)return false;
    auto c=command(NativeMonitorCommandKind::Equalizer);c.equalizer=settings;c.parameter=address;c.revision=nextRevision_+1;
    if(!send(c))return false;
    ++nextRevision_;return true;
}
void NativeMonitorPanel::poll() {
    const auto s=snapshot();const bool editable=!closing_;
    if(reopeningPending_ && (s->completedCommands>reopenCompleted_ || s->cancelAcknowledged!=reopenCancel_ || s->phase==NativeMonitorPhase::Closed))reopeningPending_=false;
    const bool saved=bool(s->result) || bool(s->reopened);
    const auto *prefix=s->reopened?s->reopened.get():(s->result && s->result->prefix?&*s->result->prefix:nullptr);
    if(interactive_) {
        if(refreshing_ && (s->completedCommands>refreshCompleted_ || s->cancelAcknowledged!=refreshCancel_ || s->phase==NativeMonitorPhase::Closed))refreshing_=false;
        updateDevices(*s);
        const bool choices=editable && !busy();
        refresh_->setEnabled(choices && !refreshing_);
        for(auto *combo:{inputDevice_,outputDevice_,mapping_})combo->setEnabled(choices && !refreshing_);
    }
    const bool idle=s->phase==NativeMonitorPhase::Idle || (s->phase==NativeMonitorPhase::Fault && !s->result);
    prepare_->setEnabled(editable && preparation_ && idle && (!interactive_ || (!refreshing_ && inputDevice_->currentIndex()>0 && outputDevice_->currentIndex()>0)));start_->setEnabled(editable && s->phase==NativeMonitorPhase::Ready);
    seconds_->setEnabled(editable && (idle || s->phase==NativeMonitorPhase::Ready));
    cancel_->setEnabled(editable && (s->phase==NativeMonitorPhase::Ready || s->phase==NativeMonitorPhase::Recording || s->phase==NativeMonitorPhase::Preparing || s->phase==NativeMonitorPhase::Finishing || s->exporting || reopeningPending_ || s->phase==NativeMonitorPhase::Reopening));
    inspect_->setEnabled(editable && saved && !s->exporting);keep_->setEnabled(editable && saved && !s->exporting);
    export_->setEnabled(editable && saved && !s->exporting && prefix && prefix->raw.finalized && prefix->raw.committedFrames && prefix->raw.finitePrefixFrames==prefix->raw.committedFrames);
    open_->setEnabled(editable && project_ && !busy());
    eq_->setEnabled(editable && s->declared && s->declared->equalizer && (s->phase==NativeMonitorPhase::Ready || s->phase==NativeMonitorPhase::Recording));
    QString phase;
    switch(s->phase){
    case NativeMonitorPhase::Unsupported:phase=tr("Recording is unavailable.");break;
    case NativeMonitorPhase::Idle:phase=tr("Choose recording settings, then prepare.");break;
    case NativeMonitorPhase::Preparing:phase=tr("Preparing recording…");break;
    case NativeMonitorPhase::Reopening:phase=tr("Opening and verifying saved native take…");break;
    case NativeMonitorPhase::Ready:phase=tr("Ready to record.");break;
    case NativeMonitorPhase::Recording:phase=tr("Recording and monitoring…");break;
    case NativeMonitorPhase::Finishing:phase=tr("Finishing and saving the original take…");break;
    case NativeMonitorPhase::Result:phase=s->result && s->result->disposition==NativeMonitorRecordingDisposition::Complete?tr("Original take saved."):tr("Recording cancelled. The retained take is available for inspection.");break;
    case NativeMonitorPhase::Fault:phase=tr("Recording stopped with an error. Inspect the retained take.");break;
    case NativeMonitorPhase::Closed:phase=tr("Recording closed.");break;
    }
    if(!s->result && s->reopened) {
        QString outcome;
        switch(s->reopened->status) {
        case NativeOriginalRecordingStatus::Complete:outcome=tr("Complete");break;
        case NativeOriginalRecordingStatus::Cancelled:outcome=tr("Cancelled");break;
        case NativeOriginalRecordingStatus::Failed:outcome=tr("Failed");break;
        case NativeOriginalRecordingStatus::Interrupted:outcome=tr("Interrupted");break;
        case NativeOriginalRecordingStatus::Pending:outcome=tr("Pending");break;
        case NativeOriginalRecordingStatus::NoNativeEvidence:outcome=tr("No native outcome");break;
        }
        phase=tr("Saved native take opened. Original outcome: %1.").arg(outcome);
    }
    if(s->exporting)phase=tr("Exporting original WAV…");
    if(s->errorSerial!=errorSeen_){errorSeen_=s->errorSerial;if(s->error)admission_=tr("Recording error (%1).").arg(int(*s->error));}
    status_->setText(admission_.isEmpty()?phase:admission_);status_->setToolTip(QStringLiteral("<pre>")+QString::fromUtf8(s->diagnostic.data(),qsizetype(std::min<std::size_t>(s->diagnostic.size(),4096))).toHtmlEscaped()+QStringLiteral("</pre>"));
    progress_->setText(tr("Original frames: %1 · Monitored frames: %2").arg(qulonglong(s->progress.rawAcceptedFrames)).arg(qulonglong(s->progress.deliveredMonitorFrames)));
    QString details=tr("EQ accepted: %1 · Applied: %2").arg(qulonglong(s->acceptedEqRevision)).arg(qulonglong(s->appliedEqRevision));
    if(s->reopened)details+=tr(" · Saved frames verified: %1").arg(qulonglong(s->reopened->raw.committedFrames));
    if(s->exported)details+=tr(" · Original WAV exported: %1 frames").arg(QLocale().toString(qulonglong(s->exported->frames)));
    if(s->exported) {
        const auto name=s->exported->destination.u8string();
        details+=QStringLiteral(" · ")+QString::fromUtf8(reinterpret_cast<const char *>(name.data()),qsizetype(name.size()));
        if(!s->exported->publicationWarning.empty())details+=tr(" · Export published with a durability warning.");
    }
    receipt_->setText(details);
    if(windowClose_ && closeReady())close();
}
void NativeMonitorPanel::cancel(){controller_.requestCancel();admission_.clear();}
bool NativeMonitorPanel::busy() const {
    const auto s=snapshot();return reopeningPending_ || bool(s->result) || bool(s->reopened) || s->phase==NativeMonitorPhase::Reopening || s->phase==NativeMonitorPhase::Preparing || s->phase==NativeMonitorPhase::Ready || s->phase==NativeMonitorPhase::Recording || s->phase==NativeMonitorPhase::Finishing;
}
void NativeMonitorPanel::beginClose(){if(closing_)return;closing_=true;controller_.requestShutdown();poll();}
bool NativeMonitorPanel::closeReady() const{return snapshot()->phase==NativeMonitorPhase::Closed;}
void NativeMonitorPanel::closeEvent(QCloseEvent *event){if(closeReady()){event->accept();return;}windowClose_=true;beginClose();event->ignore();}
} // namespace soundcurrent::daw::ui
