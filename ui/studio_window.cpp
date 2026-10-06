// SPDX-License-Identifier: GPL-3.0-only
#include "studio_window.hpp"
#include "equipment_profiles.hpp"
#include <QAction>
#include <QApplication>
#include <QCloseEvent>
#include <QDoubleSpinBox>
#include <QFileDialog>
#include <QGridLayout>
#include <QGroupBox>
#include <QInputDialog>
#include <QLabel>
#include <QMenuBar>
#include <QScreen>
#include <QScrollArea>
#include <QSignalBlocker>
#include <QSlider>
#include <QStatusBar>
#include <QTimer>
#include <QWheelEvent>
#include <QMessageBox>
#include <QComboBox>
#include <QPushButton>
#include <QProgressBar>
#include <QCheckBox>
#include <QScopedValueRollback>
#include <algorithm>
#include <cmath>
namespace soundcurrent::daw::ui {
namespace {
std::filesystem::path path(const QString &value) {
#ifdef _WIN32
    return std::filesystem::path(value.toStdWString());
#else
    return utf8Path(value.toUtf8().toStdString());
#endif
}
std::string pathUtf8(const std::filesystem::path &p) {
    const auto bytes = p.u8string();
    return {reinterpret_cast<const char *>(bytes.data()), bytes.size()};
}
QString text(std::string_view value) {
    return QString::fromUtf8(value.data(), static_cast<qsizetype>(value.size()));
}
class FocusSpin : public QDoubleSpinBox {
  public:
    using QDoubleSpinBox::QDoubleSpinBox;
    void wheelEvent(QWheelEvent *event) override {
        if (hasFocus())
            QDoubleSpinBox::wheelEvent(event);
        else
            event->ignore(); // Propagate to the containing scroll area.
    }
};
class FocusCombo : public QComboBox {
  public:
    using QComboBox::QComboBox;
    void wheelEvent(QWheelEvent *event) override {
        if (hasFocus())
            QComboBox::wheelEvent(event);
        else
            event->ignore();
    }
};
QString portKey(const PipeWirePort &p) {
    return QString::number(p.nodeSerial) + QStringLiteral(":") + QString::number(p.nodeId) +
           QStringLiteral(":") + QString::number(p.portId) + QStringLiteral(":") +
           text(p.nodeName) + QStringLiteral(":") + text(p.portName);
}
class FocusSlider : public QSlider {
  public:
    using QSlider::QSlider;
    void wheelEvent(QWheelEvent *event) override {
        if (hasFocus())
            QSlider::wheelEvent(event);
        else
            event->ignore();
    }
};
} // namespace
StudioWindow::StudioWindow(QWidget *parent, PlaybackControllerOptions options,
                           RecordingControllerOptions recordingOptions,
                           ExportControllerOptions exportOptions)
    : QMainWindow(parent), playback_(std::move(options)), recording_(std::move(recordingOptions)),
      exporter_(std::move(exportOptions)) {
    setObjectName(QStringLiteral("studioWindow"));
    setWindowTitle(tr("SoundCurrent DAW"));
    auto *file = menuBar()->addMenu(tr("&File"));
    new_ = file->addAction(tr("&New project…"), QKeySequence::New, this, &StudioWindow::newProject);
    open_ = file->addAction(tr("&Open project…"), QKeySequence::Open, this, [this] {
        auto folder = QFileDialog::getExistingDirectory(this, tr("Open project folder"));
        if (!folder.isEmpty())
            openProject(path(folder));
    });
    save_ = file->addAction(tr("&Save"), QKeySequence::Save, this,
                            [this] { submitEdit({CommandKind::Save}); });
    save_->setObjectName(QStringLiteral("saveAction"));
    exportAction_ =
        file->addAction(tr("&Export WAV…"), QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_E), this,
                        [this] { requestExport(); });
    exportAction_->setObjectName("exportAudioAction");
    cancelExportAction_ =
        file->addAction(tr("Cancel export"), this, [this] { exporter_.requestCancel(); });
    cancelExportAction_->setObjectName("cancelExportAction");
    file->addSeparator();
    file->addAction(tr("&Quit"), QKeySequence::Quit, this, &QWidget::close);
    auto *editMenu = menuBar()->addMenu(tr("&Edit"));
    undo_ = editMenu->addAction(tr("&Undo parameter edit"), QKeySequence::Undo, this, [this] {
        if (auto *focused = focusWidget())
            focused->clearFocus();
        submitEdit({CommandKind::Undo});
    });
    redo_ = editMenu->addAction(tr("&Redo parameter edit"), QKeySequence::Redo, this, [this] {
        if (auto *focused = focusWidget())
            focused->clearFocus();
        submitEdit({CommandKind::Redo});
    });
    undo_->setObjectName(QStringLiteral("undoAction"));
    redo_->setObjectName(QStringLiteral("redoAction"));
    auto *equipmentMenu = menuBar()->addMenu(tr("Equipment"));
    auto *equipmentAction =
        equipmentMenu->addAction(tr("Profile library and editor…"), this, [this] {
            try {
                equipment::openLibrary(this);
            } catch (const std::exception &error) {
                QMessageBox::warning(this, tr("Equipment profiles"), text(error.what()));
            }
        });
    equipmentAction->setObjectName(QStringLiteral("equipmentLibraryAction"));
    auto *transportMenu = menuBar()->addMenu(tr("&Transport"));
    prepareAction_ =
        transportMenu->addAction(tr("Prepare playback"), this, [this] { preparePlayback(); });
    playAction_ =
        transportMenu->addAction(tr("Play / Stop"), QKeySequence(Qt::Key_Space), this, [this] {
            if (recordingBusy()) {
                recording_.requestStop();
                return;
            }
            if (playback_.snapshot()->phase == PlaybackPhase::Playing)
                playback_.requestStop();
            else
                playSelected();
        });
    stopAction_ =
        transportMenu->addAction(tr("Stop"), QKeySequence(Qt::SHIFT | Qt::Key_Space), this, [this] {
            playback_.requestStop();
            recording_.requestStop();
        });
    prepareRecordAction_ =
        transportMenu->addAction(tr("Prepare recording"), this, [this] { prepareRecording(); });
    recordAction_ = transportMenu->addAction(tr("Record"), QKeySequence(Qt::Key_R), this,
                                             &StudioWindow::recordSelected);
    recoverAction_ = file->addAction(tr("Recover recording…"), this, [this] {
        const auto model = controller_.snapshot();
        const auto start = model->session ? text(pathUtf8(model->root / "media")) : QString();
        const auto selected =
            QFileDialog::getExistingDirectory(this, tr("Choose a capture job to inspect"), start);
        if (!selected.isEmpty())
            inspectTake(path(selected));
    });
    auto *scroll = new QScrollArea(this);
    scroll->setWidgetResizable(true);
    auto *body = new QWidget(scroll);
    auto *layout = new QVBoxLayout(body);
    project_ = new QLabel(tr("Create or open a project to begin."), body);
    project_->setObjectName(QStringLiteral("projectLabel"));
    project_->setWordWrap(true);
    layout->addWidget(project_);
    auto *transport = new QGroupBox(tr("Playback"), body);
    transport->setObjectName(QStringLiteral("playbackGroup"));
    auto *transportLayout = new QVBoxLayout(transport);
    auto *buttons = new QHBoxLayout;
    prepareButton_ = new QPushButton(tr("Prepare playback"), transport);
    playButton_ = new QPushButton(tr("Play"), transport);
    stopButton_ = new QPushButton(tr("Stop"), transport);
    prepareButton_->setObjectName(QStringLiteral("preparePlaybackButton"));
    playButton_->setObjectName(QStringLiteral("playButton"));
    stopButton_->setObjectName(QStringLiteral("stopButton"));
    buttons->addWidget(prepareButton_);
    buttons->addWidget(playButton_);
    buttons->addWidget(stopButton_);
    transportLayout->addLayout(buttons);
    connect(prepareButton_, &QPushButton::clicked, this, [this] { preparePlayback(); });
    connect(playButton_, &QPushButton::clicked, this, &StudioWindow::playSelected);
    connect(stopButton_, &QPushButton::clicked, this, [this] { playback_.requestStop(); });
    auto *routes = new QWidget(transport);
    outputsLayout_ = new QGridLayout(routes);
    transportLayout->addWidget(routes);
    playbackState_ = new QLabel(transport);
    playbackState_->setObjectName(QStringLiteral("playbackStatus"));
    playbackState_->setWordWrap(true);
    transportLayout->addWidget(playbackState_);
    auto *levels = new QHBoxLayout;
    meter_ = new QProgressBar(transport);
    meter_->setObjectName(QStringLiteral("outputMeter"));
    meter_->setAccessibleName(tr("Output peak level"));
    meter_->setRange(0, 1200);
    meter_->setTextVisible(false);
    level_ = new QLabel(transport);
    level_->setObjectName(QStringLiteral("outputPeakLabel"));
    levels->addWidget(meter_, 1);
    levels->addWidget(level_);
    transportLayout->addLayout(levels);
    layout->addWidget(transport);
    auto *recording = new QGroupBox(tr("Recording"), body);
    recording->setObjectName(QStringLiteral("recordingGroup"));
    auto *recordLayout = new QVBoxLayout(recording);
    auto *recordButtons = new QHBoxLayout;
    prepareRecordButton_ = new QPushButton(tr("Prepare recording"), recording);
    recordButton_ = new QPushButton(tr("Record"), recording);
    recordStopButton_ = new QPushButton(tr("Stop recording"), recording);
    prepareRecordButton_->setObjectName(QStringLiteral("prepareRecordingButton"));
    recordButton_->setObjectName(QStringLiteral("recordButton"));
    recordStopButton_->setObjectName(QStringLiteral("recordStopButton"));
    for (auto *b : {prepareRecordButton_, recordButton_, recordStopButton_})
        recordButtons->addWidget(b);
    recordLayout->addLayout(recordButtons);
    auto *recordModes = new QHBoxLayout;
    armed_ = new QCheckBox(tr("Arm first track"), recording);
    armed_->setObjectName(QStringLiteral("armTrack"));
    monitorMode_ = new FocusCombo(recording);
    monitorMode_->setObjectName(QStringLiteral("recordMonitorMode"));
    monitorMode_->setFocusPolicy(Qt::StrongFocus);
    monitorMode_->setAccessibleName(tr("Recording monitoring mode"));
    monitorMode_->addItem(tr("Monitoring off"), int(RecordingMonitor::Off));
    monitorMode_->addItem(tr("Monitor through track EQ"), int(RecordingMonitor::PostEq));
    recordModes->addWidget(armed_);
    recordModes->addWidget(monitorMode_, 1);
    recordLayout->addLayout(recordModes);
    auto *recordRouteWidget = new QWidget(recording);
    recordRoutes_ = new QGridLayout(recordRouteWidget);
    recordLayout->addWidget(recordRouteWidget);
    recordingState_ = new QLabel(recording);
    recordingState_->setObjectName(QStringLiteral("recordingStatus"));
    recordingState_->setWordWrap(true);
    recordingState_->setTextInteractionFlags(Qt::TextSelectableByMouse);
    recordingState_->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Minimum);
    recordLayout->addWidget(recordingState_);
    inputMeter_ = new QProgressBar(recording);
    monitorMeter_ = new QProgressBar(recording);
    inputMeter_->setObjectName(QStringLiteral("inputMeter"));
    monitorMeter_->setObjectName(QStringLiteral("recordMonitorMeter"));
    inputMeter_->setAccessibleName(tr("Raw input peak level"));
    monitorMeter_->setAccessibleName(tr("Post-EQ monitoring peak level"));
    inputLevel_ = new QLabel(recording);
    monitorLevel_ = new QLabel(recording);
    for (auto pair :
         {std::pair{inputMeter_, inputLevel_}, std::pair{monitorMeter_, monitorLevel_}}) {
        pair.first->setRange(0, 1200);
        pair.first->setTextVisible(false);
        auto *levels = new QHBoxLayout;
        levels->addWidget(pair.first, 1);
        levels->addWidget(pair.second);
        recordLayout->addLayout(levels);
    }
    auto *takeButtons = new QHBoxLayout;
    retryTakeButton_ = new QPushButton(tr("Retry adding take"), recording);
    keepTakeButton_ = new QPushButton(tr("Keep take for recovery"), recording);
    retryTakeButton_->setObjectName(QStringLiteral("retryTakeButton"));
    keepTakeButton_->setObjectName(QStringLiteral("keepTakeButton"));
    takeButtons->addWidget(retryTakeButton_);
    takeButtons->addWidget(keepTakeButton_);
    recordLayout->addLayout(takeButtons);
    connect(prepareRecordButton_, &QPushButton::clicked, this, [this] { prepareRecording(); });
    connect(recordButton_, &QPushButton::clicked, this, &StudioWindow::recordSelected);
    connect(recordStopButton_, &QPushButton::clicked, this, [this] { recording_.requestStop(); });
    connect(armed_, &QCheckBox::toggled, this, [this](bool v) {
        if (!v)
            recording_.requestStop();
    });
    connect(retryTakeButton_, &QPushButton::clicked, this, &StudioWindow::retryTake);
    connect(keepTakeButton_, &QPushButton::clicked, this, [this] {
        const auto r = recording_.snapshot();
        if (r->take && recording_.acknowledgeTake(r->take->sequence)) {
            attachingTake_ = 0;
            attachmentFailed_ = false;
            notice_->setText(
                tr("Take kept for recovery in %1")
                    .arg(text(
                        pathUtf8(r->take->root / utf8Path(r->take->receipt->asset.relativePath)))));
        }
    });
    layout->addWidget(recording);
    eq_ = new QGroupBox(tr("Track equalizer"), body);
    eq_->setObjectName(QStringLiteral("equalizerGroup"));
    eq_->setLayout(new QGridLayout);
    layout->addWidget(eq_);
    track_ = new QLabel(body);
    track_->setWordWrap(true);
    layout->addWidget(track_);
    auto *exportGroup = new QGroupBox(tr("Audio export"), body);
    exportGroup->setObjectName("exportGroup");
    auto *exportLayout = new QVBoxLayout(exportGroup);
    auto *exportButtons = new QHBoxLayout;
    exportButton_ = new QPushButton(tr("Export WAV…"));
    cancelExportButton_ = new QPushButton(tr("Cancel export"));
    exportButton_->setObjectName("exportAudioButton");
    cancelExportButton_->setObjectName("cancelExportButton");
    exportButtons->addWidget(exportButton_);
    exportLayout->addLayout(exportButtons);
    exportState_ = new QLabel;
    exportState_->setObjectName("exportStatus");
    exportState_->setWordWrap(true);
    exportState_->setTextFormat(Qt::PlainText);
    exportState_->setTextInteractionFlags(Qt::TextSelectableByMouse);
    exportProgress_ = new QProgressBar;
    exportProgress_->setObjectName("exportProgress");
    exportProgress_->setAccessibleName(tr("Audio export progress"));
    exportProgress_->setRange(0, 1000);
    exportLayout->addWidget(exportState_);
    layout->addWidget(exportGroup);
    connect(exportButton_, &QPushButton::clicked, this, [this] { requestExport(); });
    connect(cancelExportButton_, &QPushButton::clicked, this,
            [this] { exporter_.requestCancel(); });
    notice_ = new QLabel(
        tr("Development preview: first-track recording and playback are available on Linux. "
           "Captured audio stays raw; track EQ affects monitoring, playback and WAV export."),
        body);
    notice_->setObjectName(QStringLiteral("previewNotice"));
    notice_->setWordWrap(true);
    layout->addWidget(notice_);
    layout->addStretch();
    scroll->setWidget(body);
    setCentralWidget(scroll);
    state_ = new QLabel(this);
    state_->setObjectName(QStringLiteral("operationStatus"));
    statusBar()->addWidget(state_, 1);
    exportProgress_->setFixedWidth(150);
    statusBar()->addPermanentWidget(exportProgress_);
    statusBar()->addPermanentWidget(cancelExportButton_);
    const auto available = screen()->availableGeometry();
    resize(std::min(1000, available.width()), std::min(640, available.height()));
    for (auto *label : {project_, track_, state_, notice_, playbackState_, level_, recordingState_,
                        inputLevel_, monitorLevel_})
        label->setTextFormat(Qt::PlainText);
    timer_ = new QTimer(this);
    timer_->setInterval(16);
    connect(timer_, &QTimer::timeout, this, &StudioWindow::poll);
    timer_->start();
    poll();
}
std::shared_ptr<const ControllerSnapshot> StudioWindow::snapshot() const {
    return controller_.snapshot();
}
std::shared_ptr<const ExportSnapshot> StudioWindow::exportSnapshot() const {
    return exporter_.snapshot();
}
bool StudioWindow::exportWorkflowBusy() const {
    return exportBarrier_ || exportSelection_ || exportDialog_ || exporter_.snapshot()->busy;
}
bool StudioWindow::requestExport() {
    const auto m = controller_.snapshot();
    if (exportWorkflowBusy() || closing_ || closeRequested_ || closeAfterSave_ || !m->session ||
        m->session->tracks.empty() || m->io == IoOperation::Create || m->io == IoOperation::Open ||
        attachingTake_)
        return false;
    if (auto *focused = focusWidget())
        focused->clearFocus();
    ProjectCommand barrier{CommandKind::Barrier};
    barrier.barrier = nextGesture_++;
    const auto token = barrier.barrier;
    if (!submitEdit(std::move(barrier)))
        return false;
    exportBarrier_ = token;
    return true;
}
void StudioWindow::pollExport() {
    const auto m = controller_.snapshot();
    if (exportBarrier_ && m->lastBarrier == exportBarrier_ && !closing_ && !closeRequested_) {
        exportBarrier_ = 0;
        if (m->barrierSession && !m->barrierSession->tracks.empty()) {
            if (exportSelection_) {
                ExportRequest request(exportSelection_->spec);
                request.destination = exportSelection_->destination;
                request.session = m->barrierSession;
                request.root = m->barrierRoot;
                request.modelRevision = m->barrierRevision;
                exportSelection_.reset();
                if (exporter_.submit(std::move(request)) != Admission::Accepted)
                    notice_->setText(tr("An export is already running or closing. Please retry."));
            } else {
                ExportDialog dialog(m->barrierRoot, m->barrierSession, this);
                exportDialog_ = &dialog;
                const auto answer = dialog.exec();
                exportDialog_.clear();
                if (answer == QDialog::Accepted && dialog.selection() && !closing_ &&
                    !closeRequested_) {
                    exportSelection_ = *dialog.selection();
                    ProjectCommand capture{CommandKind::Barrier};
                    capture.barrier = nextGesture_++;
                    const auto token = capture.barrier;
                    if (submitEdit(std::move(capture)))
                        exportBarrier_ = token;
                    else
                        exportSelection_.reset();
                }
            }
        } else
            exportSelection_.reset();
    }
    const auto e = exporter_.snapshot();
    if (e->phase == ExportPhase::AwaitingConfirmation && e->replacement &&
        exportPromptJob_ != e->job && !exportPrompt_ && !closeRequested_ && !closing_) {
        exportPromptJob_ = e->job;
        auto *box = new QMessageBox(
            QMessageBox::Question, tr("Replace existing file?"),
            tr("Replace this file with the new audio export?\n%1\nCurrent size: %2 bytes")
                .arg(text(pathUtf8(e->replacement->path)),
                     QLocale().toString(static_cast<qulonglong>(e->replacement->bytes))),
            QMessageBox::Yes | QMessageBox::No, this);
        box->setObjectName("exportOverwritePrompt");
        box->setTextFormat(Qt::PlainText);
        box->setDefaultButton(QMessageBox::No);
        box->setWindowModality(Qt::WindowModal);
        box->setAttribute(Qt::WA_DeleteOnClose);
        exportPrompt_ = box;
        connect(box, &QMessageBox::finished, this, [this, job = e->job](int answer) {
            exporter_.confirm(job, answer == QMessageBox::Yes && !closeRequested_ && !closing_);
        });
        box->open();
    }
    if (exportPrompt_ &&
        (e->phase != ExportPhase::AwaitingConfirmation || closeRequested_ || e->cancelRequested))
        exportPrompt_->done(QMessageBox::No);
    const bool allow = !closing_ && !closeRequested_ && !closeAfterSave_ && !exportWorkflowBusy() &&
                       m->session && !m->session->tracks.empty() && m->io != IoOperation::Create &&
                       m->io != IoOperation::Open && !attachingTake_;
    exportAction_->setEnabled(allow);
    exportButton_->setEnabled(allow);
    cancelExportAction_->setEnabled(e->busy && !closing_ && !closeRequested_);
    cancelExportButton_->setEnabled(cancelExportAction_->isEnabled());
    exportProgress_->setVisible(e->busy);
    cancelExportButton_->setVisible(e->busy);
    if (e->busy && e->maximum <= 0)
        exportProgress_->setRange(0, 0);
    else {
        exportProgress_->setRange(0, 1000);
        exportProgress_->setValue(
            e->maximum > 0 ? int(std::clamp(double(e->written) / double(e->maximum), 0., 1.) * 1000)
                           : 0);
    }
    QString status;
    switch (e->phase) {
    case ExportPhase::Idle:
        status = tr("Export a track through its EQ to float32 WAV.");
        break;
    case ExportPhase::Queued:
        status = tr("Export queued…");
        break;
    case ExportPhase::Inspecting:
        status = tr("Checking the export destination…");
        break;
    case ExportPhase::AwaitingConfirmation:
        status = tr("Waiting for replacement confirmation.");
        break;
    case ExportPhase::Rendering:
        status = tr("Rendering snapshot %1 — %2 frames written")
                     .arg(QLocale().toString(static_cast<qulonglong>(e->modelRevision)),
                          QLocale().toString(static_cast<qlonglong>(e->written)));
        break;
    case ExportPhase::Complete:
        status = tr("Export complete: %1").arg(text(pathUtf8(e->destination)));
        if (e->result) {
            const auto &r = *e->result;
            status += tr("\n%1 frames · peak %2 dBFS")
                          .arg(QLocale().toString(static_cast<qlonglong>(r.frames)),
                               r.peak > 0 ? QLocale().toString(20 * std::log10(r.peak), 'f', 1)
                                          : tr("−∞"));
            if (r.overFullScaleSamples)
                status += tr("\nFloat levels exceed 0 dBFS; they were preserved.");
            if (r.tailTruncated)
                status += tr("\nThe tail reached its time limit.");
            if (r.durability == Durability::FileFlushed)
                status += tr("\nFile flushed; directory durability is not confirmed.");
            if (!r.publicationWarning.empty())
                status += tr("\nThe complete file was published with a warning: %1")
                              .arg(text(r.publicationWarning));
        }
        break;
    case ExportPhase::Canceled:
        status = tr("Export canceled; no new file was published.");
        break;
    case ExportPhase::Fault:
        status = tr("Export failed: %1").arg(text(e->diagnostic));
        break;
    case ExportPhase::Closing:
        status = tr("Closing export…");
        break;
    case ExportPhase::Closed:
        status = tr("Export worker closed.");
        break;
    }
    if (e->busy && e->cancelRequested)
        status = tr("Canceling export; waiting for the current file operation…");
    exportState_->setText(status);
}
bool StudioWindow::submitEdit(ProjectCommand command) {
    const auto admission = controller_.submit(std::move(command));
    if (admission != Admission::Accepted) {
        state_->setText(admission == Admission::Full ? tr("Too many pending changes. Please retry.")
                                                     : tr("Closing project…"));
        return false;
    }
    return true;
}
void StudioWindow::openProject(const std::filesystem::path &root) {
    if (recordingBusy() || attachingTake_ || exportWorkflowBusy()) {
        notice_->setText(tr("Finish recording, pending take attachment and export before opening "
                            "another project."));
        return;
    }
    ProjectCommand command;
    command.kind = CommandKind::Open;
    command.path = root;
    playback_.requestStop();
    submitEdit(std::move(command));
}
void StudioWindow::newProject() {
    if (recordingBusy() || attachingTake_ || exportWorkflowBusy())
        return;
    const auto parent =
        QFileDialog::getExistingDirectory(this, tr("Choose a folder for the new project"));
    if (parent.isEmpty())
        return;
    bool accepted = false;
    const auto name = QInputDialog::getText(this, tr("New project"), tr("Project name:"),
                                            QLineEdit::Normal, tr("Untitled"), &accepted);
    if (!accepted || name.trimmed().isEmpty())
        return;
    if (name == QStringLiteral(".") || name == QStringLiteral("..") || name.contains('/') ||
        name.contains('\\')) {
        state_->setText(tr("Choose a project name without folder separators."));
        return;
    }
    ProjectCommand command;
    command.kind = CommandKind::Create;
    command.path = path(parent) / path(name);
    command.name = name.toUtf8().toStdString();
    playback_.requestStop();
    submitEdit(std::move(command));
}

std::shared_ptr<const PlaybackSnapshot> StudioWindow::playbackSnapshot() const {
    return playback_.snapshot();
}
bool StudioWindow::preparePlayback() {
    const auto view = controller_.snapshot();
    if (!view->session || closing_ || closeRequested_ || closeAfterSave_ ||
        view->io == IoOperation::Create || view->io == IoOperation::Open || recordingBusy() ||
        attachingTake_)
        return false;
    PlaybackCommand c;
    c.root = view->root;
    c.session = view->session;
    c.modelRevision = view->modelRevision;
    const auto admitted = playback_.submit(std::move(c));
    if (admitted != Admission::Accepted) {
        playbackState_->setText(tr("Playback queue is full or closing. Please retry."));
        return false;
    }
    return true;
}
void StudioWindow::playSelected() {
    if (playback_.snapshot()->phase != PlaybackPhase::Ready || !outputsShown_)
        return;
    PlaybackCommand c;
    c.kind = PlaybackCommandKind::Play;
    for (auto *combo : outputs_) {
        const auto key = combo->currentData().toString();
        const auto found = std::find_if(outputsShown_->begin(), outputsShown_->end(),
                                        [&](const auto &p) { return portKey(p) == key; });
        if (found == outputsShown_->end()) {
            playbackState_->setText(tr("Choose an output for every channel."));
            return;
        }
        c.outputs.push_back(*found);
    }
    if (playback_.submit(std::move(c)) != Admission::Accepted)
        playbackState_->setText(tr("Playback queue is full or closing. Please retry."));
}
void StudioWindow::updateOutputs(const PlaybackSnapshot &view) {
    const auto channels = view.ports ? view.channels : 0;
    const bool rebuild = outputs_.size() != channels;
    if (rebuild) {
        while (auto *item = outputsLayout_->takeAt(0)) {
            delete item->widget();
            delete item;
        }
        outputs_.clear();
        outputsShown_.reset();
        for (std::uint32_t c = 0; c < channels; ++c) {
            auto *combo = new FocusCombo;
            combo->setObjectName(QStringLiteral("outputChannel%1").arg(c));
            combo->setFocusPolicy(Qt::StrongFocus);
            combo->setAccessibleName(tr("Output channel %1").arg(QLocale().toString(c + 1)));
            combo->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
            combo->setMinimumContentsLength(16);
            auto *label = new QLabel(tr("Output %1").arg(QLocale().toString(c + 1)));
            label->setBuddy(combo);
            outputsLayout_->addWidget(label, int(c), 0);
            outputsLayout_->addWidget(combo, int(c), 1);
            outputs_.push_back(combo);
        }
    }
    if (view.ports && (!outputsShown_ || *outputsShown_ != *view.ports)) {
        for (auto *combo : outputs_) {
            const auto previous = combo->currentData().toString();
            QSignalBlocker blocked(combo);
            combo->clear();
            combo->addItem(tr("Choose an output…"), QString());
            for (const auto &p : *view.ports)
                combo->addItem(text(p.nodeName) + QStringLiteral(" / ") + text(p.portName),
                               portKey(p));
            const auto index = combo->findData(previous);
            combo->setCurrentIndex(index >= 0 ? index : 0);
        }
        outputsShown_ = view.ports;
    }
    for (auto *combo : outputs_)
        combo->setEnabled(view.phase == PlaybackPhase::Ready && !closing_);
}
void StudioWindow::pollPlayback() {
    const auto p = playback_.snapshot();
    const auto model = controller_.snapshot();
    updateOutputs(*p);
    const bool allow = !closing_ && !closeRequested_ && !closeAfterSave_;
    const bool idle = p->phase == PlaybackPhase::Idle || p->phase == PlaybackPhase::Ready ||
                      p->phase == PlaybackPhase::Complete || p->phase == PlaybackPhase::Fault;
    const bool prepare = allow && !recordingBusy() && !attachingTake_ && p->supported && idle &&
                         model->session && model->io != IoOperation::Create &&
                         model->io != IoOperation::Open;
    prepareButton_->setEnabled(prepare);
    prepareAction_->setEnabled(prepare);
    playButton_->setEnabled(allow && p->phase == PlaybackPhase::Ready);
    playAction_->setEnabled(
        allow && ((p->phase == PlaybackPhase::Ready || p->phase == PlaybackPhase::Playing) ||
                  recordingBusy()));
    const bool stoppable =
        allow && (p->phase == PlaybackPhase::Preparing || p->phase == PlaybackPhase::Ready ||
                  p->phase == PlaybackPhase::Playing || p->phase == PlaybackPhase::Complete);
    stopButton_->setEnabled(stoppable);
    stopAction_->setEnabled(stoppable);
    QString status;
    switch (p->phase) {
    case PlaybackPhase::Unsupported:
        status = tr("Native playback is not available in this build.");
        break;
    case PlaybackPhase::Idle:
        status = tr("Prepare the first audio track, choose outputs, then play.");
        break;
    case PlaybackPhase::Preparing:
        status = tr("Preparing playback…");
        break;
    case PlaybackPhase::Ready:
        status = tr("Choose outputs for every channel, then play.");
        break;
    case PlaybackPhase::Playing:
        status = p->pending ? tr("Playing — EQ changes pending")
                            : tr("Playing — EQ changes acknowledged");
        break;
    case PlaybackPhase::Stopping:
        status = tr("Stopping playback…");
        break;
    case PlaybackPhase::Complete:
        status = tr("Playback complete");
        break;
    case PlaybackPhase::Fault:
        status = tr("Playback stopped: %1").arg(text(p->diagnostic));
        break;
    case PlaybackPhase::Closing:
        status = tr("Closing playback…");
        break;
    case PlaybackPhase::Closed:
        status = tr("Playback closed");
        break;
    }
    if (p->sampleRate)
        status += tr(" · %1 s · %2 missing frames")
                      .arg(QLocale().toString(double(p->position) / p->sampleRate, 'f', 2),
                           QLocale().toString(p->missingFrames));
    playbackState_->setText(status);
    const auto peak = std::isfinite(p->peak) ? std::max(0.0, p->peak) : 0.0;
    meter_->setValue(int(std::lround(std::min(1.2, peak) * 1000)));
    const auto color = peak >= 1     ? QStringLiteral("#c83434")
                       : peak >= .85 ? QStringLiteral("#c78a12")
                                     : QStringLiteral("#28894e");
    const auto style = QStringLiteral("QProgressBar::chunk { background: %1; }").arg(color);
    if (meter_->styleSheet() != style)
        meter_->setStyleSheet(style);
    level_->setText(
        peak > 0 ? tr("Output: %1 dBFS").arg(QLocale().toString(20 * std::log10(peak), 'f', 1))
                 : tr("Output: −∞ dBFS"));
    if (p->errorSerial != playbackError_) {
        playbackError_ = p->errorSerial;
        notice_->setText(tr("Playback could not be completed: %1").arg(text(p->diagnostic)));
    }
}
bool StudioWindow::recordingBusy() const {
    const auto r = recording_.snapshot();
    return recordCommandPending_ || r->take ||
           (r->phase != RecordingPhase::Idle && r->phase != RecordingPhase::Unsupported &&
            r->phase != RecordingPhase::Fault && r->phase != RecordingPhase::Closed);
}
std::shared_ptr<const RecordingSnapshot> StudioWindow::recordingSnapshot() const {
    return recording_.snapshot();
}
bool StudioWindow::submitRecording(RecordingCommand c) {
    if (recordCommandPending_)
        return false;
    const auto r = recording_.snapshot();
    if (recording_.submit(std::move(c)) != Admission::Accepted)
        return false;
    recordCommandPending_ = true;
    recordCommandCompleted_ = r->completedCommands;
    recordCommandError_ = r->errorSerial;
    recordCommandStop_ = r->stopAcknowledged;
    return true;
}
bool StudioWindow::prepareRecording() {
    const auto m = controller_.snapshot();
    const auto p = playback_.snapshot();
    if (!m->session || m->io != IoOperation::None || closing_ || closeRequested_ ||
        closeAfterSave_ || recording_.snapshot()->take || attachingTake_ ||
        (p->phase != PlaybackPhase::Idle && p->phase != PlaybackPhase::Fault &&
         p->phase != PlaybackPhase::Unsupported))
        return false;
    RecordingCommand c;
    c.root = m->root;
    c.session = m->session;
    c.modelRevision = m->modelRevision;
    c.monitoring = static_cast<RecordingMonitor>(monitorMode_->currentData().toInt());
    if (!submitRecording(std::move(c))) {
        recordingState_->setText(tr("Recording queue is full or closing. Please retry."));
        return false;
    }
    return true;
}
void StudioWindow::recordSelected() {
    const auto r = recording_.snapshot();
    if (!armed_->isChecked() || r->phase != RecordingPhase::Ready || !recordPortsShown_ ||
        closing_ || closeRequested_)
        return;
    RecordingCommand c;
    c.kind = RecordingCommandKind::Start;
    c.armed = true;
    auto selected = [&](const std::vector<QComboBox *> &combos, bool input,
                        std::vector<PipeWirePort> &ports) {
        for (auto *combo : combos) {
            const auto key = combo->currentData().toString();
            const auto found =
                std::find_if(recordPortsShown_->begin(), recordPortsShown_->end(),
                             [&](const auto &p) { return p.input == input && portKey(p) == key; });
            if (found == recordPortsShown_->end())
                return false;
            ports.push_back(*found);
        }
        return true;
    };
    if (!selected(inputs_, false, c.inputs) || !selected(monitors_, true, c.outputs)) {
        recordingState_->setText(tr("Choose an input and every required monitoring output."));
        return;
    }
    if (!submitRecording(std::move(c)))
        recordingState_->setText(tr("Recording queue is full or closing. Please retry."));
}
bool StudioWindow::inspectTake(const std::filesystem::path &job) {
    const auto m = controller_.snapshot();
    if (!m->session || m->io != IoOperation::None || recordingBusy() || attachingTake_ ||
        closing_ || closeRequested_)
        return false;
    RecordingCommand c;
    c.kind = RecordingCommandKind::Inspect;
    c.root = m->root;
    c.session = m->session;
    c.modelRevision = m->modelRevision;
    c.job = job;
    return submitRecording(std::move(c));
}
void StudioWindow::retryTake() {
    if (recording_.snapshot()->take && !attachingTake_)
        attachmentFailed_ = false;
}
void StudioWindow::updateRecordingRoutes(const RecordingSnapshot &r) {
    const auto n = r.ports ? r.channels : 0;
    const auto out = r.monitoring == RecordingMonitor::PostEq ? n : 0;
    if (inputs_.size() != n || monitors_.size() != out) {
        while (auto *item = recordRoutes_->takeAt(0)) {
            delete item->widget();
            delete item;
        }
        inputs_.clear();
        monitors_.clear();
        recordPortsShown_.reset();
        auto make = [&](std::vector<QComboBox *> &combos, bool input, std::uint32_t count) {
            for (std::uint32_t c = 0; c < count; ++c) {
                auto *combo = new FocusCombo;
                combo->setObjectName(
                    (input ? QStringLiteral("monitorChannel%1") : QStringLiteral("inputChannel%1"))
                        .arg(c));
                combo->setFocusPolicy(Qt::StrongFocus);
                const auto label = input ? tr("Monitor output %1") : tr("Input %1");
                combo->setAccessibleName(label.arg(QLocale().toString(c + 1)));
                combo->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
                combo->setMinimumContentsLength(16);
                auto *buddy = new QLabel(combo->accessibleName());
                buddy->setBuddy(combo);
                const auto row = int(c + (input ? n : 0));
                recordRoutes_->addWidget(buddy, row, 0);
                recordRoutes_->addWidget(combo, row, 1);
                combos.push_back(combo);
            }
        };
        make(inputs_, false, n);
        make(monitors_, true, out);
    }
    if (r.ports && (!recordPortsShown_ || *recordPortsShown_ != *r.ports)) {
        auto populate = [&](const std::vector<QComboBox *> &combos, bool input) {
            for (auto *combo : combos) {
                const auto previous = combo->currentData().toString();
                QSignalBlocker block(combo);
                combo->clear();
                combo->addItem(input ? tr("Choose a monitoring output…") : tr("Choose an input…"),
                               QString());
                for (const auto &p : *r.ports)
                    if (p.input == input)
                        combo->addItem(text(p.nodeName) + QStringLiteral(" / ") + text(p.portName),
                                       portKey(p));
                const auto index = combo->findData(previous);
                combo->setCurrentIndex(index < 0 ? 0 : index);
            }
        };
        populate(inputs_, false);
        populate(monitors_, true);
        recordPortsShown_ = r.ports;
    }
    for (const auto &combos : {inputs_, monitors_})
        for (auto *combo : combos)
            combo->setEnabled(r.phase == RecordingPhase::Ready && !closing_ && !closeRequested_);
}
void StudioWindow::pollRecording() {
    const auto r = recording_.snapshot();
    const auto m = controller_.snapshot();
    const auto p = playback_.snapshot();
    if (recordCommandPending_ &&
        (r->completedCommands > recordCommandCompleted_ || r->errorSerial > recordCommandError_ ||
         r->stopAcknowledged > recordCommandStop_))
        recordCommandPending_ = false;
    if (m->session && m->modelRevision > recordingFollowed_ &&
        recording_.follow(m->root, m->session, m->modelRevision))
        recordingFollowed_ = m->modelRevision;
    if (r->take) {
        if (takeShown_ != r->take->sequence) {
            takeShown_ = r->take->sequence;
            attachingTake_ = 0;
            attachmentFailed_ = false;
        }
        if (m->root == r->take->root && m->lastAttachedAsset == r->take->receipt->asset.id) {
            recording_.acknowledgeTake(r->take->sequence);
            attachingTake_ = 0;
            attachmentFailed_ = false;
        } else if (attachingTake_ && m->errorSerial > attachmentError_) {
            attachingTake_ = 0;
            attachmentFailed_ = true;
        } else if (!attachingTake_ && !attachmentFailed_ && m->io == IoOperation::None &&
                   !closing_) {
            if (!m->session || m->root != r->take->root ||
                m->session->id != r->take->receipt->spec.projectId)
                attachmentFailed_ = true;
            else {
                ProjectCommand c{CommandKind::AttachRecording};
                c.path = r->take->root;
                c.recording = r->take->receipt;
                if (controller_.submit(std::move(c)) == Admission::Accepted) {
                    attachingTake_ = r->take->sequence;
                    attachmentError_ = m->errorSerial;
                }
            }
        }
    }
    retryTakeButton_->setVisible(bool(r->take) && attachmentFailed_);
    keepTakeButton_->setVisible(bool(r->take) && attachmentFailed_);
    retryTakeButton_->setEnabled(m->io == IoOperation::None && !attachingTake_);
    keepTakeButton_->setEnabled(m->io == IoOperation::None && !attachingTake_);
    const bool allow = !closing_ && !closeRequested_ && !closeAfterSave_;
    const bool playbackIdle = p->phase == PlaybackPhase::Idle || p->phase == PlaybackPhase::Fault ||
                              p->phase == PlaybackPhase::Unsupported;
    const bool idle = r->phase == RecordingPhase::Idle || r->phase == RecordingPhase::Fault ||
                      r->phase == RecordingPhase::Unsupported;
    const bool ready = r->phase == RecordingPhase::Ready;
    const bool prepare = allow && !recordCommandPending_ && r->supported && (idle || ready) &&
                         !r->take && !attachingTake_ && playbackIdle && m->session &&
                         m->io == IoOperation::None;
    prepareRecordButton_->setEnabled(prepare);
    prepareRecordAction_->setEnabled(prepare);
    monitorMode_->setEnabled(allow && idle && !r->take);
    armed_->setEnabled(allow && r->supported && m->session && !r->take);
    recordButton_->setEnabled(allow && !recordCommandPending_ && ready && armed_->isChecked());
    recordAction_->setEnabled(recordButton_->isEnabled());
    recordStopButton_->setEnabled(allow && !idle && !r->closed);
    recoverAction_->setEnabled(allow && !recordCommandPending_ && idle && !r->take &&
                               !attachingTake_ && m->session && m->io == IoOperation::None);
    updateRecordingRoutes(*r);
    QString status;
    switch (r->phase) {
    case RecordingPhase::Unsupported:
        status = tr("Native recording is not available in this build. Stored takes can still be "
                    "recovered.");
        break;
    case RecordingPhase::Idle:
        status = tr("Choose monitoring, prepare, select inputs, arm, then record.");
        break;
    case RecordingPhase::Preparing:
        status = tr("Preparing recording…");
        break;
    case RecordingPhase::Ready:
        status = tr("Choose every required channel and arm the track.");
        break;
    case RecordingPhase::Recording:
        status = r->pending ? tr("Recording — EQ changes pending")
                            : tr("Recording — EQ changes acknowledged");
        break;
    case RecordingPhase::Complete:
        status = tr("Recording range complete. Stop to add the take.");
        break;
    case RecordingPhase::Finalizing:
        status = tr("Finalizing recorded audio…");
        break;
    case RecordingPhase::Inspecting:
        status = tr("Verifying recovery checkpoint…");
        break;
    case RecordingPhase::Recovering:
        status = tr("Recovering a copy of the verified take…");
        break;
    case RecordingPhase::Fault:
        status = tr("Recording stopped: %1").arg(text(r->diagnostic));
        break;
    case RecordingPhase::Closing:
        status = tr("Closing recording…");
        break;
    case RecordingPhase::Closed:
        status = tr("Recording closed");
        break;
    }
    if (r->take)
        status = attachmentFailed_ ? tr("Take could not be added. Retry, or keep it for recovery.")
                                   : tr("Adding verified take to the project…");
    if (r->sampleRate)
        status += tr(" · %1 s · %2 rejected frames")
                      .arg(QLocale().toString(double(r->telemetry.capturedFrames) / r->sampleRate,
                                              'f', 2),
                           QLocale().toString(r->telemetry.rejectedFrames));
    if (r->job && (r->phase == RecordingPhase::Fault || attachmentFailed_))
        status += tr("\nStored recording: %1").arg(text(pathUtf8(*r->job)));
    recordingState_->setText(status);
    recordingState_->setToolTip(r->job ? text(pathUtf8(*r->job)) : QString());
    // Independent text measurement lets a shorter diagnostic shrink again.
    const auto textHeight = recordingState_->fontMetrics()
                                .boundingRect(0, 0, std::max(1, recordingState_->width()), 100000,
                                              Qt::TextWordWrap | Qt::AlignLeft, status)
                                .height();
    recordingState_->setMinimumHeight(
        std::max(recordingState_->fontMetrics().height(), textHeight) +
        2 * recordingState_->margin());
    auto meter = [&](QProgressBar *bar, QLabel *label, double level, const QString &name) {
        const auto peak = std::isfinite(level) ? std::max(0.0, level) : 0.;
        bar->setValue(int(std::lround(std::min(1.2, peak) * 1000)));
        const auto color = peak >= 1     ? QStringLiteral("#c83434")
                           : peak >= .85 ? QStringLiteral("#c78a12")
                                         : QStringLiteral("#28894e");
        const auto style = QStringLiteral("QProgressBar::chunk { background: %1; }").arg(color);
        if (bar->styleSheet() != style)
            bar->setStyleSheet(style);
        label->setText(
            peak > 0
                ? tr("%1: %2 dBFS").arg(name, QLocale().toString(20 * std::log10(peak), 'f', 1))
                : tr("%1: −∞ dBFS").arg(name));
    };
    meter(inputMeter_, inputLevel_, r->telemetry.inputPeak, tr("Input"));
    meter(monitorMeter_, monitorLevel_,
          r->monitoring == RecordingMonitor::Off ? 0 : r->telemetry.outputPeak, tr("Monitor"));
    if (r->errorSerial != recordingError_) {
        recordingError_ = r->errorSerial;
        notice_->setText(tr("Recording could not be completed: %1. Any stored checkpoint remains "
                            "available for recovery.")
                             .arg(text(r->diagnostic)));
    }
    if (r->preview && r->previewSequence != previewShown_) {
        previewShown_ = r->previewSequence;
        if (!allow)
            return;
        const auto answer = QMessageBox::question(
            this, tr("Recover recording"),
            tr("Verified %1 frames (%2 seconds). Recover a new copy into this project? The "
               "original recording is preserved.")
                .arg(QLocale().toString(r->preview->committedFrames),
                     QLocale().toString(double(r->preview->committedFrames) /
                                            r->preview->spec.capture.sampleRate,
                                        'f', 2)),
            QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
        if (answer == QMessageBox::Yes && m->session && r->job) {
            RecordingCommand c;
            c.kind = RecordingCommandKind::Recover;
            c.root = m->root;
            c.session = m->session;
            c.modelRevision = m->modelRevision;
            c.job = *r->job;
            c.previewSequence = r->previewSequence;
            if (!submitRecording(std::move(c)))
                notice_->setText(tr("Recovery queue is full or closing. Please retry."));
        }
    }
}

void StudioWindow::shutdownWorkers() {
    exporter_.requestShutdown();
    playback_.requestShutdown();
    recording_.requestShutdown();
    controller_.requestShutdown();
}

void StudioWindow::rebuildBands(const Session &session) {
    auto *layout = static_cast<QGridLayout *>(eq_->layout());
    while (auto *item = layout->takeAt(0)) {
        if (item->widget())
            item->widget()->blockSignals(true);
        delete item->widget();
        delete item;
    }
    bands_.clear();
    if (session.tracks.empty())
        return;
    const auto &track = session.tracks.front();
    bands_.resize(track.eq.bands.size());
    const QStringList headings{tr("Band"), tr("Frequency (Hz)"), tr("Gain (dB)"), tr("Gain"),
                               tr("Q")};
    for (int column = 0; column < headings.size(); ++column)
        layout->addWidget(new QLabel(headings[column], eq_), 0, column);
    for (std::size_t i = 0; i < bands_.size(); ++i) {
        auto &editor = bands_[i];
        auto makeSpin = [&](BandParameter p, const QString &label, int column) {
            auto *spin = new FocusSpin(eq_);
            spin->setKeyboardTracking(true);
            spin->setFocusPolicy(Qt::StrongFocus);
            spin->setAccessibleName(label);
            spin->setObjectName(QString::fromLatin1(descriptor(p).stableId.data(),
                                                    qsizetype(descriptor(p).stableId.size())) +
                                QString::number(i));
            const auto d = descriptor(p);
            spin->setRange(d.minimum,
                           p == BandParameter::FrequencyHz
                               ? std::min(d.maximum, double(session.sampleRate) / 2 - .01)
                               : d.maximum);
            spin->setDecimals(p == BandParameter::FrequencyHz ? 1 : 2);
            spin->setSingleStep(p == BandParameter::FrequencyHz ? 10
                                : p == BandParameter::GainDb    ? .5
                                                                : .1);
            spin->setValue(
                parameterValue(session, {track.id, track.eq.id, track.eq.bands[i].id, p}));
            layout->addWidget(spin, int(i + 1), column);
            connect(spin, &QDoubleSpinBox::valueChanged, this,
                    [this, i, p](double value) { edit(i, p, value); });
            connect(spin, &QDoubleSpinBox::editingFinished, this,
                    [this, i, p, spin] { edit(i, p, spin->value(), true); });
            return spin;
        };
        const auto label = tr("Band %1").arg(QLocale().toString(static_cast<qulonglong>(i + 1)));
        layout->addWidget(new QLabel(label, eq_), int(i + 1), 0);
        editor.frequency = makeSpin(BandParameter::FrequencyHz, tr("%1 frequency").arg(label), 1);
        editor.gain = makeSpin(BandParameter::GainDb, tr("%1 gain").arg(label), 2);
        editor.q = makeSpin(BandParameter::Q, tr("%1 Q").arg(label), 4);
        editor.slider = new FocusSlider(Qt::Horizontal, eq_);
        editor.slider->setObjectName(QStringLiteral("gainSlider%1").arg(i));
        editor.slider->setRange(-240, 240);
        editor.slider->setValue(int(std::lround(track.eq.bands[i].gainDb * 10)));
        editor.slider->setAccessibleName(tr("%1 gain slider").arg(label));
        editor.slider->setFocusPolicy(Qt::StrongFocus);
        layout->addWidget(editor.slider, int(i + 1), 3);
        editor.frequencyAddress = ParameterAddress{track.id, track.eq.id, track.eq.bands[i].id,
                                                   BandParameter::FrequencyHz};
        editor.gainAddress =
            ParameterAddress{track.id, track.eq.id, track.eq.bands[i].id, BandParameter::GainDb};
        editor.qAddress =
            ParameterAddress{track.id, track.eq.id, track.eq.bands[i].id, BandParameter::Q};
        connect(editor.slider, &QSlider::valueChanged, this, [this, i](int value) {
            edit(i, BandParameter::GainDb, value / 10., !bands_[i].slider->isSliderDown(), true);
        });
        connect(editor.slider, &QSlider::sliderReleased, this, [this, i] {
            edit(i, BandParameter::GainDb, bands_[i].slider->value() / 10., true, true);
        });
    }
    layout->setColumnStretch(3, 1);
}
void StudioWindow::edit(std::size_t band, BandParameter parameter, double value, bool final,
                        bool slider) {
    if (band >= bands_.size() || closing_ || closeAfterSave_ || closeRequested_)
        return;
    auto &e = bands_[band];
    auto &gesture = slider                                    ? e.sliderGesture
                    : parameter == BandParameter::FrequencyHz ? e.frequencyGesture
                    : parameter == BandParameter::GainDb      ? e.gainGesture
                                                              : e.qGesture;
    const auto &address = parameter == BandParameter::FrequencyHz ? e.frequencyAddress
                          : parameter == BandParameter::GainDb    ? e.gainAddress
                                                                  : e.qAddress;
    if (!gesture)
        gesture = nextGesture_++;
    ProjectCommand command;
    command.kind = CommandKind::Parameter;
    command.address = address;
    command.gesture = gesture;
    command.value = value;
    command.final = final;
    if (submitEdit(std::move(command)) && final)
        gesture = 0;
}
void StudioWindow::updateBands(const Session &session) {
    if (session.tracks.empty())
        return;
    const auto &track = session.tracks.front();
    for (std::size_t i = 0; i < bands_.size(); ++i) {
        auto &e = bands_[i];
        const auto &b = track.eq.bands[i];
        auto update = [](QDoubleSpinBox *spin, double value) {
            if (!spin->hasFocus()) {
                QSignalBlocker block(spin);
                spin->setValue(value);
            }
        };
        update(e.frequency, b.frequencyHz);
        update(e.gain, b.gainDb);
        update(e.q, b.q);
        if (!e.slider->isSliderDown() && !e.slider->hasFocus()) {
            QSignalBlocker block(e.slider);
            e.slider->setValue(int(std::lround(b.gainDb * 10)));
        }
    }
}
void StudioWindow::poll() {
    const auto view = controller_.snapshot();
    pollRecording();
    pollPlayback();
    pollExport();
    if (view->closed && playback_.snapshot()->closed && recording_.snapshot()->closed &&
        exporter_.snapshot()->closed && closing_) {
        close();
        return;
    }
    if (view->errorSerial != lastError_) {
        lastError_ = view->errorSerial;
        closeAfterSave_ = false;
        closeSaveSubmitted_ = false;
        closeRequested_ = false;
        closeBarrier_ = 0;
        exportBarrier_ = 0;
        exportSelection_.reset();
        notice_->setText(
            tr("The operation could not be completed: %1").arg(text(view->diagnostic)));
    }
    if (!shown_ || shown_->session != view->session) {
        if (view->session) {
            bool rebuild = !shown_ || !shown_->session ||
                           shown_->session->id != view->session->id ||
                           bands_.size() != (view->session->tracks.empty()
                                                 ? 0
                                                 : view->session->tracks.front().eq.bands.size());
            if (!rebuild && !view->session->tracks.empty()) {
                const auto &track = view->session->tracks.front();
                rebuild = shown_->session->sampleRate != view->session->sampleRate;
                for (std::size_t i = 0; i < bands_.size(); ++i)
                    rebuild = rebuild || !bands_[i].gainAddress ||
                              bands_[i].gainAddress->trackId != track.id ||
                              bands_[i].gainAddress->processorId != track.eq.id ||
                              bands_[i].gainAddress->bandId != track.eq.bands[i].id;
            }
            if (rebuild)
                rebuildBands(*view->session);
            updateBands(*view->session);
        }
    }
    const bool replacing = view->io == IoOperation::Create || view->io == IoOperation::Open;
    eq_->setEnabled(bool(view->session) && !replacing && !closing_ && !closeAfterSave_ &&
                    !closeRequested_);
    new_->setEnabled(!recordingBusy() && !attachingTake_ && !exportWorkflowBusy() &&
                     view->io == IoOperation::None && !view->dirty && !closing_ &&
                     !closeRequested_);
    open_->setEnabled(!recordingBusy() && !attachingTake_ && !exportWorkflowBusy() &&
                      view->io == IoOperation::None && !view->dirty && !closing_ &&
                      !closeRequested_);
    save_->setEnabled(bool(view->session) && view->io == IoOperation::None && !closing_ &&
                      !closeRequested_ && !closeAfterSave_);
    undo_->setEnabled(bool(view->session) && !replacing && !closing_ && !closeRequested_);
    redo_->setEnabled(bool(view->session) && !replacing && !closing_ && !closeRequested_);
    if (view->session) {
        project_->setText(text(view->session->name) +
                          (view->dirty ? tr(" — unsaved changes") : QString()));
        if (!view->session->tracks.empty()) {
            const auto &t = view->session->tracks.front();
            track_->setText(tr("%1 · %2 Hz · %n audio clip(s)", nullptr, int(t.clips.size()))
                                .arg(text(t.name), QLocale().toString(view->session->sampleRate)));
        } else
            track_->setText(tr("This project has no audio tracks."));
    }
    state_->setText(closing_                     ? tr("Closing project…")
                    : exporter_.snapshot()->busy ? tr("Exporting an immutable project snapshot…")
                    : view->io == IoOperation::AttachRecording ? tr("Verifying recorded take…")
                    : view->io == IoOperation::Save            ? tr("Saving project…")
                    : view->io == IoOperation::Open            ? tr("Opening project…")
                    : view->io == IoOperation::Create          ? tr("Creating project…")
                    : view->dirty                              ? tr("Unsaved changes")
                                                               : tr("Ready"));
    setWindowTitle(tr("SoundCurrent DAW") +
                   (view->session ? QStringLiteral(" — ") + text(view->session->name) : QString()));
    if (view->session && view->modelRevision > followedRevision_) {
        if (playback_.follow(view->root, view->session, view->modelRevision))
            followedRevision_ = view->modelRevision;
    }
    shown_ = view;
    if (closeRequested_ && !closing_ && !closeAfterSave_ && !closePromptActive_ && !closeBarrier_ &&
        recording_.snapshot()->stopAcknowledged >= closeDrainToken_ &&
        !recording_.snapshot()->take && !attachingTake_ && !exporter_.snapshot()->busy &&
        view->io == IoOperation::None) {
        ProjectCommand barrier{CommandKind::Barrier};
        barrier.barrier = nextGesture_++;
        const auto token = barrier.barrier;
        if (submitEdit(std::move(barrier)))
            closeBarrier_ = token;
    }
    if (closeBarrier_ && view->lastBarrier == closeBarrier_) {
        closeBarrier_ = 0;
        confirmClose();
    }
    if (closeAfterSave_ && view->io == IoOperation::None) {
        if (view->dirty) {
            if (!closeSaveSubmitted_)
                closeSaveSubmitted_ = submitEdit({CommandKind::Save});
        } else {
            closing_ = true;
            shutdownWorkers();
        }
    }
}
void StudioWindow::closeEvent(QCloseEvent *event) {
    const auto view = controller_.snapshot();
    if (view->closed && playback_.snapshot()->closed && recording_.snapshot()->closed &&
        exporter_.snapshot()->closed) {
        event->accept();
        return;
    }
    event->ignore();
    if (closing_ || closeAfterSave_ || closeRequested_)
        return;
    if (auto *focused = focusWidget())
        focused->clearFocus();
    closeRequested_ = true;
    exportBarrier_ = 0;
    exportSelection_.reset();
    if (exportDialog_)
        exportDialog_->reject();
    if (exportPrompt_)
        exportPrompt_->done(QMessageBox::No);
    exporter_.requestCancel();
    playback_.requestStop();
    closeDrainToken_ = recording_.requestStop();
}
void StudioWindow::confirmClose() {
    QScopedValueRollback<bool> promptGuard(closePromptActive_, true);
    const auto view = controller_.snapshot();
    if (view->dirty) {
        const auto choice = QMessageBox::question(
            this, tr("Close project"), tr("Save your changes before closing?"),
            QMessageBox::Save | QMessageBox::Discard | QMessageBox::Cancel, QMessageBox::Save);
        if (choice == QMessageBox::Cancel) {
            closeRequested_ = false;
            return;
        }
        if (choice == QMessageBox::Save) {
            closeAfterSave_ = true;
            return;
        }
    }
    closing_ = true;
    shutdownWorkers();
}
} // namespace soundcurrent::daw::ui
