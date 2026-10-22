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
#include <QLineEdit>
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
    layout->addWidget(export_,7,0,1,2);
    copy_=new QPushButton(tr("Copy verified audio…"),this);copy_->setObjectName("nativeCopyPrefix");
    copy_->setToolTip(tr("Copy the verified playable audio to a new WAV. The original take and its outcome stay unchanged."));
    layout->addWidget(copy_,7,2,1,3);
    copyPreview_=new QLabel(this);copyPreview_->setObjectName("nativeCopyPreview");copyPreview_->setTextFormat(Qt::PlainText);copyPreview_->setWordWrap(true);
    layout->addWidget(copyPreview_,8,0,1,5);
    open_=new QPushButton(tr("Open saved native take…"),this);open_->setObjectName("nativeOpenOriginal");
    open_->setToolTip(tr("Choose an original take directory in this project's media folder. Inspection does not activate audio devices."));
    layout->addWidget(open_,9,0,1,5);
    auto *admissionRow=new QWidget(this);auto *admissionLayout=new QGridLayout(admissionRow);
    auto *placementLabel=new QLabel(tr("Start frame (project rate)"),admissionRow);
    placement_=new QLineEdit(QLocale().toString(qlonglong(0)),admissionRow);placement_->setObjectName("nativePlacement");placement_->setMaxLength(32);placementLabel->setBuddy(placement_);
    add_=new QPushButton(tr("Add to selected track"),admissionRow);add_->setObjectName("nativeAddTake");
    add_->setToolTip(tr("Use this explicit timeline position. Captured audio and native timing evidence stay unchanged."));
    admissionLayout->addWidget(placementLabel,0,0);admissionLayout->addWidget(placement_,0,1);admissionLayout->addWidget(add_,0,2);layout->addWidget(admissionRow,10,0,1,5);
    auto *storageRow=new QWidget(this);auto *storageLayout=new QGridLayout(storageRow);
    storageEnabled_=new QCheckBox(tr("Buffer for disk pauses"),storageRow);storageEnabled_->setObjectName("nativeStorageEnabled");
    storageEnabled_->setToolTip(tr("Prepare a finite buffer before recording. Repeated pauses or a slow disk can still stop recording."));
    storageMilliseconds_=new QSpinBox(storageRow);storageMilliseconds_->setObjectName("nativeStorageMilliseconds");storageMilliseconds_->setRange(1,60000);storageMilliseconds_->setValue(3000);storageMilliseconds_->setSuffix(tr(" ms"));
    minimumCapture_=new QSpinBox(storageRow);minimumCapture_->setObjectName("nativeStorageCaptureMinimum");minimumCapture_->setRange(0,65536);minimumCapture_->setSpecialValueText(tr("Choose"));
    minimumRender_=new QSpinBox(storageRow);minimumRender_->setObjectName("nativeStorageRenderMinimum");minimumRender_->setRange(0,65536);minimumRender_->setSpecialValueText(tr("Choose"));
    storageLayout->addWidget(storageEnabled_,0,0);storageLayout->addWidget(storageMilliseconds_,0,1);
    const std::pair<QSpinBox *,QString> minimums[]={{minimumCapture_,tr("Minimum input block (frames)")},{minimumRender_,tr("Minimum output block (frames)")}};
    for(int n=0;n<2;++n){auto *label=new QLabel(minimums[n].second,storageRow);label->setBuddy(minimums[n].first);minimums[n].first->setAccessibleName(minimums[n].second);
        minimums[n].first->setToolTip(tr("Enter the smallest block allowed by your audio setup. Recording stops if an actual block is smaller. This is not the maximum block size."));
        storageLayout->addWidget(label,n+1,0);storageLayout->addWidget(minimums[n].first,n+1,1);}
    storageMilliseconds_->setAccessibleName(tr("Disk pause budget"));storagePreview_=new QLabel(storageRow);storagePreview_->setObjectName("nativeStoragePreview");storagePreview_->setTextFormat(Qt::PlainText);storagePreview_->setWordWrap(true);
    storageLayout->addWidget(storagePreview_,3,0,1,2);layout->addWidget(storageRow,11,0,1,5);
    connect(storageEnabled_,&QCheckBox::toggled,this,[this]{storageDirty_=true;admission_.clear();poll();});
    for(auto *spin:{storageMilliseconds_,minimumCapture_,minimumRender_})connect(spin,qOverload<int>(&QSpinBox::valueChanged),this,[this]{storageDirty_=true;admission_.clear();poll();});
    connect(add_,&QPushButton::clicked,this,[this]{bool valid=false;const auto start=QLocale().toLongLong(placement_->text(),&valid);
        if(!valid || start<0){admission_=tr("Enter a nonnegative project frame.");poll();return;}
        if(addRequested)addRequested(Frame(start));});
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
    connect(copy_,&QPushButton::clicked,this,[this]{
        const auto name=QFileDialog::getSaveFileName(this,tr("Copy verified audio"),{},tr("WAV audio (*.wav)"));
        if(name.isEmpty())return;
#ifdef _WIN32
        copyPrefix(std::filesystem::path(name.toStdWString()));
#else
        copyPrefix(utf8Path(name.toUtf8().toStdString()));
#endif
    });
    connect(prepare_,&QPushButton::clicked,this,[this]{
        if(busy() || !preparation_ || nextGeneration_==UINT64_MAX || (interactive_ && !chooseDevices()))return;
        try {
            NativeMonitorCommand shape;shape.preparation=preparation_;
            auto lease=memory_.reserve(nativeMonitorCommandPayloadBytes(shape,memory_.usage().limitBytes));
            auto copy=std::make_shared<OwnedPreparation>(std::move(lease),*preparation_);
            if(copy->value.pipewire){auto &o=*copy->value.pipewire;if(!applyStorageBudget(o)){poll();return;}o.declared.generation=++nextGeneration_;o.declared.run=Id::generate();o.declared.raw.asset=Id::generate();}
            else{if(!applyNativeStorageBudget(copy->value.options)){poll();return;}copy->value.options.route.runGeneration=++nextGeneration_;copy->value.options.run=Id::generate();copy->value.options.raw.asset=Id::generate();}
            auto c=command(NativeMonitorCommandKind::Prepare);c.generation=nextGeneration_;c.preparation=std::shared_ptr<const NativeMonitorPreparation>(copy,&copy->value);queuePreparation(std::move(c));
        } catch(const ProjectError &){admission_=tr("Recording settings exceed the panel memory limit.");poll();}
    });
    connect(start_,&QPushButton::clicked,this,[this]{
        const auto s=snapshot();if(!s->declared && !s->pipewireDeclared)return;auto c=command(NativeMonitorCommandKind::Activate);
        const auto frames=std::uint64_t(seconds_->value())*s->monitorRate();
        if(frames>UINT64_MAX-(s->pipewireDeclared?s->monitorFirst.value:s->declared->monitorFirst.value)){admission_=tr("Monitoring range is too large.");poll();return;}
        c.end={(s->pipewireDeclared?s->monitorFirst.value:s->declared->monitorFirst.value)+frames};send(c);
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
void NativeMonitorPanel::bindAutosaveGuard(ProjectController &project, std::uint64_t &sequence) {
    if (busy() || projectController_) throw ProjectError(ErrorCode::InvalidState,"Native autosave guard is already bound or busy");
    projectController_=&project; guardSequence_=&sequence;
}
void NativeMonitorPanel::queuePreparation(NativeMonitorCommand c) {
    if (!projectController_) {if(send(std::move(c)))nextRevision_=0;return;}
    const auto p=projectController_->snapshot();
    if (!p->session || p->root!=c.preparation->root || !*guardSequence_ || *guardSequence_==UINT64_MAX) {
        admission_=tr("Recording preparation could not pause autosave. Finish project operations and retry.");return;
    }
    ProjectCommand acquire{CommandKind::AcquireAutosaveInhibition};
    acquire.autosaveInhibitionToken=(*guardSequence_)++;
    acquire.autosaveInhibitionEpoch=p->projectEpoch;
    acquire.commitGestureForAutosaveInhibition=true;
    if(projectController_->submit(acquire)!=Admission::Accepted) {
        admission_=tr("Recording controls are busy. Try again.");return;
    }
    autosaveToken_=acquire.autosaveInhibitionToken;autosaveEpoch_=p->projectEpoch;
    autosaveAcquiring_=true;guardedPreparation_=std::move(c);
}
void NativeMonitorPanel::pollAutosaveGuard(const NativeMonitorSnapshot &s) {
    if(!projectController_ || !autosaveToken_)return;
    const auto p=projectController_->snapshot();
    if(autosaveAcquiring_) {
        const auto &r=p->autosaveInhibitionRejected;
        if(r.token==autosaveToken_ && r.projectEpoch==autosaveEpoch_ && r.acquisition) {
            guardedPreparation_.reset();autosaveAcquiring_=false;autosaveToken_=0;
            admission_=tr("Recording preparation could not pause autosave. Finish project operations and retry.");return;
        }
        if(p->autosaveInhibitionToken==autosaveToken_ && p->projectEpoch==autosaveEpoch_)autosaveAcquiring_=false;
    }
    if(autosaveReleasing_ && !p->autosaveInhibitionToken) {autosaveReleasing_=false;autosaveToken_=0;return;}
    if(preparePending_ && (s.completedPrepareGeneration==prepareGeneration_ || s.cancelAcknowledged!=prepareCanceled_ || s.phase==NativeMonitorPhase::Closed))preparePending_=false;
    if(guardedPreparation_ && !autosaveAcquiring_ && !closing_) {
        const auto result=controller_.submit(*guardedPreparation_);
        if(result==NativeMonitorAdmission::Accepted) {
            preparePending_=true;prepareGeneration_=guardedPreparation_->generation;prepareCanceled_=s.cancelAcknowledged;
            guardedPreparation_.reset();nextRevision_=0;
        } else if(result!=NativeMonitorAdmission::Full) {
            guardedPreparation_.reset();admission_=tr("Recording settings are invalid.");
        }
    }
    const bool inactive=s.phase==NativeMonitorPhase::Idle || s.phase==NativeMonitorPhase::Fault || s.phase==NativeMonitorPhase::Closed;
    if(!autosaveAcquiring_ && !autosaveReleasing_ && !guardedPreparation_ && !preparePending_ && inactive &&
       (s.phase==NativeMonitorPhase::Closed || (!s.hasOriginal() && !reopeningPending_ && !s.copying && !s.exporting && !s.admitting && !admissionPending_ && !copyPending_))) {
        ProjectCommand release{CommandKind::ReleaseAutosaveInhibition};
        release.autosaveInhibitionToken=autosaveToken_;release.autosaveInhibitionEpoch=autosaveEpoch_;
        autosaveReleasing_=projectController_->submit(std::move(release))==Admission::Accepted;
    }
}
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
    if(!setProject(p.root,(p.pipewire?p.pipewire->declared.raw.project:p.options.raw.project)))return false;
    const auto s=snapshot();if(closing_ || s->phase==NativeMonitorPhase::Preparing || s->phase==NativeMonitorPhase::Ready || s->phase==NativeMonitorPhase::Recording || s->phase==NativeMonitorPhase::Finishing || s->hasOriginal())return false;
    try{
        NativeMonitorCommand c;c.preparation=std::shared_ptr<const NativeMonitorPreparation>(std::shared_ptr<void>{},&p);
        const auto bytes=nativeMonitorCommandPayloadBytes(c,memory_.usage().limitBytes);
        auto lease=memory_.reserve(bytes);auto owned=std::make_shared<OwnedPreparation>(std::move(lease),p);
        preparation_=std::shared_ptr<const NativeMonitorPreparation>(owned,&owned->value);storageDirty_=true;
        if(p.pipewire && p.pipewire->storageBudget){const auto &budget=*p.pipewire->storageBudget;
            QSignalBlocker enabled(storageEnabled_),duration(storageMilliseconds_),input(minimumCapture_),output(minimumRender_);
            storageEnabled_->setChecked(true);storageMilliseconds_->setValue(int(budget.stallMilliseconds));minimumCapture_->setValue(int(budget.minimumCaptureFrames));minimumRender_->setValue(int(budget.minimumRenderFrames));}
        if(!p.pipewire && p.options.route.storagePolicy){const auto &budget=*p.options.route.storagePolicy;
            QSignalBlocker enabled(storageEnabled_),duration(storageMilliseconds_),input(minimumCapture_);
            storageEnabled_->setChecked(true);storageMilliseconds_->setValue(int(budget.stallMilliseconds));minimumCapture_->setValue(int(budget.minimumPacketFrames));}
        {QSignalBlocker block(eq_);const auto &eq=p.pipewire?p.pipewire->route.equalizer:p.options.route.equalizer;eq_->setChecked(eq && eq->settings.enabled);}
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
    c.root=projectRoot_;c.job=path;c.project=project_;c.generation=nextGeneration_+1;c.pipewire=s->pipewire;
    if(!send(std::move(c)))return false;
    ++nextGeneration_;reopeningPending_=true;reopenCompleted_=s->completedCommands;reopenCancel_=s->cancelAcknowledged;poll();return true;
}
bool NativeMonitorPanel::prepareAdmission(const Id &track,Frame placement,std::uint32_t rate,std::uint64_t revision) {
    const auto s=snapshot();if(closing_ || !s->hasOriginal() || s->admitting || s->exporting || s->copying || copyPending_)return false;
    auto c=command(NativeMonitorCommandKind::AdmitTake);c.targetTrack=track;c.clipId=Id::generate();c.placement=placement;c.projectRate=rate;c.revision=revision;c.resultSequence=s->resultSequence;
    if(!send(std::move(c)))return false;
    admissionPending_=true;admissionCompleted_=s->completedCommands;admissionCancel_=s->cancelAcknowledged;poll();return true;
}
void NativeMonitorPanel::releaseOriginal(){auto c=command(NativeMonitorCommandKind::ReleaseResult);c.resultSequence=snapshot()->resultSequence;send(std::move(c));}
bool NativeMonitorPanel::exportOriginal(const std::filesystem::path &path) {
    const auto s=snapshot();if(closing_ || !s->hasOriginal() || s->copying || copyPending_)return false;
    auto c=command(NativeMonitorCommandKind::ExportOriginal);c.destination=path;c.resultSequence=s->resultSequence;return send(c);
}
bool NativeMonitorPanel::copyPrefix(const std::filesystem::path &path) {
    const auto s=snapshot();const auto *prefix=s->rawPrefix();
    if(closing_ || copyPending_ || s->copying || s->exporting || s->admitting || !prefix || !prefix->finitePrefixFrames ||
        (s->phase!=NativeMonitorPhase::Result && s->phase!=NativeMonitorPhase::Fault))return false;
    auto c=command(NativeMonitorCommandKind::CopyPrefix);c.destination=path;c.resultSequence=s->resultSequence;
    if(!send(std::move(c)))return false;
    copyPending_=true;copyCompleted_=s->completedCommands;copyCancel_=s->cancelAcknowledged;poll();return true;
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
            bool complete=(first.backendId=="wasapi" || first.backendId=="pipewire") && !first.loopback && (first.backendId=="pipewire" || first.portId==0) && count && count<=256 && count<=s.ports->size()-i;
            for(std::size_t j=0;complete && j<count;++j) {
                const auto &port=(*s.ports)[i+j];complete=port.deviceIdentity==first.deviceIdentity && port.backendId==first.backendId && port.input==first.input && !port.loopback && port.nativeChannels==count && port.sampleRate==first.sampleRate && (first.backendId=="pipewire" || port.portId==j);
            }
            if(!complete){++i;continue;}
            auto *model=first.input?outputs.get():inputs.get();
            auto label=tr("%1 · %n channel(s) · %2 Hz",nullptr,int(count)).arg(QString::fromUtf8(first.nodeName.data(),qsizetype(first.nodeName.size())),QLocale().toString(first.sampleRate));
            if(first.backendId=="pipewire")label=tr("%1 · %n channel(s)",nullptr,int(count)).arg(QString::fromUtf8(first.nodeName.data(),qsizetype(first.nodeName.size())));
            auto *item=new QStandardItem(label);
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
        if(inCount>shownPorts_->size()-inputs || outCount>shownPorts_->size()-outputs || inCount!=(preparation_->pipewire?preparation_->pipewire->declared.raw.layout.channels:preparation_->options.raw.layout.channels)) {
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
        if(owned->value.pipewire){auto &o=*owned->value.pipewire;o.route.channelMap=duplicate?std::optional(monoToStereoMonitor()):std::nullopt;o.declared.renderChannels=outCount;}
        else owned->value.options.route.channelMap=duplicate?std::optional(monoToStereoMonitor()):std::nullopt;
        if(preparationGuard_)return preparationGuard_(owned->value);
        return setPreparation(owned->value);
    }catch(const ProjectError &){admission_=tr("Selected devices exceed the panel memory limit.");return false;}
}
void NativeMonitorPanel::clearPreparation(){if(busy())return;storageDirty_=true;preparation_.reset();devices_->clear();poll();}
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
bool NativeMonitorPanel::applyStorageBudget(PipeWireMonitorRecordingOptions &o) const {
    if(!storageEnabled_->isChecked()){o.storageBudget.reset();o.route.minimumCaptureFrames=o.route.minimumRenderFrames=1;o.route.storageStallMilliseconds=0;return true;}
    if(!minimumCapture_->value() || !minimumRender_->value())return false;
    if(std::uint32_t(minimumRender_->value())>o.declared.maximumRenderFrames)throw ProjectError(ErrorCode::InvalidParameter,"Minimum output block exceeds declared maximum");
    RecordingStorageBudget budget;budget.captureRate=o.declared.raw.sampleRate;budget.renderRate=o.declared.renderRate;
    budget.captureChannels=o.declared.raw.layout.channels;budget.maximumCaptureFrames=o.declared.raw.maximumPacketFrames;
    budget.minimumCaptureFrames=std::uint32_t(minimumCapture_->value());budget.minimumRenderFrames=std::uint32_t(minimumRender_->value());budget.stallMilliseconds=std::uint32_t(storageMilliseconds_->value());
    if(o.storageBudget){if(o.storageBudget->headroomPackets!=2)throw ProjectError(ErrorCode::InvalidParameter,"Unsupported owner storage headroom");budget.memoryBudgetBytes=o.storageBudget->memoryBudgetBytes;}
    const auto capacity=prepareRecordingStorageCapacity(budget);o.storageBudget=budget;o.route.rawSlots=capacity.rawSlots;
    o.route.receiptSlots=std::max({o.route.receiptSlots,capacity.captureReceiptSlots,capacity.renderReceiptSlots});
    o.route.minimumCaptureFrames=budget.minimumCaptureFrames;o.route.minimumRenderFrames=budget.minimumRenderFrames;o.route.storageStallMilliseconds=budget.stallMilliseconds;
    return true;
}
bool NativeMonitorPanel::applyNativeStorageBudget(NativeMonitorRecordingOptions &o) const {
    if(!storageEnabled_->isChecked()){o.route.storagePolicy.reset();return true;}
    if(!minimumCapture_->value())return false;
    RawRecordingStoragePolicy policy;policy.stallMilliseconds=std::uint32_t(storageMilliseconds_->value());policy.minimumPacketFrames=std::uint32_t(minimumCapture_->value());
    if(o.route.storagePolicy){policy.maximumSlots=o.route.storagePolicy->maximumSlots;policy.memoryBudgetBytes=o.route.storagePolicy->memoryBudgetBytes;}
    o.route.storagePolicy=policy;return true;
}
void NativeMonitorPanel::updateStoragePreview(bool inactive){
    const auto s=snapshot();const bool supported=bool(preparation_);const bool pipewire=supported && bool(preparation_->pipewire);
    storageEnabled_->setEnabled(inactive && supported);for(auto *spin:{storageMilliseconds_,minimumCapture_})spin->setEnabled(inactive && supported && storageEnabled_->isChecked());
    minimumRender_->setEnabled(inactive && pipewire && storageEnabled_->isChecked());
    if(storageDirty_)storageValid_=true;
    if(!inactive && s->storageCapacity){const auto &c=*s->storageCapacity;
        if(!s->pipewire){
            if(s->routePayloadBytes)storagePreview_->setText(tr("Prepared: %1 ms · %2 audio slots · %3 bytes of prepared route payload. Output fills are variable.").arg(QLocale().toString(c.declaredStallMilliseconds),QLocale().toString(c.rawSlots),QLocale().toString(qulonglong(s->routePayloadBytes))));
            else storagePreview_->setText(tr("Saved declaration: %1 ms · %2 audio slots. No audio route is allocated.").arg(QLocale().toString(c.declaredStallMilliseconds),QLocale().toString(c.rawSlots)));
            return;
        }
        if(!s->routePayloadBytes)storagePreview_->setText(tr("Saved declaration: %1 ms · %2 audio slots · %3 receipt slots. No audio route is allocated.").arg(QLocale().toString(c.declaredStallMilliseconds),QLocale().toString(c.rawSlots),QLocale().toString(c.captureReceiptSlots)));
        else storagePreview_->setText(tr("Prepared: %1 ms · %2 audio slots · %3 receipt slots · %4 bytes of prepared route payload.").arg(QLocale().toString(c.declaredStallMilliseconds),QLocale().toString(c.rawSlots),QLocale().toString(c.captureReceiptSlots),QLocale().toString(qulonglong(s->routePayloadBytes))));
        return;
    }
    if(!inactive && (s->pipewireDeclared || s->declared || s->hasOriginal())){storagePreview_->setText(tr("No disk-pause duration was declared for this take."));return;}
    if(!supported){storageValid_=true;storagePreview_->setText(tr("Disk-pause budgeting is not available for this audio route yet."));return;}
    if(!storageEnabled_->isChecked()){storageValid_=true;storagePreview_->setText(tr("Using existing buffers. No disk-pause duration is declared."));return;}
    if(!pipewire){
        storageValid_=minimumCapture_->value()>0;
        if(storageValid_)storagePreview_->setText(tr("The input device's actual format and memory cost will be checked during Prepare. Output fills do not set the disk-buffer budget."));
        else storagePreview_->setText(tr("Choose the minimum input packet size before preparing."));
        return;
    }
    if(!minimumCapture_->value() || !minimumRender_->value()){storageValid_=false;storageDirty_=true;storagePreview_->setText(tr("Choose minimum input and output block sizes before preparing."));return;}
    if(!storageDirty_){storagePreview_->setText(storagePlan_);return;}
    storageDirty_=false;
    try{auto options=*preparation_->pipewire;applyStorageBudget(options);const auto capacity=pipeWireStorageCapacity(options.declared,options.route);const auto bytes=pipeWireRoutePayloadBytes(options.declared,options.route);
        storagePreview_->setText(tr("Plan: %1 ms · %2 audio slots · %3 receipt slots · %4 bytes of route payload. Block sizes will be checked while recording.").arg(QLocale().toString(capacity->declaredStallMilliseconds),QLocale().toString(capacity->rawSlots),QLocale().toString(capacity->captureReceiptSlots),QLocale().toString(qulonglong(bytes))));
    }catch(const ProjectError &e){storageValid_=false;storagePreview_->setText(e.code()==ErrorCode::ResourceLimit?tr("This disk-pause budget exceeds the buffer or memory limit. Reduce the duration or change the audio setup."):tr("The minimum block sizes do not match the recording setup."));}
    storagePlan_=storagePreview_->text();
}
void NativeMonitorPanel::poll() {
    const auto s=snapshot();const bool editable=!closing_;
    if(reopeningPending_ && (s->completedCommands>reopenCompleted_ || s->cancelAcknowledged!=reopenCancel_ || s->phase==NativeMonitorPhase::Closed))reopeningPending_=false;
    if(admissionPending_ && (s->completedCommands>admissionCompleted_ || s->cancelAcknowledged!=admissionCancel_ || s->phase==NativeMonitorPhase::Closed))admissionPending_=false;
    if(copyPending_ && (s->completedCommands>copyCompleted_ || s->cancelAcknowledged!=copyCancel_ || s->phase==NativeMonitorPhase::Closed))copyPending_=false;
    pollAutosaveGuard(*s);
    const bool saved=s->hasOriginal();
    const auto *prefix=s->rawPrefix();
    if(interactive_) {
        if(refreshing_ && (s->completedCommands>refreshCompleted_ || s->cancelAcknowledged!=refreshCancel_ || s->phase==NativeMonitorPhase::Closed))refreshing_=false;
        updateDevices(*s);
        const bool choices=editable && !busy();
        refresh_->setEnabled(choices && !refreshing_);
        for(auto *combo:{inputDevice_,outputDevice_,mapping_})combo->setEnabled(choices && !refreshing_);
    }
    const bool idle=s->phase==NativeMonitorPhase::Idle || (s->phase==NativeMonitorPhase::Fault && !saved);
    updateStoragePreview(editable && idle && !saved);
    prepare_->setEnabled(editable && !busy() && preparation_ && storageValid_ && idle && (!interactive_ || (!refreshing_ && inputDevice_->currentIndex()>0 && outputDevice_->currentIndex()>0)));start_->setEnabled(editable && s->phase==NativeMonitorPhase::Ready);
    seconds_->setEnabled(editable && (idle || s->phase==NativeMonitorPhase::Ready));
    cancel_->setEnabled(editable && (autosaveAcquiring_ || guardedPreparation_ || preparePending_ || s->phase==NativeMonitorPhase::Ready || s->phase==NativeMonitorPhase::Recording || s->phase==NativeMonitorPhase::Preparing || s->phase==NativeMonitorPhase::Finishing || s->copying || copyPending_ || s->exporting || s->admitting || admissionPending_ || reopeningPending_ || s->phase==NativeMonitorPhase::Reopening));
    inspect_->setEnabled(editable && saved && !s->copying && !copyPending_ && !s->exporting && !s->admitting && !admissionPending_);keep_->setEnabled(editable && saved && !s->copying && !copyPending_ && !s->exporting && !s->admitting && !admissionPending_);
    export_->setEnabled(editable && saved && !s->copying && !copyPending_ && !s->exporting && !s->admitting && !admissionPending_ && prefix && prefix->finalized && prefix->committedFrames && prefix->finitePrefixFrames==prefix->committedFrames);
    copy_->setEnabled(editable && saved && !s->copying && !copyPending_ && !s->exporting && !s->admitting &&
        !admissionPending_ && prefix && prefix->finitePrefixFrames &&
        (s->phase==NativeMonitorPhase::Result || s->phase==NativeMonitorPhase::Fault));
    if(prefix) {
        QString text=tr("Available to copy: %1 of %2 verified frames · %3 Hz · %4 channels.").arg(
            QLocale().toString(qulonglong(prefix->finitePrefixFrames)),QLocale().toString(qulonglong(prefix->committedFrames)),
            QLocale().toString(prefix->spec.sampleRate),QLocale().toString(prefix->spec.layout.channels));
        text+=tr(" Unverified audio stays in the original take.");
        if(!prefix->finalized)text+=tr(" This take was not finalized.");
        if(prefix->finitePrefixFrames<prefix->committedFrames)text+=tr(" Audio from the first invalid sample onward will not be copied.");
        QString outcome=tr("Pending");
        const NativeOriginalRecordingPrefix *native=s->reopened.get();
        if(!native && s->result && s->result->prefix)native=&*s->result->prefix;
        if(native)switch(native->status) {
        case NativeOriginalRecordingStatus::Complete:outcome=tr("Complete");break;
        case NativeOriginalRecordingStatus::Cancelled:outcome=tr("Cancelled");break;
        case NativeOriginalRecordingStatus::Failed:outcome=tr("Failed");break;
        case NativeOriginalRecordingStatus::Interrupted:outcome=tr("Interrupted");break;
        case NativeOriginalRecordingStatus::Pending:break;
        case NativeOriginalRecordingStatus::NoNativeEvidence:outcome=tr("No native outcome");break;
        }
        std::optional<PipeWireStoredDisposition> disposition;
        if(s->pipewireReopened && s->pipewireReopened->outcome)disposition=s->pipewireReopened->outcome->disposition;
        else if(s->pipewireResult && s->pipewireResult->terminalStored)switch(s->pipewireResult->disposition){
        case PipeWireRecordingDisposition::Recorded:disposition=PipeWireStoredDisposition::Recorded;break;
        case PipeWireRecordingDisposition::Cancelled:disposition=PipeWireStoredDisposition::Cancelled;break;
        case PipeWireRecordingDisposition::Failed:disposition=PipeWireStoredDisposition::Failed;break;
        }
        if(disposition)switch(*disposition){
        case PipeWireStoredDisposition::Recorded:outcome=tr("Complete");break;
        case PipeWireStoredDisposition::Cancelled:outcome=tr("Cancelled");break;
        case PipeWireStoredDisposition::Failed:outcome=tr("Failed");break;
        }
        text+=tr(" Original outcome: %1.").arg(outcome);
        copyPreview_->setText(text);
    } else copyPreview_->clear();
    open_->setEnabled(editable && project_ && !busy());
    const bool attachable=editable && saved && !s->copying && !copyPending_ && !s->exporting && !s->admitting && !admissionPending_ && bool(addRequested) && prefix && prefix->finalized && prefix->committedFrames && prefix->finitePrefixFrames==prefix->committedFrames;
    add_->setEnabled(attachable);placement_->setEnabled(attachable);
    eq_->setEnabled(editable && s->hasEqualizer() && (s->phase==NativeMonitorPhase::Ready || s->phase==NativeMonitorPhase::Recording));
    QString phase;
    switch(s->phase){
    case NativeMonitorPhase::Unsupported:phase=tr("Recording is unavailable.");break;
    case NativeMonitorPhase::Idle:phase=tr("Choose recording settings, then prepare.");break;
    case NativeMonitorPhase::Preparing:phase=tr("Preparing recording…");break;
    case NativeMonitorPhase::Reopening:phase=tr("Opening and verifying saved native take…");break;
    case NativeMonitorPhase::Ready:phase=tr("Ready to record.");break;
    case NativeMonitorPhase::Recording:phase=tr("Recording and monitoring…");break;
    case NativeMonitorPhase::Finishing:phase=tr("Finishing and saving the original take…");break;
    case NativeMonitorPhase::Result:phase=((s->result && s->result->disposition==NativeMonitorRecordingDisposition::Complete) || (s->pipewireResult && s->pipewireResult->disposition==PipeWireRecordingDisposition::Recorded))?tr("Original take saved."):tr("Recording cancelled. The retained take is available for inspection.");break;
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
    if(!s->pipewireResult && s->pipewireReopened){
        QString outcome=tr("Pending");
        if(s->pipewireReopened->outcome)switch(s->pipewireReopened->outcome->disposition){
        case PipeWireStoredDisposition::Recorded:outcome=tr("Complete");break;
        case PipeWireStoredDisposition::Cancelled:outcome=tr("Cancelled");break;
        case PipeWireStoredDisposition::Failed:outcome=tr("Failed");break;
        }
        phase=tr("Saved native take opened. Original outcome: %1.").arg(outcome);
    }
    if(s->admitting)phase=tr("Verifying original take for the timeline…");
    if(s->exporting)phase=tr("Exporting original WAV…");
    if(s->copying || copyPending_)phase=tr("Copying verified audio…");
    if(s->errorSerial!=errorSeen_){errorSeen_=s->errorSerial;if(s->error){
        if(*s->error==ErrorCode::Canceled && saved)admission_=tr("Audio copy cancelled. The retained take is unchanged.");
        else if(*s->error==ErrorCode::MediaMismatch)admission_=tr("The saved audio changed or no longer matches the reviewed take. Open and verify it again.");
        else if(*s->error==ErrorCode::ResourceLimit)admission_=tr("The recording buffer exceeds the available capacity or memory. Reduce the disk-pause budget.");
        else if(*s->error==ErrorCode::InvalidParameter)admission_=tr("The recording settings do not match the prepared device format. Check the minimum input packet size.");
        else admission_=tr("Recording error (%1).").arg(int(*s->error));}}
    status_->setText(admission_.isEmpty()?phase:admission_);status_->setToolTip(QStringLiteral("<pre>")+QString::fromUtf8(s->diagnostic.data(),qsizetype(std::min<std::size_t>(s->diagnostic.size(),4096))).toHtmlEscaped()+QStringLiteral("</pre>"));
    progress_->setText(tr("Original frames: %1 · Monitored frames: %2").arg(qulonglong(s->progress.rawAcceptedFrames)).arg(qulonglong(s->progress.deliveredMonitorFrames)));
    QString details=tr("EQ accepted: %1 · Applied: %2").arg(qulonglong(s->acceptedEqRevision)).arg(qulonglong(s->appliedEqRevision));
    if(s->pipewireReopened)details+=tr(" · Saved frames verified: %1").arg(qulonglong(s->pipewireReopened->prefix.raw.committedFrames));
    if(s->reopened)details+=tr(" · Saved frames verified: %1").arg(qulonglong(s->reopened->raw.committedFrames));
    if(s->pipewireExported)details+=tr(" · Original WAV exported: %1 frames").arg(QLocale().toString(qulonglong(s->pipewireExported->frames)));
    if(s->exported)details+=tr(" · Original WAV exported: %1 frames").arg(QLocale().toString(qulonglong(s->exported->frames)));
    if(s->exported) {
        const auto name=s->exported->destination.u8string();
        details+=QStringLiteral(" · ")+QString::fromUtf8(reinterpret_cast<const char *>(name.data()),qsizetype(name.size()));
        if(!s->exported->publicationWarning.empty())details+=tr(" · Export published with a durability warning.");
    }
    if(s->copiedPrefix) {
        const auto &copy=s->copiedPrefix->audio;const auto name=copy.destination.u8string();
        details+=tr(" · Verified audio copied: %1 frames to %2.").arg(QLocale().toString(qulonglong(copy.frames)),
            QString::fromUtf8(reinterpret_cast<const char *>(name.data()),qsizetype(name.size())));
        if(!copy.publicationWarning.empty())details+=tr(" · Copy published with a durability warning.");
    }
    receipt_->setText(details);
    // Keep wrapped paths/receipts readable in a constrained recording tab.
    receipt_->setMinimumHeight(std::max(0,receipt_->heightForWidth(std::max(1,receipt_->width()))));
    if(windowClose_ && closeReady())close();
}
void NativeMonitorPanel::cancel(){guardedPreparation_.reset();controller_.requestCancel();admission_.clear();}
bool NativeMonitorPanel::busy() const {
    const auto s=snapshot();return autosaveToken_ || guardedPreparation_ || preparePending_ || reopeningPending_ || s->hasOriginal() || s->phase==NativeMonitorPhase::Reopening || s->phase==NativeMonitorPhase::Preparing || s->phase==NativeMonitorPhase::Ready || s->phase==NativeMonitorPhase::Recording || s->phase==NativeMonitorPhase::Finishing;
}
void NativeMonitorPanel::beginClose(){if(closing_)return;closing_=true;guardedPreparation_.reset();controller_.requestShutdown();poll();}
bool NativeMonitorPanel::closeReady() const{return snapshot()->phase==NativeMonitorPhase::Closed && !autosaveToken_;}
void NativeMonitorPanel::closeEvent(QCloseEvent *event){if(closeReady()){event->accept();return;}windowClose_=true;beginClose();event->ignore();}
} // namespace soundcurrent::daw::ui
