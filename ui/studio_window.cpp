// SPDX-License-Identifier: GPL-3.0-only
#include "studio_window.hpp"
#include "localization.hpp"
#include "history_resources_dialog.hpp"
#include "session_list_model.hpp"
#include "master_dialog.hpp"
#include "track_view.hpp"
#include "equipment_profiles.hpp"
#include <soundcurrent/routing.hpp>
#include <QAction>
#include <QApplication>
#include <QCloseEvent>
#include <QDoubleSpinBox>
#include <QSpinBox>
#include <QFileDialog>
#include <QGridLayout>
#include <QGroupBox>
#include <QInputDialog>
#include <QLabel>
#include <QLineEdit>
#include <QMenuBar>
#include <QScreen>
#include <QScrollArea>
#include <QSignalBlocker>
#include <QSlider>
#include <QStatusBar>
#include <QTimer>
#include <QTabWidget>
#include <QWheelEvent>
#include <QMessageBox>
#include <QComboBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QListWidget>
#include <QListView>
#include <QPushButton>
#include <QProgressBar>
#include <QCheckBox>
#include <QScopedValueRollback>
#include <QStandardItemModel>
#include <algorithm>
#include <cmath>
#include <limits>
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
class FocusIntegerSpin : public QSpinBox {
  public:
    using QSpinBox::QSpinBox;
    void wheelEvent(QWheelEvent *event) override {
        if (hasFocus())
            QSpinBox::wheelEvent(event);
        else
            event->ignore();
    }
};
ChannelPortIntent portIntent(const PipeWirePort &p) {
    return audioPortIntent(p);
}
QString portKey(const PipeWirePort &p) {
    if (p.backendId != "pipewire")
        return text(p.backendId) + QStringLiteral(":") + text(p.deviceIdentity) +
               QStringLiteral(":") + text(p.channelIdentity) + QStringLiteral(":") + text(p.mediaClass);
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
                           ExportControllerOptions exportOptions,
                           ManualControlOptions manualOptions, ControllerOptions projectOptions,
                           std::function<void(HistoryBudget)> historyAccepted,
                           std::function<void(MemoryPreferences)> memoryAccepted)
    : QMainWindow(parent), controller_(std::move(projectOptions)), playback_([&] {
          options.projectMemory = controller_.resourceLedger();
          return std::move(options);
      }()),
      recording_([&] {
          recordingOptions.projectMemory = controller_.resourceLedger();
          return std::move(recordingOptions);
      }()),
      exporter_([&] {
          exportOptions.render.resources = controller_.resourceLedger();
          return std::move(exportOptions);
      }()) {
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
    auto *settings = menuBar()->addMenu(tr("Settings"));
    auto *languageAction = settings->addAction(tr("Language and regional settings…"), this, [this] {
        auto *dialog = soundcurrent::daw::i18n::settingsDialog(this);
        dialog->setAttribute(Qt::WA_DeleteOnClose);
        dialog->open();
    });
    languageAction->setObjectName(QStringLiteral("languageSettingsAction"));
    auto *editMenu = menuBar()->addMenu(tr("&Edit"));
    undo_ = editMenu->addAction(tr("&Undo project edit"), QKeySequence::Undo, this, [this] {
        if (auto *focused = focusWidget())
            focused->clearFocus();
        submitEdit({CommandKind::Undo});
    });
    redo_ = editMenu->addAction(tr("&Redo project edit"), QKeySequence::Redo, this, [this] {
        if (auto *focused = focusWidget())
            focused->clearFocus();
        submitEdit({CommandKind::Redo});
    });
    undo_->setObjectName(QStringLiteral("undoAction"));
    redo_->setObjectName(QStringLiteral("redoAction"));
    editMenu->addSeparator();
    auto *historyAction = editMenu->addAction(
        tr("Project resources…"), this, [this, historyAccepted, memoryAccepted] {
            if (historyDialog_) {
                historyDialog_->raise();
                return;
            }
            historyDialog_ = new HistoryResourcesDialog(controller_, nextGesture_, historyAccepted,
                                                        this, memoryAccepted);
            historyDialog_->show();
        });
    historyAction->setObjectName("historyResourcesAction");
    retryGui_ = editMenu->addAction(tr("Retry project display"), this, [this] {
        guiRefusedSource_.reset();
        guiRefusedEpoch_ = 0;
        guiRefusedUsage_ = {};
        poll();
    });
    retryGui_->setObjectName("retryProjectDisplayAction");
    retryGui_->setEnabled(false);
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
            if (manual_ && manual_->busy()) {
                manual_->stop();
                return;
            }
            if (recordingBusy()) {
                recordPrepareBarrier_ = 0;
                recording_.requestStop();
                return;
            }
            if (playback_.snapshot()->phase == PlaybackPhase::Playing || playbackPrepareBarrier_) {
                playbackPrepareBarrier_ = 0;
                playback_.requestStop();
            } else
                playSelected();
        });
    stopAction_ =
        transportMenu->addAction(tr("Stop"), QKeySequence(Qt::SHIFT | Qt::Key_Space), this, [this] {
            if (manual_)
                manual_->stop();
            recordPrepareBarrier_ = 0;
            playbackPrepareBarrier_ = 0;
            playback_.requestStop();
            recording_.requestStop();
        });
    stopAction_->setObjectName("stopTransportAction");
    prepareRecordAction_ =
        transportMenu->addAction(tr("Prepare recording"), this, [this] { prepareRecording(); });
    recordAction_ = transportMenu->addAction(tr("Record"), QKeySequence(Qt::Key_R), this,
                                             &StudioWindow::recordSelected);
    recoverAction_ = file->addAction(tr("Recover recording…"), this, [this] {
        const auto model = inspectorSnapshot();
        const auto start = model->session ? text(pathUtf8(model->root / "media")) : QString();
        const auto selected =
            QFileDialog::getExistingDirectory(this, tr("Choose a capture job to inspect"), start);
        if (!selected.isEmpty())
            inspectTake(path(selected));
    });
    scanRecoveryAction_ =
        file->addAction(tr("Find recoverable recordings…"), this, [this] { scanRecordings(); });
    scanRecoveryAction_->setObjectName("scanRecordingsAction");
    auto *scroll = new QScrollArea(this);
    scroll->setWidgetResizable(true);
    auto *body = new QWidget(scroll);
    auto *layout = new QVBoxLayout(body);
    project_ = new QLabel(tr("Create or open a project to begin."), body);
    project_->setObjectName(QStringLiteral("projectLabel"));
    project_->setWordWrap(true);
    layout->addWidget(project_);
    timeline_ = new TimelineEditor(body, controller_.resourceLedger());
    layout->addWidget(timeline_);
    timeline_->submit = [this](std::vector<SessionEdit> edits) {
        const auto phase = playback_.snapshot()->phase;
        if (closing_ || closeRequested_ || recordingBusy() || playbackPrepareBarrier_ ||
            (phase != PlaybackPhase::Idle && phase != PlaybackPhase::Fault &&
             phase != PlaybackPhase::Unsupported))
            return false;
        ProjectCommand command{CommandKind::Structural};
        command.edits = std::move(edits);
        return submitEdit(std::move(command));
    };
    timeline_->selectionAdmission = [this](std::shared_ptr<const Session> source,
                                           std::optional<Id> id) {
        if (source != controller_.snapshot()->session || guiBlocked_)
            return false;
        if (source == inspectorSource_ && id == inspectorTrack_)
            return true;
        try {
            auto projected = sessionForTrack(source, id, controller_.resourceLedger());
            if (!projected && source && !source->tracks.empty())
                return false;
            inspectorSource_ = std::move(source);
            inspectorProjection_ = std::move(projected);
            inspectorTrack_ = std::move(id);
            return true;
        } catch (const std::exception &e) {
            notice_->setText(
                tr("Track selection could not be updated: %1. Raise Project resources and retry.")
                    .arg(text(e.what())));
            return false;
        }
    };
    timeline_->selectionChanged = [this] {
        shown_.reset();
        outputIntentShown_.reset();
        inputIntentShown_.reset();
        monitorIntentShown_.reset();
        monitoringShown_.reset();
        if (!polling_)
            poll();
    };
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
    mixTracks_ =
        new QCheckBox(tr("Play project mix (saved master or matching layouts)"), transport);
    mixTracks_->setObjectName(QStringLiteral("mixAllTracks"));
    mixTracks_->setToolTip(
        tr("Use the saved master's channel matrix and output routes. Without a saved master, "
           "use the selected track's layout and output routes. Different track layouts require "
           "an explicit matrix."));
    transportLayout->addWidget(mixTracks_);
    masterButton_ = new QPushButton(tr("Edit master layout and matrix…"), transport);
    masterButton_->setObjectName("editMasterButton");
    transportLayout->addWidget(masterButton_);
    connect(masterButton_, &QPushButton::clicked, this, [this] {
        const auto model = controller_.snapshot();
        if (!model->session)
            return;
        MasterDialog dialog(*model->session, this);
        if (dialog.exec() == QDialog::Accepted && dialog.selection()) {
            ProjectCommand c{CommandKind::Structural};
            c.edits.push_back(SetMaster{dialog.selection()});
            if (!submitEdit(std::move(c)))
                notice_->setText(tr("Master change was not admitted. Please retry."));
        }
    });
    connect(prepareButton_, &QPushButton::clicked, this, [this] { preparePlayback(); });
    connect(playButton_, &QPushButton::clicked, this, &StudioWindow::playSelected);
    connect(stopButton_, &QPushButton::clicked, this, [this] {
        if (manual_)
            manual_->stop();
        recordPrepareBarrier_ = 0;
        playbackPrepareBarrier_ = 0;
        playback_.requestStop();
        recording_.requestStop();
    });
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
    armed_ = new QCheckBox(tr("Arm selected track"), recording);
    armed_->setObjectName(QStringLiteral("armTrack"));
    monitorMode_ = new FocusCombo(recording);
    monitorMode_->setObjectName(QStringLiteral("recordMonitorMode"));
    monitorMode_->setFocusPolicy(Qt::StrongFocus);
    monitorMode_->setAccessibleName(tr("Recording monitoring mode"));
    monitorMode_->addItem(tr("Monitoring off"), int(RecordingMonitor::Off));
    monitorMode_->addItem(tr("Monitor through track EQ"), int(RecordingMonitor::PostEq));
    monitorMode_->addItem(tr("Auto — monitor during recording"),
                          int(RecordingMonitor::AutoRecording));
    monitorMode_->setToolTip(tr("Saved monitoring mode takes effect after Stop and preparation."));
    connect(monitorMode_, &QComboBox::currentIndexChanged, this, [this] {
        // This control can be changed just after Open publishes, before the
        // first display tick. Preserve the requested value across synchronization.
        const auto requested = static_cast<RecordingMonitor>(monitorMode_->currentData().toInt());
        if (!polling_)
            poll();
        const auto model = inspectorSnapshot();
        const auto native = recording_.snapshot();
        if (!model->session || model->session->tracks.empty() || closing_ || closeRequested_ ||
            recordPrepareBarrier_ || recordCommandPending_ || native->take ||
            (manual_ && manual_->busy()) || model->io == IoOperation::Create ||
            model->io == IoOperation::Open ||
            (native->phase != RecordingPhase::Idle && native->phase != RecordingPhase::Fault &&
             native->phase != RecordingPhase::Unsupported)) {
            QSignalBlocker block(monitorMode_);
            const auto canonical = model->session && !model->session->tracks.empty()
                                       ? model->session->tracks.front().monitoring
                                       : RecordingMonitor::Off;
            monitorMode_->setCurrentIndex(monitorMode_->findData(int(canonical)));
            return;
        }
        ProjectCommand c{CommandKind::Monitoring};
        c.monitoringTrack = model->session->tracks.front().id;
        c.monitoring = requested;
        if (!submitEdit(std::move(c))) {
            QSignalBlocker block(monitorMode_);
            monitorMode_->setCurrentIndex(
                monitorMode_->findData(int(model->session->tracks.front().monitoring)));
        }
    });
    recordModes->addWidget(armed_);
    recordModes->addWidget(monitorMode_, 1);
    recordLayout->addLayout(recordModes);
    auto *latencyRow = new QHBoxLayout;
    auto *latencyLabel = new QLabel(tr("Selected track input latency:"), recording);
    inputLatency_ = new FocusIntegerSpin(recording);
    inputLatency_->setObjectName("inputLatencyFrames");
    inputLatency_->setFocusPolicy(Qt::StrongFocus);
    inputLatency_->setKeyboardTracking(false);
    inputLatency_->setAccessibleName(tr("Selected track input latency in sample frames"));
    inputLatency_->setSuffix(tr(" samples"));
    inputLatency_->setToolTip(tr("Declared input delay, from zero to 60 seconds. New takes are "
                                 "aligned earlier by this amount. Existing clips and monitor "
                                 "audio are unchanged. Stop recording before editing."));
    latencyLabel->setBuddy(inputLatency_);
    inputLatencyTime_ = new QLabel(recording);
    inputLatencyTime_->setObjectName("inputLatencyTime");
    latencyRow->addWidget(latencyLabel);
    latencyRow->addWidget(inputLatency_, 1);
    latencyRow->addWidget(inputLatencyTime_);
    recordLayout->addLayout(latencyRow);
    connect(inputLatency_, &QSpinBox::valueChanged, this, [this](int frames) {
        const auto m = controller_.snapshot();
        if (!latencyTrack_ || latencyEpoch_ != m->projectEpoch || selectedTrack() != latencyTrack_)
            return;
        if (!configureInputLatency(*latencyTrack_, frames)) {
            latencyShown_.reset();
            notice_->setText(tr("Input latency could not be changed. Stop recording and retry."));
        }
    });
    multiRecord_ = new QCheckBox(tr("Record armed tracks with project playback"), recording);
    multiRecord_->setObjectName("recordProjectMix");
    multiRecord_->setToolTip(tr("Select tracks below. Every raw input and project output must be "
                                "chosen explicitly after preparation."));
    recordLayout->addWidget(multiRecord_);
    armedTracksList_ = new QListView(recording);
    armedTrackModel_ = new SessionListModel(SessionListModel::Kind::Tracks, this, true,
                                            controller_.resourceLedger());
    armedTracksList_->setModel(armedTrackModel_);
    armedTracksList_->setUniformItemSizes(true);
    armedTracksList_->setLayoutMode(QListView::Batched);
    armedTracksList_->setBatchSize(128);
    armedTracksList_->setObjectName("armedTracksList");
    armedTracksList_->setAccessibleName(tr("Tracks to arm for simultaneous recording"));
    armedTracksList_->setMaximumHeight(150);
    armedTracksList_->setMinimumHeight(80);
    recordLayout->addWidget(armedTracksList_);
    auto *rangeRow = new QHBoxLayout;
    auto *rangeLabel = new QLabel(tr("Recording range (seconds)"), recording);
    recordRangeLabel_ = rangeLabel;
    recordSeconds_ = new QSpinBox(recording);
    recordSeconds_->setObjectName("recordRangeSeconds");
    recordSeconds_->setRange(1, 86400);
    recordSeconds_->setValue(600);
    recordSeconds_->setAccessibleName(tr("Maximum shared recording duration in seconds"));
    recordSeconds_->setToolTip(tr("Recording stops at this range; you can stop earlier. Existing "
                                  "audio plays from the current playhead."));
    rangeLabel->setBuddy(recordSeconds_);
    rangeRow->addWidget(rangeLabel);
    rangeRow->addWidget(recordSeconds_);
    recordLayout->addLayout(rangeRow);
    auto *punchRow = new QHBoxLayout;
    punchEnabled_ = new QCheckBox(tr("Punch recording"), recording);
    punchEnabled_->setObjectName("punchEnabled");
    punchEnabled_->setToolTip(
        tr("Record only within the saved punch range while project playback continues."));
    punchRangeButton_ = new QPushButton(tr("Set punch range…"), recording);
    punchRangeButton_->setObjectName("editPunchRange");
    punchRow->addWidget(punchEnabled_);
    punchRow->addWidget(punchRangeButton_);
    recordLayout->addLayout(punchRow);
    punchSummary_ = new QLabel(recording);
    punchSummary_->setObjectName("punchSummary");
    punchSummary_->setWordWrap(true);
    recordLayout->addWidget(punchSummary_);
    connect(punchRangeButton_, &QPushButton::clicked, this, [this] { editPunchRange(); });
    connect(punchEnabled_, &QCheckBox::toggled, this, [this](bool enabled) {
        const auto m = controller_.snapshot();
        if (!m->session)
            return;
        auto value = m->session->punch;
        value.enabled = enabled;
        if (enabled && value.endFrame == value.startFrame) {
            const auto duration = Frame(m->session->sampleRate) * 10;
            if (m->session->playheadFrame > std::numeric_limits<Frame>::max() - duration) {
                notice_->setText(tr("There is no room for a punch range at this position."));
                punchShown_.reset();
                return;
            }
            value.startFrame = m->session->playheadFrame;
            value.endFrame = value.startFrame + duration;
        }
        if (!configurePunch(value)) {
            punchShown_.reset();
            notice_->setText(
                tr("Punch settings could not be changed. Stop recording setup and retry."));
        }
    });
    auto *reserveRow = new QHBoxLayout;
    auto *reserveLabel = new QLabel(tr("Recording disk-stall reserve"), recording);
    recordReserve_ = new FocusCombo(recording);
    recordReserve_->setObjectName("recordingReserveMilliseconds");
    recordReserve_->addItem(tr("2 seconds"), 2000);
    recordReserve_->addItem(tr("5 seconds"), 5000);
    recordReserve_->addItem(tr("10 seconds"), 10000);
    recordReserve_->setCurrentIndex(2);
    recordReserve_->setAccessibleName(tr("Recording disk-stall reserve"));
    recordReserve_->setToolTip(
        tr("Uses memory to absorb temporary disk stalls. Applies on Prepare; "
           "monitoring latency is unchanged. Preparation refuses a reserve "
           "that exceeds the recording memory budget."));
    reserveLabel->setBuddy(recordReserve_);
    reserveRow->addWidget(reserveLabel);
    reserveRow->addWidget(recordReserve_);
    recordLayout->addLayout(reserveRow);
    connect(recordSeconds_, &QSpinBox::valueChanged, this, [this] { recordRangeOverride_ = 0; });
    connect(multiRecord_, &QCheckBox::toggled, this, [this] {
        recordPrepareBarrier_ = 0;
        if (recording_.snapshot()->phase == RecordingPhase::Ready)
            recording_.requestStop();
        refreshArms();
    });
    armedTrackModel_->checkChanged = [this](const Id &id, bool checked) {
        const auto at = std::find(armedTracksSelection_.begin(), armedTracksSelection_.end(), id);
        if (checked && at == armedTracksSelection_.end())
            armedTracksSelection_.push_back(id);
        else if (!checked && at != armedTracksSelection_.end())
            armedTracksSelection_.erase(at);
    };
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
    auto *recoveryRow = new QHBoxLayout;
    recoverySummary_ = new QLabel(tr("Open a project to find stored recordings."), recording);
    recoverySummary_->setObjectName("recoverySummary");
    recoverySummary_->setWordWrap(true);
    recoverySummary_->setTextFormat(Qt::PlainText);
    reviewRecoveryButton_ = new QPushButton(tr("Review recordings…"), recording);
    reviewRecoveryButton_->setObjectName("reviewRecordingsButton");
    scanRecoveryButton_ = new QPushButton(tr("Refresh"), recording);
    scanRecoveryButton_->setObjectName("refreshRecordingsButton");
    recoveryRow->addWidget(recoverySummary_, 1);
    recoveryRow->addWidget(reviewRecoveryButton_);
    recoveryRow->addWidget(scanRecoveryButton_);
    recordLayout->addLayout(recoveryRow);
    connect(reviewRecoveryButton_, &QPushButton::clicked, this, [this] { reviewRecordings(); });
    connect(scanRecoveryButton_, &QPushButton::clicked, this, [this] { scanRecordings(); });
    connect(prepareRecordButton_, &QPushButton::clicked, this, [this] { prepareRecording(); });
    connect(recordButton_, &QPushButton::clicked, this, &StudioWindow::recordSelected);
    connect(recordStopButton_, &QPushButton::clicked, this, [this] {
        recordPrepareBarrier_ = 0;
        recording_.requestStop();
    });
    connect(armed_, &QCheckBox::toggled, this, [this](bool v) {
        if (!v) {
            recordPrepareBarrier_ = 0;
            recording_.requestStop();
        }
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
    auto *recordModesTabs = new QTabWidget(body);
    recordModesTabs->setObjectName("recordingModes");
    recordModesTabs->setAccessibleName(tr("Recording workflows"));
    recordModesTabs->addTab(recording, tr("Fixed range / locators"));
    layout->addWidget(recordModesTabs);
    manualOptions.projectMemory = controller_.resourceLedger();
    manual_ = std::make_unique<ManualRecordingPanel>(
        controller_, nextGesture_,
        [this] {
            const auto r = recording_.snapshot();
            const auto p = playback_.snapshot();
            const bool recordIdle = r->phase == RecordingPhase::Idle ||
                                    r->phase == RecordingPhase::Unsupported ||
                                    r->phase == RecordingPhase::Fault;
            const bool playIdle = p->phase == PlaybackPhase::Idle ||
                                  p->phase == PlaybackPhase::Unsupported ||
                                  p->phase == PlaybackPhase::Fault;
            return !closing_ && !closeRequested_ && !recordPrepareBarrier_ &&
                   !recordCommandPending_ && !r->take && !attachingTake_ &&
                   !playbackPrepareBarrier_ && recordIdle && playIdle && !exportWorkflowBusy();
        },
        std::move(manualOptions), body);
    recordModesTabs->addTab(manual_.get(), tr("Manual / repeated takes"));
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
        tr("Development preview: selected-track recording and shared-clock playback are available "
           "on Linux. "
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
    timer_->setObjectName("studioPollTimer");
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
    poll();
    const auto m = inspectorSnapshot();
    if (exportWorkflowBusy() || playbackPrepareBarrier_ || recordPrepareBarrier_ || closing_ ||
        closeRequested_ || closeAfterSave_ || !m->session || m->session->tracks.empty() ||
        m->io == IoOperation::Create || m->io == IoOperation::Open || attachingTake_)
        return false;
    if (auto *focused = focusWidget())
        focused->clearFocus();
    ProjectCommand barrier{CommandKind::Barrier};
    barrier.barrier = nextGesture_++;
    const auto token = barrier.barrier;
    if (!submitEdit(std::move(barrier)))
        return false;
    exportBarrier_ = token;
    exportTrack_ = selectedTrack();
    return true;
}
void StudioWindow::pollExport() {
    const auto m = inspectorSnapshot();
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
                const auto selected = projectTrack(m->barrierSession, exportTrack_);
                if (!selected) {
                    notice_->setText(
                        tr("The selected export track no longer exists. Please retry."));
                    return;
                }
                ExportDialog dialog(m->barrierRoot, selected, this);
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
    if (manual_ && manual_->busy() &&
        (command.kind == CommandKind::Structural || command.kind == CommandKind::Monitoring ||
         command.kind == CommandKind::Open || command.kind == CommandKind::Create)) {
        state_->setText(tr("Stop manual recording and resolve its previews before changing the "
                           "project structure."));
        return false;
    }
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
    playbackPrepareBarrier_ = 0;
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
    const int sampleRate = QInputDialog::getInt(
        this, tr("New project"),
        tr("Sample rate (Hz). For Windows native audio, match the device mix rate:"),
        48000, 8000, 384000, 1, &accepted);
    if (!accepted)
        return;
    command.sampleRate = static_cast<std::uint32_t>(sampleRate);
    command.path = path(parent) / path(name);
    command.name = name.toUtf8().toStdString();
    playbackPrepareBarrier_ = 0;
    playback_.requestStop();
    submitEdit(std::move(command));
}

std::shared_ptr<const PlaybackSnapshot> StudioWindow::playbackSnapshot() const {
    return playback_.snapshot();
}
std::optional<Id> StudioWindow::selectedTrack() const {
    return timeline_->selectedTrack();
}
bool StudioWindow::selectTrack(const Id &id) {
    // Open may already be published while the displayed timeline still awaits
    // its timer tick. Resolve a caller's stable ID against the current model.
    poll();
    return timeline_->selectTrack(id);
}
ResourceLedger StudioWindow::resourceLedger() const {
    return controller_.resourceLedger();
}
ResourceUsage StudioWindow::memoryResources() const {
    return controller_.memoryResources();
}
std::size_t StudioWindow::guiResourceBytes() const {
    auto bytes = guiCharge("GUI retained ownership");
    bytes.add(timeline_->resourceBytes());
    bytes.add(armedTrackModel_->resourceBytes());
    if (inspectorProjection_ && inspectorProjection_ != inspectorSource_)
        bytes.add(sessionPayloadBytes(*inspectorProjection_, StateBudget{SIZE_MAX}) + 256);
    return bytes.bytes() - 256;
}
bool StudioWindow::syncGui(const std::shared_ptr<const ControllerSnapshot> &canonical,
                           bool editable) {
    const auto usage = controller_.memoryResources();
    if (!guiBlocked_ && canonical->session == inspectorSource_ &&
        canonical->projectEpoch == guiEpoch_) {
        guiRevision_ = canonical->modelRevision;
        timeline_->editing(editable);
        return true;
    }
    if (guiBlocked_ && guiRefusedSource_ == canonical->session &&
        guiRefusedEpoch_ == canonical->projectEpoch &&
        usage.limitBytes == guiRefusedUsage_.limitBytes &&
        usage.reservedBytes == guiRefusedUsage_.reservedBytes) {
        timeline_->editing(false);
        return false;
    }
    try {
        auto timeline =
            timeline_->prepareModel(canonical->session, canonical->projectEpoch, editable);
        auto arms = armedTrackModel_->prepare(canonical->session);
        const auto selected = timeline_->preparedTrack(timeline);
        auto projection =
            inspectorSource_ == canonical->session && inspectorTrack_ == selected
                ? inspectorProjection_
                : sessionForTrack(canonical->session, selected, controller_.resourceLedger());
        timeline_->commitModel(std::move(timeline));
        armedTrackModel_->commit(std::move(arms));
        inspectorSource_ = canonical->session;
        inspectorProjection_ = std::move(projection);
        inspectorTrack_ = selected;
        guiEpoch_ = canonical->projectEpoch;
        guiRevision_ = canonical->modelRevision;
        if (guiBlocked_)
            notice_->setText(tr("Project display updated. Retained edits are available."));
        guiBlocked_ = false;
        guiRefusedSource_.reset();
        retryGui_->setEnabled(false);
        return true;
    } catch (const std::exception &e) {
        guiBlocked_ = true;
        guiRefusedSource_ = canonical->session;
        guiRefusedEpoch_ = canonical->projectEpoch;
        guiRefusedUsage_ = controller_.memoryResources();
        retryGui_->setEnabled(true);
        timeline_->editing(false);
        notice_->setText(
            tr("Project display could not be updated: %1. The previous view is retained and "
               "editing is paused. Raise Project resources, then retry the display.")
                .arg(text(e.what())));
        return false;
    }
}
std::shared_ptr<const Session> StudioWindow::projectTrack(std::shared_ptr<const Session> source,
                                                          std::optional<Id> id) {
    if (source == inspectorSource_ && id == inspectorTrack_)
        return inspectorProjection_;
    try {
        return sessionForTrack(std::move(source), id, controller_.resourceLedger());
    } catch (const std::exception &e) {
        notice_->setText(
            tr("Audio view could not be prepared: %1. Raise Project resources and retry.")
                .arg(text(e.what())));
        return {};
    }
}
std::shared_ptr<const ControllerSnapshot>
StudioWindow::inspectorSnapshot(std::shared_ptr<const ControllerSnapshot> canonical) const {
    const bool supplied = bool(canonical);
    if (!canonical)
        canonical = controller_.snapshot();
    const auto selected = timeline_ ? timeline_->selectedTrack() : std::optional<Id>{};
    const bool exact = canonical->session == inspectorSource_;
    // A command may publish during poll. Default UI bindings keep the last
    // admitted display tuple for the same epoch/stable selected ID; command
    // barriers still capture the latest accepted prefix. Explicit poll tuples
    // require an exact match, and a refused display never supplies edit state.
    const bool displayed = !supplied && inspectorSource_ && canonical->session && selected &&
                           trackForId(canonical->session.get(), selected);
    const bool ready = !guiBlocked_ && canonical->projectEpoch == guiEpoch_ &&
                       selected == inspectorTrack_ && (exact || displayed);
    if (ready && exact && inspectorProjection_ == canonical->session)
        return canonical;
    auto result = std::make_shared<ControllerSnapshot>(*canonical);
    result->session = ready ? inspectorProjection_ : std::shared_ptr<const Session>{};
    if (ready)
        result->modelRevision = guiRevision_;
    return result;
}
bool StudioWindow::preparePlayback() {
    // Open/create publishes asynchronously; synchronize initial selection even
    // when preparation is requested before the next timer tick.
    poll();
    const auto view = inspectorSnapshot();
    if (!view->session || view->session->tracks.empty() || !selectedTrack() || closing_ ||
        closeRequested_ || closeAfterSave_ || playbackPrepareBarrier_ || recordPrepareBarrier_ ||
        exportBarrier_ || view->io == IoOperation::Create || view->io == IoOperation::Open ||
        recordingBusy() || attachingTake_)
        return false;
    if (auto *focused = focusWidget())
        focused->clearFocus();
    ProjectCommand capture{CommandKind::Barrier};
    capture.barrier = nextGesture_++;
    const auto token = capture.barrier;
    if (!submitEdit(std::move(capture)))
        return false;
    playbackPrepareBarrier_ = token;
    playbackPreparationTrack_ = selectedTrack();
    playbackPrepareMix_ = mixTracks_->isChecked();
    return true;
}

void StudioWindow::playSelected() {
    if (playback_.snapshot()->phase != PlaybackPhase::Ready || !outputsShown_ ||
        (!playback_.snapshot()->projectMix && selectedTrack() != playbackTrack_))
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
void StudioWindow::populateRoutes(const std::vector<QComboBox *> &combos,
                                  const std::vector<PipeWirePort> &ports, const RouteIntent &intent,
                                  bool endpointInput) {
    std::vector<ChannelPortIntent> descriptors;
    for (const auto &p : ports)
        descriptors.push_back(portIntent(p));
    for (std::size_t channel = 0; channel < combos.size(); ++channel) {
        auto *combo = combos[channel];
        const auto previous = combo->currentData().toString();
        QSignalBlocker blocked(combo);
        combo->clear();
        combo->addItem(tr("Choose an endpoint…"), QString());
        for (const auto &p : ports)
            if (p.input == endpointInput) {
                auto label = text(p.nodeName) + QStringLiteral(" / ") + text(p.portName);
                if (p.sampleRate)
                    label += tr(" · %1 Hz").arg(QLocale().toString(p.sampleRate));
                combo->addItem(label, portKey(p));
            }
        const auto result = matchRouteIntent(intent, channel, ports.empty() ?
#ifdef Q_OS_WIN
            "wasapi"
#else
            "pipewire"
#endif
            : ports.front().backendId, descriptors);
        const auto prior = std::find_if(ports.begin(), ports.end(),
                                        [&](const auto &p) { return portKey(p) == previous; });
        if (prior != ports.end() && channel < intent.ports.size() && intent.ports[channel] &&
            *intent.ports[channel] == portIntent(*prior)) {
            combo->setCurrentIndex(combo->findData(
                previous)); // Explicit live choice, even if saved descriptor is ambiguous.
        } else if (result.index) {
            combo->setCurrentIndex(combo->findData(portKey(ports[*result.index])));
        } else if (result.status != RouteMatchStatus::Unassigned) {
            QString message;
            switch (result.status) {
            case RouteMatchStatus::Missing:
                message = tr("Missing endpoint");
                break;
            case RouteMatchStatus::Ambiguous:
                message = tr("Ambiguous endpoint; choose explicitly");
                break;
            case RouteMatchStatus::Legacy:
                message = tr("Legacy route; choose explicitly");
                break;
            case RouteMatchStatus::UnsupportedBackend:
                message = tr("Unavailable backend: %1").arg(text(intent.backendId));
                break;
            default:
                break;
            }
            if (channel < intent.ports.size() && intent.ports[channel])
                message += QStringLiteral(" — ") + text(intent.ports[channel]->deviceIdentity) +
                           QStringLiteral(" / ") + text(intent.ports[channel]->portIdentity);
            else if (!intent.portIdentity.empty())
                message += QStringLiteral(" — ") + text(intent.portIdentity);
            combo->addItem(message, QStringLiteral("unresolved-route"));
            combo->setCurrentIndex(combo->count() - 1);
        } else
            combo->setCurrentIndex(0);
    }
}
void StudioWindow::selectRoute(RouteTarget target, std::size_t channel, QComboBox *combo) {
    auto model = inspectorSnapshot();
    if (target == RouteTarget::Output && playback_.snapshot()->projectMix) {
        auto canonical = controller_.snapshot();
        if (canonical->session && canonical->session->master) {
            const auto &m = *canonical->session->master;
            const auto key = combo->currentData().toString();
            if (key == "unresolved-route" || !outputsShown_)
                return;
            const auto found = std::find_if(outputsShown_->begin(), outputsShown_->end(),
                                            [&](const auto &p) { return portKey(p) == key; });
            if (!key.isEmpty() && found == outputsShown_->end())
                return;
            ProjectCommand c{CommandKind::Routing};
            c.routeAddress = RouteAddress{m.id, RouteTarget::Master};
            c.routePatch =
                RouteChannelPatch{std::uint32_t(channel), outputsShown_->empty() ? m.output.backendId : outputsShown_->front().backendId,
                                  found == outputsShown_->end() ? std::optional<ChannelPortIntent>{}
                                                                : portIntent(*found)};
            if (!submitEdit(std::move(c)))
                notice_->setText(tr("Master output was not admitted. Please retry."));
            return;
        }
        auto projected = projectTrack(canonical->session, playbackTrack_);
        if (!projected)
            return;
        auto anchored = std::make_shared<ControllerSnapshot>(*canonical);
        anchored->session = std::move(projected);
        model = std::move(anchored);
    }
    if (!model->session || model->session->tracks.empty() || closing_ || closeRequested_)
        return;
    const auto &track = model->session->tracks.front();
    if (channel >= track.layout.channels)
        return;
    const auto key = combo->currentData().toString();
    if (key == "unresolved-route")
        return;
    const auto ports = target == RouteTarget::Output ? outputsShown_ : recordPortsShown_;
    if (!ports)
        return;
    const auto found = std::find_if(ports->begin(), ports->end(),
                                    [&](const auto &p) { return portKey(p) == key; });
    if (!key.isEmpty() && found == ports->end())
        return;
    ProjectCommand command(CommandKind::Routing);
    command.routeAddress = RouteAddress{track.id, target};
    command.routePatch = RouteChannelPatch{
        static_cast<std::uint32_t>(channel), ports->empty() ? routeValue(*model->session, {track.id, target}).backendId : ports->front().backendId,
        found == ports->end() ? std::optional<ChannelPortIntent>{} : portIntent(*found)};
    if (!submitEdit(std::move(command))) {
        const RouteAddress address{track.id, target};
        const auto &intent = routeValue(*model->session, address);
        populateRoutes(target == RouteTarget::Output  ? outputs_
                       : target == RouteTarget::Input ? inputs_
                                                      : monitors_,
                       *ports, intent, target != RouteTarget::Input);
        notice_->setText(tr("The route change was not accepted. Please retry."));
    }
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
            combo->setToolTip(tr("Saved routing changes take effect after Stop and preparation."));
            combo->setAccessibleName(tr("Output channel %1").arg(QLocale().toString(c + 1)));
            combo->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
            combo->setMinimumContentsLength(16);
            auto *label = new QLabel(tr("Output %1").arg(QLocale().toString(c + 1)));
            label->setBuddy(combo);
            outputsLayout_->addWidget(label, int(c), 0);
            outputsLayout_->addWidget(combo, int(c), 1);
            outputs_.push_back(combo);
            connect(combo, &QComboBox::currentIndexChanged, this,
                    [this, combo, c] { selectRoute(RouteTarget::Output, c, combo); });
        }
    }
    auto model = inspectorSnapshot();
    const auto canonical = controller_.snapshot();
    const auto anchor = view.projectMix ? trackForId(canonical->session.get(), playbackTrack_)
                                        : trackForId(model->session.get(), {});
    const auto intent = view.projectMix && canonical->session && canonical->session->master
                            ? canonical->session->master->output
                        : anchor ? anchor->output
                                 : RouteIntent{};
    if (view.ports && (!outputsShown_ || *outputsShown_ != *view.ports || !outputIntentShown_ ||
                       *outputIntentShown_ != intent)) {
        populateRoutes(outputs_, *view.ports, intent, true);
        outputsShown_ = view.ports;
        outputIntentShown_ = intent;
    }
    for (auto *combo : outputs_)
        combo->setEnabled(view.phase == PlaybackPhase::Ready &&
                          (view.projectMix || selectedTrack() == playbackTrack_) && !closing_ &&
                          !closeRequested_);
}
void StudioWindow::pollPlayback() {
    const auto prefix = controller_.snapshot();
    if (playbackPrepareBarrier_ && prefix->lastBarrier == playbackPrepareBarrier_ && !closing_ &&
        !closeRequested_) {
        playbackPrepareBarrier_ = 0;
        PlaybackCommand c;
        c.root = prefix->barrierRoot;
        c.session = projectTrack(prefix->barrierSession, playbackPreparationTrack_);
        c.modelRevision = prefix->barrierRevision;
        try {
            if (c.session && playbackPrepareMix_) {
                std::vector<Id> ids;
                for (const auto &t : c.session->tracks)
                    ids.push_back(t.id);
                c.plan = c.session->master
                             ? c.session->master->plan
                             : identityMix(*c.session, ids, c.session->tracks.front().layout);
            }
            if (c.session && playback_.submit(std::move(c)) == Admission::Accepted)
                playbackTrack_ = playbackPreparationTrack_;
            else
                notice_->setText(tr("Playback could not be prepared. Please retry."));
        } catch (const ProjectError &e) {
            notice_->setText(tr("Playback could not be prepared: %1").arg(text(e.what())));
        }
    }
    const auto p = playback_.snapshot();
    const auto model = inspectorSnapshot();
    updateOutputs(*p);
    const bool allow = !closing_ && !closeRequested_ && !closeAfterSave_;
    const bool idle = p->phase == PlaybackPhase::Idle || p->phase == PlaybackPhase::Ready ||
                      p->phase == PlaybackPhase::Complete || p->phase == PlaybackPhase::Fault;
    const bool prepare = allow && !playbackPrepareBarrier_ && !recordingBusy() && !attachingTake_ &&
                         p->supported && idle && model->session &&
                         model->io != IoOperation::Create && model->io != IoOperation::Open;
    mixTracks_->setEnabled(prepare && !playbackPrepareBarrier_);
    masterButton_->setEnabled(allow && !playbackPrepareBarrier_ && !recordingBusy() &&
                              (p->phase == PlaybackPhase::Idle ||
                               p->phase == PlaybackPhase::Fault ||
                               p->phase == PlaybackPhase::Unsupported) &&
                              model->session && model->io == IoOperation::None);
    prepareButton_->setEnabled(prepare);
    prepareAction_->setEnabled(prepare);
    playButton_->setEnabled(allow && p->phase == PlaybackPhase::Ready &&
                            (p->projectMix || selectedTrack() == playbackTrack_));
    playAction_->setEnabled(
        allow && ((p->phase == PlaybackPhase::Ready || p->phase == PlaybackPhase::Playing) ||
                  recordingBusy()));
    const bool stoppable =
        allow && (playbackPrepareBarrier_ || recordingBusy() ||
                  p->phase == PlaybackPhase::Preparing || p->phase == PlaybackPhase::Ready ||
                  p->phase == PlaybackPhase::Playing || p->phase == PlaybackPhase::Complete);
    stopButton_->setEnabled(stoppable);
    stopAction_->setEnabled(stoppable);
    QString status;
    switch (p->phase) {
    case PlaybackPhase::Unsupported:
        status = tr("Native playback is not available in this build.");
        break;
    case PlaybackPhase::Idle:
        status =
            tr("Prepare the selected track or matching-layout mix, choose outputs, then play.");
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
        status += tr(" · %1 s · %2 missing track-frames")
                      .arg(QLocale().toString(double(p->position) / p->sampleRate, 'f', 2),
                           QLocale().toString(p->missingFrames));
    if (p->projectMix) {
        status += tr(" · %n mixed track(s)", nullptr, int(p->tracks));
        const auto canonical = controller_.snapshot();
        const auto anchor = trackForId(canonical->session.get(), playbackTrack_);
        if (canonical->session && canonical->session->master)
            status += tr(" · Saved master output");
        else if (anchor)
            status += tr(" · Shared output routes: %1").arg(text(anchor->name));
    } else if (playbackTrack_ && selectedTrack() != playbackTrack_ && p->ports)
        status += tr(" · Another track is prepared. Stop or prepare the selected track.");
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
    return (manual_ && manual_->busy()) || recordPrepareBarrier_ || recordCommandPending_ ||
           r->take ||
           (r->phase != RecordingPhase::Idle && r->phase != RecordingPhase::Unsupported &&
            r->phase != RecordingPhase::Fault && r->phase != RecordingPhase::Closed);
}
std::shared_ptr<const ManualControlSnapshot> StudioWindow::manualRecordingSnapshot() const {
    return manual_->snapshot();
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
    poll();
    const auto m = inspectorSnapshot();
    const auto p = playback_.snapshot();
    const auto r = recording_.snapshot();
    if ((r->phase != RecordingPhase::Idle && r->phase != RecordingPhase::Fault &&
         r->phase != RecordingPhase::Unsupported && r->phase != RecordingPhase::Ready) ||
        !m->session || m->session->tracks.empty() || recordPrepareBarrier_ ||
        recordCommandPending_ || playbackPrepareBarrier_ || exportBarrier_ ||
        m->io != IoOperation::None || closing_ || closeRequested_ || closeAfterSave_ ||
        recording_.snapshot()->take || attachingTake_ ||
        (p->phase != PlaybackPhase::Idle && p->phase != PlaybackPhase::Fault &&
         p->phase != PlaybackPhase::Unsupported))
        return false;
    ProjectCommand capture{CommandKind::Barrier};
    capture.barrier = nextGesture_++;
    const auto token = capture.barrier;
    if (!submitEdit(std::move(capture)))
        return false;
    recordPrepareBarrier_ = token;
    recordPreparationTrack_ = selectedTrack();
    recordPreparationReserveMilliseconds_ = recordReserve_->currentData().toUInt();
    recordPrepareMix_ = multiRecord_->isChecked();
    recordPreparationArms_.clear();
    if (recordPrepareMix_) {
        const auto canonical = controller_.snapshot();
        for (const auto &t : canonical->session->tracks)
            if (std::find(armedTracksSelection_.begin(), armedTracksSelection_.end(), t.id) !=
                armedTracksSelection_.end())
                recordPreparationArms_.push_back(t.id);
        if (recordPreparationArms_.empty()) {
            recordPrepareBarrier_ = 0;
            notice_->setText(tr("Select at least one track to arm."));
            return false;
        }
        recordPreparationFrames_ =
            recordRangeOverride_ ? recordRangeOverride_
                                 : Frame(recordSeconds_->value()) * canonical->session->sampleRate;
    }
    return true;
}
bool StudioWindow::configureArmedRecording(const std::vector<Id> &ids, Frame frames) {
    // Arming can be requested immediately after Open's worker publication.
    // Synchronize the admitted inventory before changing its check states.
    poll();
    const auto m = controller_.snapshot();
    const auto r = recording_.snapshot();
    if (guiBlocked_ || m->session != inspectorSource_ || !m->session || ids.empty() ||
        ids.size() > 256 || recordingBusy() || frames < 0 ||
        frames > Frame(m->session->sampleRate) * 86400 || !r->duplexSupported)
        return false;
    std::vector<Id> seen;
    for (const auto &id : ids) {
        if (std::find(seen.begin(), seen.end(), id) != seen.end() ||
            std::none_of(m->session->tracks.begin(), m->session->tracks.end(),
                         [&](const auto &t) { return t.id == id; }))
            return false;
        seen.push_back(id);
    }
    armProjectEpoch_ = m->projectEpoch;
    armedTracksSelection_ = std::move(seen);
    multiRecord_->setChecked(true);
    recordRangeOverride_ = frames;
    refreshArms();
    return true;
}
bool StudioWindow::configurePunch(PunchSettings value) {
    const auto m = controller_.snapshot();
    const auto r = recording_.snapshot();
    if (!m->session || m->io != IoOperation::None || recordingBusy() ||
        r->phase == RecordingPhase::Ready || r->take || attachingTake_ || recordPrepareBarrier_ ||
        recordCommandPending_ || closing_ || closeRequested_ || exportWorkflowBusy() ||
        value.startFrame < 0 || value.endFrame < value.startFrame ||
        (value.enabled && value.endFrame == value.startFrame))
        return false;
    ProjectCommand c{CommandKind::Structural};
    c.edits = {SetPunch{value}};
    return submitEdit(std::move(c));
}
bool StudioWindow::configureInputLatency(const Id &track, Frame frames) {
    const auto m = controller_.snapshot();
    const auto r = recording_.snapshot();
    if (!m->session || m->io != IoOperation::None || recordingBusy() ||
        r->phase == RecordingPhase::Ready || r->take || attachingTake_ || recordPrepareBarrier_ ||
        recordCommandPending_ || closing_ || closeRequested_ || closeAfterSave_ ||
        exportWorkflowBusy() || frames < 0 || frames > Frame(m->session->sampleRate) * 60 ||
        std::none_of(m->session->tracks.begin(), m->session->tracks.end(),
                     [&](const auto &t) { return t.id == track; }))
        return false;
    ProjectCommand c{CommandKind::Structural};
    c.edits = {SetInputLatency{track, frames}};
    return submitEdit(std::move(c));
}
void StudioWindow::editPunchRange() {
    const auto m = controller_.snapshot();
    if (!m->session || punchDialog_ || !punchRangeButton_->isEnabled())
        return;
    auto *d = new QDialog(this);
    punchDialog_ = d;
    d->setObjectName("punchRangeDialog");
    d->setAttribute(Qt::WA_DeleteOnClose);
    d->setWindowTitle(tr("Punch recording range"));
    d->setModal(true);
    auto *layout = new QVBoxLayout(d);
    auto *info = new QLabel(tr("Positions are sample frames at %1 Hz. Input latency is accounted "
                               "for during preparation.")
                                .arg(QLocale().toString(m->session->sampleRate)),
                            d);
    info->setWordWrap(true);
    layout->addWidget(info);
    auto *enabled = new QCheckBox(tr("Enable punch recording"), d);
    enabled->setObjectName("punchRangeEnabled");
    enabled->setChecked(m->session->punch.enabled);
    layout->addWidget(enabled);
    auto *start = new QLineEdit(QLocale().toString(m->session->punch.startFrame), d);
    auto *end = new QLineEdit(QLocale().toString(m->session->punch.endFrame), d);
    start->setObjectName("punchStartFrame");
    end->setObjectName("punchEndFrame");
    for (auto pair :
         {std::pair{tr("Punch in (samples)"), start}, std::pair{tr("Punch out (samples)"), end}}) {
        auto *row = new QHBoxLayout;
        auto *label = new QLabel(pair.first, d);
        label->setBuddy(pair.second);
        pair.second->setAccessibleName(pair.first);
        row->addWidget(label);
        row->addWidget(pair.second);
        layout->addLayout(row);
    }
    auto *error = new QLabel(d);
    error->setObjectName("punchRangeError");
    error->setWordWrap(true);
    error->setMinimumHeight(error->fontMetrics().height() * 3);
    layout->addWidget(error);
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, d);
    layout->addWidget(buttons);
    connect(buttons, &QDialogButtonBox::rejected, d, &QDialog::reject);
    connect(
        buttons, &QDialogButtonBox::accepted, d,
        [this, d, start, end, enabled, error, epoch = m->projectEpoch, project = m->session->id] {
            const auto showError = [d, error](const QString &message) {
                error->setText(message);
                d->layout()->activate();
                d->adjustSize();
            };
            const auto current = controller_.snapshot();
            if (!current->session || current->projectEpoch != epoch ||
                current->session->id != project) {
                error->setText(
                    tr("The project changed. Close this dialog and reopen its punch settings."));
                return;
            }
            bool a = false, b = false;
            const auto first = QLocale().toLongLong(start->text(), &a),
                       last = QLocale().toLongLong(end->text(), &b);
            if (!a || !b || first < 0 || last < first || (enabled->isChecked() && first == last)) {
                showError(tr("Enter nonnegative whole sample positions, with punch out after "
                             "punch in when enabled."));
                return;
            }
            if (!configurePunch({enabled->isChecked(), first, last})) {
                error->setText(
                    tr("Punch settings could not be changed. Stop recording setup and retry."));
                return;
            }
            d->accept();
        });
    d->open();
}
void StudioWindow::refreshArms() {
    if (guiBlocked_) {
        armedTrackModel_->checkEditing(false);
        return;
    }
    const auto m = controller_.snapshot();
    const auto r = recording_.snapshot();
    const bool newProject = armProjectEpoch_ != m->projectEpoch;
    const bool suggestArm = m->session && m->session->punch.enabled &&
                            (newProject || !punchShown_ || !punchShown_->enabled);
    if (newProject) {
        armProjectEpoch_ = m->projectEpoch;
        armedTracksSelection_.clear();
    }
    if (m->session && (!punchShown_ || *punchShown_ != m->session->punch)) {
        punchShown_ = m->session->punch;
        QSignalBlocker blocked(punchEnabled_);
        punchEnabled_->setChecked(punchShown_->enabled);
    }
    if (m->session && m->session->punch.enabled) {
        QSignalBlocker blocked(multiRecord_);
        multiRecord_->setChecked(true);
        if (suggestArm && armedTracksSelection_.empty() && !m->session->tracks.empty())
            armedTracksSelection_.push_back(
                selectedTrack().value_or(m->session->tracks.front().id));
    }
    const bool mix = multiRecord_->isChecked();
    armedTracksList_->setVisible(mix);
    recordSeconds_->setVisible(mix);
    recordRangeLabel_->setVisible(mix);
    armed_->setVisible(!mix);
    if (!m->session) {
        armedTrackModel_->decorate({});
        armedTrackModel_->checkEditing(false);
        punchEnabled_->setEnabled(false);
        punchRangeButton_->setEnabled(false);
        punchSummary_->clear();
        punchShown_.reset();
        return;
    }
    std::unordered_map<std::string, TrackDecoration> decorations;
    for (const auto &id : armedTracksSelection_)
        decorations[id.str()].checked = true;
    if (r->projectMix)
        for (std::size_t n = 0; n < r->lanes.size(); ++n) {
            const auto &lane = r->lanes[n];
            auto &d = decorations[lane.track.str()];
            if (n < r->telemetry.lanes.size()) {
                const auto &c = r->telemetry.lanes[n];
                d.suffix = tr(" · captured %1 / written %2")
                               .arg(QLocale().toString(c.captured), QLocale().toString(c.written));
                d.color = c.rejected || c.status == CaptureStatus::WriterFailed ? QColor("#c83434")
                                                                                : QColor("#28894e");
            }
            if (lane.errorCode) {
                d.color = QColor("#c83434");
                d.suffix += tr(" · failed; recovery available");
            }
            d.tooltip = text(lane.diagnostic) +
                        (lane.job ? QStringLiteral("\n") + text(pathUtf8(*lane.job)) : QString());
        }
    try {
        armedTrackModel_->decorate(std::move(decorations));
    } catch (const std::exception &e) {
        notice_->setText(
            tr("Recording indicators could not be updated: %1. Raise Project resources and retry.")
                .arg(text(e.what())));
    }
    const bool idle = r->phase == RecordingPhase::Idle || r->phase == RecordingPhase::Fault ||
                      r->phase == RecordingPhase::Unsupported;
    const bool edit = idle && !recordPrepareBarrier_ && !recordCommandPending_ && !r->take &&
                      !closing_ && !closeRequested_;
    multiRecord_->setEnabled(edit && r->duplexSupported && !m->session->punch.enabled);
    armedTracksList_->setEnabled(edit);
    armedTrackModel_->checkEditing(edit);
    recordSeconds_->setEnabled(edit);
    punchEnabled_->setEnabled(edit && r->duplexSupported);
    punchRangeButton_->setEnabled(edit && r->duplexSupported);
    punchSummary_->setText(
        m->session->punch.enabled
            ? tr("Punch in %1 · punch out %2 samples. Prepared playback end: %3.")
                  .arg(QLocale().toString(m->session->punch.startFrame),
                       QLocale().toString(m->session->punch.endFrame),
                       r->projectMix && (r->phase == RecordingPhase::Ready ||
                                         r->phase == RecordingPhase::Recording ||
                                         r->phase == RecordingPhase::Complete ||
                                         r->phase == RecordingPhase::Finalizing)
                           ? QLocale().toString(r->endFrame)
                           : tr("not prepared"))
            : tr("Punch is off. Saved range: %1 to %2 samples.")
                  .arg(QLocale().toString(m->session->punch.startFrame),
                       QLocale().toString(m->session->punch.endFrame)));
}
bool StudioWindow::recordingRoutesReady(const RecordingSnapshot &r) const {
    if (!recordPortsShown_ || !r.channels || inputs_.size() != r.channels ||
        monitors_.size() != r.outputChannels)
        return false;
    const auto selected = [&](const std::vector<QComboBox *> &combos, bool input) {
        return std::all_of(combos.begin(), combos.end(), [&](const auto *combo) {
            const auto key = combo->currentData().toString();
            return std::any_of(
                recordPortsShown_->begin(), recordPortsShown_->end(),
                [&](const auto &p) { return p.input == input && portKey(p) == key; });
        });
    };
    return selected(inputs_, false) && selected(monitors_, true);
}
void StudioWindow::recordSelected() {
    const auto r = recording_.snapshot();
    if ((!r->projectMix && (!armed_->isChecked() || selectedTrack() != recordingTrack_)) ||
        r->phase != RecordingPhase::Ready || !recordPortsShown_ || closing_ || closeRequested_)
        return;
    if (!recordingRoutesReady(*r)) {
        recordingState_->setText(tr("Choose an input and every required monitoring output."));
        return;
    }
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
    const auto m = inspectorSnapshot();
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
void StudioWindow::selectRecordingRoute(bool output, std::size_t channel, QComboBox *combo) {
    const auto r = recording_.snapshot();
    if (!r->projectMix) {
        selectRoute(output ? RouteTarget::Monitor : RouteTarget::Input, channel, combo);
        return;
    }
    const auto m = controller_.snapshot();
    if (!m->session || closing_ || closeRequested_ || !recordPortsShown_)
        return;
    const auto key = combo->currentData().toString();
    if (key == "unresolved-route")
        return;
    const auto port =
        std::find_if(recordPortsShown_->begin(), recordPortsShown_->end(),
                     [&](const auto &p) { return p.input == output && portKey(p) == key; });
    if (!key.isEmpty() && port == recordPortsShown_->end())
        return;
    ProjectCommand c{CommandKind::Routing};
    if (output) {
        if (m->session->master)
            c.routeAddress = RouteAddress{m->session->master->id, RouteTarget::Master};
        else if (!m->session->tracks.empty())
            c.routeAddress = RouteAddress{m->session->tracks.front().id, RouteTarget::Output};
    } else {
        for (const auto &lane : r->lanes)
            if (channel >= lane.firstInput && channel < lane.firstInput + lane.layout.channels) {
                c.routeAddress = RouteAddress{lane.track, RouteTarget::Input};
                channel -= lane.firstInput;
                break;
            }
    }
    if (!c.routeAddress)
        return;
    c.routePatch = RouteChannelPatch{
        std::uint32_t(channel), "pipewire",
        port == recordPortsShown_->end() ? std::optional<ChannelPortIntent>{} : portIntent(*port)};
    if (!submitEdit(std::move(c)))
        notice_->setText(tr("Recording route was not admitted. Please retry."));
}
void StudioWindow::updateRecordingRoutes(const RecordingSnapshot &r) {
    const auto n = r.ports ? r.channels : 0;
    const auto out = r.ports ? r.outputChannels : 0;
    if (inputs_.size() != n || monitors_.size() != out ||
        (n && recordRoutesGeneration_ != r.generation)) {
        recordRoutesGeneration_ = r.generation;
        while (auto *item = recordRoutes_->takeAt(0)) {
            delete item->widget();
            delete item;
        }
        inputs_.clear();
        monitors_.clear();
        recordPortsShown_.reset();
        packedInputsShown_.clear();
        auto make = [&](std::vector<QComboBox *> &combos, bool input, std::uint32_t count) {
            for (std::uint32_t c = 0; c < count; ++c) {
                auto *combo = new FocusCombo;
                combo->setObjectName(
                    (input ? QStringLiteral("monitorChannel%1") : QStringLiteral("inputChannel%1"))
                        .arg(c));
                combo->setFocusPolicy(Qt::StrongFocus);
                combo->setToolTip(
                    tr("Saved routing changes take effect after Stop and preparation."));
                auto label =
                    input ? (r.projectMix ? tr("Project mix output %1") : tr("Monitor output %1"))
                          : tr("Input %1");
                label = label.arg(QLocale().toString(c + 1));
                if (!input && r.projectMix) {
                    for (const auto &lane : r.lanes)
                        if (c >= lane.firstInput && c < lane.firstInput + lane.layout.channels) {
                            const auto canonical = controller_.snapshot();
                            if (canonical->session)
                                for (const auto &t : canonical->session->tracks)
                                    if (t.id == lane.track)
                                        label =
                                            tr("%1 — input %2")
                                                .arg(text(t.name),
                                                     QLocale().toString(c - lane.firstInput + 1));
                        }
                }
                combo->setAccessibleName(label);
                combo->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
                combo->setMinimumContentsLength(16);
                auto *buddy = new QLabel(combo->accessibleName());
                buddy->setBuddy(combo);
                const auto row = int(c + (input ? n : 0));
                recordRoutes_->addWidget(buddy, row, 0);
                recordRoutes_->addWidget(combo, row, 1);
                combos.push_back(combo);
                connect(combo, &QComboBox::currentIndexChanged, this,
                        [this, combo, c, input] { selectRecordingRoute(input, c, combo); });
            }
        };
        make(inputs_, false, n);
        make(monitors_, true, out);
    }
    if (r.projectMix) {
        const auto model = controller_.snapshot();
        if (r.ports && model->session) {
            std::vector<RouteIntent> intents(n);
            for (const auto &lane : r.lanes)
                for (const auto &t : model->session->tracks)
                    if (t.id == lane.track)
                        for (std::uint32_t ch = 0; ch < lane.layout.channels; ++ch) {
                            auto &intent = intents[lane.firstInput + ch];
                            intent.backendId = t.input.backendId;
                            intent.ports.push_back(ch < t.input.ports.size() ? t.input.ports[ch]
                                                                             : std::nullopt);
                        }
            const auto output = model->session->master ? model->session->master->output
                                                       : model->session->tracks.front().output;
            const bool portsChanged = !recordPortsShown_ || *recordPortsShown_ != *r.ports;
            for (std::size_t ch = 0; ch < intents.size(); ++ch)
                if (portsChanged || ch >= packedInputsShown_.size() ||
                    packedInputsShown_[ch] != intents[ch])
                    populateRoutes({inputs_[ch]}, *r.ports, intents[ch], false);
            if (portsChanged || !monitorIntentShown_ || *monitorIntentShown_ != output)
                populateRoutes(monitors_, *r.ports, output, true);
            packedInputsShown_ = std::move(intents);
            monitorIntentShown_ = output;
            recordPortsShown_ = r.ports;
        }
        for (const auto &combos : {inputs_, monitors_})
            for (auto *combo : combos)
                combo->setEnabled(r.phase == RecordingPhase::Ready && !closing_ &&
                                  !closeRequested_);
        return;
    }
    const auto model = inspectorSnapshot();
    const auto input = model->session && !model->session->tracks.empty()
                           ? model->session->tracks.front().input
                           : RouteIntent{};
    const auto monitor = model->session && !model->session->tracks.empty()
                             ? model->session->tracks.front().monitor
                             : RouteIntent{};
    if (r.ports &&
        (!recordPortsShown_ || *recordPortsShown_ != *r.ports || !inputIntentShown_ ||
         !monitorIntentShown_ || *inputIntentShown_ != input || *monitorIntentShown_ != monitor)) {
        populateRoutes(inputs_, *r.ports, input, false);
        populateRoutes(monitors_, *r.ports, monitor, true);
        recordPortsShown_ = r.ports;
        inputIntentShown_ = input;
        monitorIntentShown_ = monitor;
    }
    for (const auto &combos : {inputs_, monitors_})
        for (auto *combo : combos)
            combo->setEnabled(r.phase == RecordingPhase::Ready &&
                              selectedTrack() == recordingTrack_ && !closing_ && !closeRequested_);
}
void StudioWindow::pollRecording() {
    const auto r = recording_.snapshot();
    const auto m = inspectorSnapshot();
    const auto p = playback_.snapshot();
    if (recordCommandPending_ &&
        (r->completedCommands > recordCommandCompleted_ || r->errorSerial > recordCommandError_ ||
         r->stopAcknowledged > recordCommandStop_))
        recordCommandPending_ = false;
    if (recordPrepareBarrier_ && (closing_ || closeRequested_))
        recordPrepareBarrier_ = 0;
    if (recordPrepareBarrier_ && m->lastBarrier == recordPrepareBarrier_) {
        recordPrepareBarrier_ = 0;
        if (m->barrierSession && !m->barrierSession->tracks.empty()) {
            RecordingCommand c;
            c.root = m->barrierRoot;
            c.session = recordPrepareMix_
                            ? m->barrierSession
                            : projectTrack(m->barrierSession, recordPreparationTrack_);
            if (!c.session) {
                notice_->setText(tr("Selected recording track no longer exists."));
                return;
            }
            c.modelRevision = m->barrierRevision;
            c.storageReserveMilliseconds = recordPreparationReserveMilliseconds_;
            c.monitoring = c.session->tracks.front().monitoring;
            if (recordPrepareMix_) {
                c.armedTracks = recordPreparationArms_;
                c.recordFrames = recordPreparationFrames_;
            }
            if (!submitRecording(std::move(c)))
                notice_->setText(tr("Recording queue is full or closing. Please retry."));
            else
                recordingTrack_ = recordPreparationTrack_;
        }
    }
    const auto mode = m->session && !m->session->tracks.empty()
                          ? m->session->tracks.front().monitoring
                          : RecordingMonitor::Off;
    if (m->session && !m->session->tracks.empty()) {
        const auto &track = m->session->tracks.front();
        if (latencyEpoch_ != m->projectEpoch || latencyTrack_ != track.id ||
            latencyShown_ != track.inputLatencyFrames ||
            (!inputLatency_->hasFocus() && inputLatency_->value() != track.inputLatencyFrames)) {
            QSignalBlocker blocked(inputLatency_);
            inputLatency_->setRange(0, int(m->session->sampleRate * 60));
            inputLatency_->setValue(int(track.inputLatencyFrames));
            inputLatencyTime_->setText(tr("%1 ms").arg(QLocale().toString(
                1000.0 * double(track.inputLatencyFrames) / m->session->sampleRate, 'f', 3)));
            latencyEpoch_ = m->projectEpoch;
            latencyTrack_ = track.id;
            latencyShown_ = track.inputLatencyFrames;
        }
    } else {
        QSignalBlocker blocked(inputLatency_);
        inputLatency_->setValue(0);
        inputLatencyTime_->clear();
        latencyTrack_.reset();
        latencyShown_.reset();
    }
    if (!monitoringShown_ || *monitoringShown_ != mode ||
        monitorMode_->currentData().toInt() != int(mode)) {
        QSignalBlocker blocked(monitorMode_);
        monitorMode_->setCurrentIndex(monitorMode_->findData(int(mode)));
        monitoringShown_ = mode;
    }
    if (m->session && m->modelRevision > recordingFollowed_) {
        const auto canonical = controller_.snapshot();
        const auto target =
            r->projectMix ? canonical->session : projectTrack(canonical->session, recordingTrack_);
        if (target && recording_.follow(canonical->root, target, canonical->modelRevision))
            recordingFollowed_ = m->modelRevision;
    }
    if (r->take) {
        if (takeShown_ != r->take->sequence) {
            takeShown_ = r->take->sequence;
            attachingTake_ = 0;
            attachmentFailed_ = false;
        }
        const bool group = r->take->receipts && r->take->receipts->size() > 1;
        std::vector<Id> ids;
        if (r->take->receipts)
            for (const auto &take : *r->take->receipts)
                ids.push_back(take.asset.id);
        if (m->root == r->take->root &&
            (group ? m->lastAttachedAssets == ids
                   : m->lastAttachedAsset == r->take->receipt->asset.id)) {
            recording_.acknowledgeTake(r->take->sequence);
            attachingTake_ = 0;
            attachmentFailed_ = false;
        } else if (attachingTake_ && ((m->attachmentCompleted.request == attachmentRequest_ &&
                                       m->attachmentCompleted.error) ||
                                      m->attachmentRejected.request == attachmentRequest_)) {
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
                c.attachmentRequest = nextGesture_++;
                const auto request = c.attachmentRequest;
                if (group)
                    c.recordings = r->take->receipts;
                else
                    c.recording = r->take->receipt;
                if (controller_.submit(std::move(c)) == Admission::Accepted) {
                    attachingTake_ = r->take->sequence;
                    attachmentRequest_ = request;
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
    const bool prepare = allow && !recordPrepareBarrier_ && !recordCommandPending_ &&
                         r->supported && (idle || ready) && !r->take && !attachingTake_ &&
                         playbackIdle && m->session && m->io == IoOperation::None;
    prepareRecordButton_->setEnabled(prepare);
    prepareRecordAction_->setEnabled(prepare);
    recordReserve_->setEnabled(prepare);
    monitorMode_->setToolTip(r->monitoringSupported
        ? tr("Saved monitoring mode takes effect after Stop and preparation.")
        : tr("Windows recording monitoring is not available in this preview. Use monitoring off."));
    if (auto *model = qobject_cast<QStandardItemModel *>(monitorMode_->model()))
        for (int index = 1; index < monitorMode_->count(); ++index)
            if (auto *item = model->item(index); item && item->isEnabled() != r->monitoringSupported)
                item->setEnabled(r->monitoringSupported);
    // Keep Off selectable when a portable project saved an unavailable mode.
    monitorMode_->setEnabled(allow && !recordPrepareBarrier_ && !recordCommandPending_ && idle &&
                             !r->take && m->session && !m->session->tracks.empty() &&
                             m->io != IoOperation::Create && m->io != IoOperation::Open);
    inputLatency_->setEnabled(allow && !recordPrepareBarrier_ && !recordCommandPending_ && idle &&
                              !r->take && !attachingTake_ && m->session &&
                              !m->session->tracks.empty() && m->io == IoOperation::None &&
                              !exportWorkflowBusy());
    armed_->setEnabled(allow && r->supported && m->session && !r->take);
    // Reconcile asynchronous route publication before deciding whether a click
    // can start. A Ready endpoint alone does not establish selected routes.
    updateRecordingRoutes(*r);
    recordButton_->setEnabled(
        allow && !recordCommandPending_ && ready &&
        (r->projectMix || (armed_->isChecked() && selectedTrack() == recordingTrack_)) &&
        recordingRoutesReady(*r));
    recordAction_->setEnabled(recordButton_->isEnabled());
    recordStopButton_->setEnabled(allow && (recordPrepareBarrier_ || !idle) && !r->closed);
    recoverAction_->setEnabled(allow && !recordCommandPending_ && idle && !r->take &&
                               !attachingTake_ && m->session && m->io == IoOperation::None);
    refreshArms();
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
    if ((r->phase == RecordingPhase::Ready || r->phase == RecordingPhase::Recording ||
         r->phase == RecordingPhase::Complete) &&
        !r->projectMix && mode != r->monitoring)
        status += tr(" · Prepared monitoring: %1. Stop and prepare to use the saved mode.")
                      .arg(r->monitoring == RecordingMonitor::Off ? tr("Off")
                           : r->monitoring == RecordingMonitor::AutoRecording
                               ? tr("Auto during recording")
                               : tr("Through track EQ"));
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
    if (!r->projectMix && recordingTrack_ && selectedTrack() != recordingTrack_ && r->ports)
        status += tr(" · Another track is prepared. Stop or prepare the selected track.");
    if (r->projectMix)
        status += tr(" · %n armed track(s)", nullptr, int(r->lanes.size())) +
                  tr(" · %1 missing playback frames")
                      .arg(QLocale().toString(r->telemetry.missingTrackFrames));
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
    inputMeter_->setVisible(!r->projectMix);
    inputLevel_->setVisible(!r->projectMix);
    meter(inputMeter_, inputLevel_, r->telemetry.inputPeak, tr("Input"));
    meter(monitorMeter_, monitorLevel_,
          !r->projectMix && r->monitoring == RecordingMonitor::Off ? 0 : r->telemetry.outputPeak,
          r->projectMix ? tr("Project output") : tr("Monitor"));
    if (r->errorSerial != recordingError_) {
        recordingError_ = r->errorSerial;
        auto diagnostic = text(r->diagnostic);
        if (r->phase == RecordingPhase::Fault && r->backendFaultDiagnostic && r->telemetry.firstFault) {
            diagnostic = recordingFaultText(*r->telemetry.firstFault);
        }
        if (!r->telemetry.faultStorageDiagnostic.empty())
            diagnostic += QStringLiteral(" ") +
                          tr("Recording error details could not be saved: %1")
                              .arg(text(r->telemetry.faultStorageDiagnostic));
        notice_->setText(tr("Recording could not be completed: %1. Any stored checkpoint remains "
                            "available for recovery.")
                             .arg(diagnostic));
    }
    if (r->preview && r->previewSequence != previewShown_) {
        previewShown_ = r->previewSequence;
        if (!allow)
            return;
        QMessageBox prompt(
            QMessageBox::Question, tr("Recover recording"),
            tr("Verified %1 frames (%2 seconds). Recover a new copy into this project? The "
               "original recording is preserved.")
                    .arg(QLocale().toString(r->preview->committedFrames),
                         QLocale().toString(double(r->preview->committedFrames) /
                                                r->preview->spec.capture.sampleRate,
                                            'f', 2)) +
                (r->preview->writerActivityConfirmed
                     ? QString()
                     : tr("\nThis older job has no writer lock; writer activity cannot be "
                          "confirmed. Only the verified checkpoint will be copied.")),
            QMessageBox::Yes | QMessageBox::No, this);
        prompt.setDefaultButton(QMessageBox::No);
        recoveryPrompt_ = &prompt;
        const auto answer = prompt.exec();
        recoveryPrompt_.clear();
        if (answer == QMessageBox::Yes && !closing_ && !closeRequested_ && m->session && r->job) {
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

std::shared_ptr<const RecoveryScanSnapshot> StudioWindow::recoverySnapshot() const {
    return recoveryScanner_.snapshot();
}
bool StudioWindow::scanRecordings() {
    const auto m = inspectorSnapshot();
    if (!m->session || m->io != IoOperation::None || closing_ || closeRequested_ || closeAfterSave_)
        return false;
    if (!recoveryScanner_.scan(m->root, m->session, m->projectEpoch))
        return false;
    recoveryEpoch_ = m->projectEpoch;
    recoveryJobSeen_ = recording_.snapshot()->job;
    recoveryRequested_ = m->session;
    return true;
}
QString StudioWindow::recordingFaultText(const AudioBridgeFault &fault) const {
    QString diagnostic;
    switch (fault.reason) {
    case AudioBridgeFaultReason::ControlRequest:
        diagnostic = tr("The audio backend stopped recording."); break;
    case AudioBridgeFaultReason::InvalidQuantum:
        diagnostic = tr("The audio block size was outside the prepared recording capacity."); break;
    case AudioBridgeFaultReason::RateChanged:
        diagnostic = tr("The input sample rate changed."); break;
    case AudioBridgeFaultReason::InvalidBuffer:
        diagnostic = tr("The recording input or output buffer was unavailable."); break;
    case AudioBridgeFaultReason::Xrun:
        diagnostic = tr("The audio backend reported a processing overrun."); break;
    case AudioBridgeFaultReason::DiscontinuityFlag:
        diagnostic = tr("The audio backend reported a clock discontinuity."); break;
    case AudioBridgeFaultReason::PositionOverflow:
        diagnostic = tr("The audio clock position exceeded the supported range."); break;
    case AudioBridgeFaultReason::ClockChanged:
        diagnostic = tr("The recording clock changed while the take was running."); break;
    case AudioBridgeFaultReason::PositionJump:
        diagnostic = tr("The recording clock position did not follow the previous block."); break;
    case AudioBridgeFaultReason::TimingOriginRejected:
        diagnostic = tr("The recording timing origin could not be established."); break;
    case AudioBridgeFaultReason::CaptureFailed:
        diagnostic = tr("The raw recording queue or writer stopped."); break;
    case AudioBridgeFaultReason::ProcessorFailed:
        diagnostic = tr("The prepared audio processor stopped."); break;
    }
    if (fault.callbackClock) {
        diagnostic += QStringLiteral(" ") +
            tr("Clock %1 at position %2, block %3 frames; engine frame %4.")
                .arg(QLocale().toString(fault.rejected.id),
                     QLocale().toString(fault.rejected.position),
                     QLocale().toString(fault.rejected.duration),
                     QLocale().toString(fault.engineFrame));
        if (fault.previousClock)
            diagnostic += QStringLiteral(" ") +
                tr("Previous clock %1 at position %2, block %3 frames.")
                    .arg(QLocale().toString(fault.previous.id),
                         QLocale().toString(fault.previous.position),
                         QLocale().toString(fault.previous.duration));
    }
    return diagnostic;
}
void StudioWindow::pollRecovery() {
    const auto m = inspectorSnapshot();
    const bool allow = m->session && m->io == IoOperation::None && !closing_ && !closeRequested_ &&
                       !closeAfterSave_;
    const auto recorder = recording_.snapshot();
    if (allow && recorder->job && !recorder->take && !recordingBusy() &&
        recorder->job != recoveryJobSeen_)
        scanRecordings();
    if (allow && (recoveryEpoch_ != m->projectEpoch || !recoveryRequested_ ||
                  recoveryRequested_->assets != m->session->assets))
        scanRecordings();
    const auto scan = recoveryScanner_.snapshot();
    const bool current =
        m->session && scan->root == m->root && scan->projectEpoch == m->projectEpoch;
    scanRecoveryAction_->setEnabled(allow);
    scanRecoveryButton_->setEnabled(allow && !scan->running);
    reviewRecoveryButton_->setEnabled(allow && current && !scan->running && scan->discovery &&
                                      (!scan->discovery->entries.empty() || !scan->discovery->faults.empty()) && !recordingBusy());
    if (!current)
        recoverySummary_->setText(tr("Open a project to find stored recordings."));
    else if (scan->running)
        recoverySummary_->setText(tr("Looking for stored recordings…"));
    else if (scan->errorCode)
        recoverySummary_->setText(tr("Recording discovery failed: %1").arg(text(scan->diagnostic)));
    else if (scan->discovery) {
        const auto &d = *scan->discovery;
        const auto count = std::count_if(d.entries.begin(), d.entries.end(), [](const auto &e) {
            return e.status == RecordingJobStatus::NeedsVerification ||
                   e.status == RecordingJobStatus::LegacyNeedsVerification;
        });
        auto message = tr("%n recording(s) need review.", nullptr, int(count));
        if (d.entries.size() > std::size_t(count))
            message += tr(" %n additional job(s) are listed.", nullptr,
                          int(d.entries.size() - std::size_t(count)));
        if (!d.faults.empty())
            message += tr(" Saved recording error details are available.");
        if (d.truncated)
            message += tr(" Discovery limit reached; this list is incomplete.");
        if (!d.warnings.empty())
            message += tr(" Some recovery metadata needs attention.");
        recoverySummary_->setText(message);
    }
}
void StudioWindow::reviewRecordings() {
    const auto scan = recoveryScanner_.snapshot();
    const auto m = inspectorSnapshot();
    if (!scan->discovery || scan->running || !m->session || scan->root != m->root ||
        scan->projectEpoch != m->projectEpoch || recordingBusy() || closing_ || closeRequested_)
        return;
    QDialog dialog(this);
    dialog.setObjectName("recordingRecoveryList");
    dialog.setWindowTitle(tr("Stored recordings"));
    QVBoxLayout layout(&dialog);
    QLabel help(tr("This list shows checkpoint metadata. Review verifies the audio before offering "
                   "a recovery copy. Active jobs cannot be reviewed."),
                &dialog);
    help.setWordWrap(true);
    layout.addWidget(&help);
    QListWidget list(&dialog);
    list.setObjectName("recoveryJobs");
    list.setWordWrap(true);
    layout.addWidget(&list, 1);
    for (const auto &e : scan->discovery->entries) {
        QString status;
        switch (e.status) {
        case RecordingJobStatus::NeedsVerification:
            status = tr("Needs audio verification");
            break;
        case RecordingJobStatus::LegacyNeedsVerification:
            status = tr("Needs verification — writer activity unconfirmed");
            break;
        case RecordingJobStatus::Active:
            status = tr("Active recording");
            break;
        case RecordingJobStatus::Empty:
            status = tr("No committed audio");
            break;
        case RecordingJobStatus::Foreign:
            status = tr("Different project, track, rate or layout");
            break;
        case RecordingJobStatus::Invalid:
            status = tr("Invalid checkpoint: %1").arg(text(e.diagnostic));
            break;
        case RecordingJobStatus::RecoveredSource:
            status = tr("Matching recovered copy is attached");
            break;
        }
        auto label = text(pathUtf8(e.job.filename())) + QStringLiteral(" — ") + status;
        if (e.checkpoint)
            label +=
                tr(" · %1 claimed frames").arg(QLocale().toString(e.checkpoint->committedFrames));
        list.addItem(label);
        list.item(list.count() - 1)
            ->setToolTip(QStringLiteral("<pre>") + text(pathUtf8(e.job)).toHtmlEscaped() +
                         QStringLiteral("</pre>"));
    }
    for (const auto &stored : scan->discovery->faults) {
        const auto detail = stored.fault
                                ? recordingFaultText(*stored.fault)
                                : tr("Stored recording error metadata is invalid: %1")
                                      .arg(text(stored.diagnostic));
        const auto label = tr("Saved recording error — %1: %2")
                               .arg(text(pathUtf8(stored.job.filename())), detail);
        list.addItem(label);
        list.item(list.count() - 1)->setToolTip(label);
    }
    for (const auto &warning : scan->discovery->warnings)
        list.addItem(tr("Metadata warning: %1").arg(text(warning)));
    if (scan->discovery->truncated) {
        auto *warning = new QLabel(tr("Discovery limit reached. The list is incomplete."), &dialog);
        warning->setWordWrap(true);
        layout.addWidget(warning);
    }
    QDialogButtonBox buttons(QDialogButtonBox::Cancel, &dialog);
    auto *review = buttons.addButton(tr("Review audio…"), QDialogButtonBox::AcceptRole);
    review->setObjectName("reviewSelectedRecording");
    review->setEnabled(false);
    layout.addWidget(&buttons);
    connect(&buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    connect(review, &QPushButton::clicked, &dialog, &QDialog::accept);
    connect(&list, &QListWidget::currentRowChanged, &dialog, [&](int row) {
        const auto valid = row >= 0 && std::size_t(row) < scan->discovery->entries.size();
        const auto status =
            valid ? scan->discovery->entries[std::size_t(row)].status : RecordingJobStatus::Invalid;
        review->setEnabled(valid && (status == RecordingJobStatus::NeedsVerification ||
                                     status == RecordingJobStatus::LegacyNeedsVerification ||
                                     status == RecordingJobStatus::RecoveredSource));
    });
    const auto area = screen() ? screen()->availableGeometry() : QRect(0, 0, 800, 600);
    dialog.resize(std::min(850, std::max(300, area.width() - 40)),
                  std::min(550, std::max(220, area.height() - 40)));
    recoveryDialog_ = &dialog;
    const auto answer = dialog.exec();
    recoveryDialog_.clear();
    const auto now = inspectorSnapshot();
    if (answer == QDialog::Accepted && !closing_ && !closeRequested_ &&
        now->projectEpoch == scan->projectEpoch && now->root == scan->root &&
        list.currentRow() >= 0 &&
        std::size_t(list.currentRow()) < scan->discovery->entries.size())
        inspectTake(scan->discovery->entries[std::size_t(list.currentRow())].job);
}

void StudioWindow::shutdownWorkers() {
    manual_->requestShutdown();
    recoveryScanner_.requestShutdown();
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
    if (polling_)
        return;
    QScopedValueRollback<bool> guard(polling_, true);
    const auto canonical = controller_.snapshot();
    if (!closeRequested_)
        manual_->cancelClose();
    manual_->poll();
    const auto native = playback_.snapshot();
    const bool inactive = native->phase == PlaybackPhase::Idle ||
                          native->phase == PlaybackPhase::Fault ||
                          native->phase == PlaybackPhase::Unsupported;
    syncGui(canonical, !closing_ && !closeRequested_ && !closeAfterSave_ && !recordingBusy() &&
                           !attachingTake_ && inactive && !playbackPrepareBarrier_ &&
                           canonical->io != IoOperation::Create &&
                           canonical->io != IoOperation::Open);
    // Keep timeline and inspector on the same published snapshot. A second
    // read could observe an Open completion between these two view updates.
    const auto view = inspectorSnapshot(canonical);
    if (routeRevisionShown_ != view->modelRevision || view->errorSerial != lastError_) {
        routeRevisionShown_ = view->modelRevision;
        outputIntentShown_.reset();
        inputIntentShown_.reset();
        monitorIntentShown_.reset();
        monitoringShown_.reset();
    }
    if (view->session && routeProjectEpoch_ != view->projectEpoch) {
        routeProjectEpoch_ = view->projectEpoch;
        outputIntentShown_.reset();
        inputIntentShown_.reset();
        monitorIntentShown_.reset();
        monitoringShown_.reset();
        outputsShown_.reset();
        recordPortsShown_.reset();
        for (const auto &combos : {outputs_, inputs_, monitors_})
            for (auto *combo : combos) {
                QSignalBlocker blocked(combo);
                combo->setCurrentIndex(0);
            }
    }
    pollRecording();
    pollRecovery();
    pollPlayback();
    pollExport();
    if (view->closed && playback_.snapshot()->closed && recording_.snapshot()->closed &&
        exporter_.snapshot()->closed && recoveryScanner_.snapshot()->closed &&
        manual_->snapshot()->closed && closing_) {
        close();
        return;
    }
    if (view->errorSerial != lastError_) {
        lastError_ = view->errorSerial;
        // A rejection already published when Close was requested must still be
        // displayed, but must not cancel that newer request. Errors from the
        // close/drain/barrier/save workflow keep the app and dirty model open.
        if (closeRequested_ && view->errorSerial > closeErrorSerial_) {
            closeAfterSave_ = false;
            closeSaveSubmitted_ = false;
            closeRequested_ = false;
            closeBarrier_ = 0;
        }
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
    // Save writes controller-owned canonical state, even when GUI admission
    // retains an older display and therefore supplies no editing projection.
    save_->setEnabled(bool(canonical->session) && view->io == IoOperation::None && !closing_ &&
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
        const auto canonical = controller_.snapshot();
        const auto target = playback_.snapshot()->projectMix
                                ? canonical->session
                                : projectTrack(canonical->session, playbackTrack_);
        if (target && playback_.follow(canonical->root, target, canonical->modelRevision))
            followedRevision_ = view->modelRevision;
    }
    if (guiBlocked_) {
        eq_->setEnabled(false);
        undo_->setEnabled(false);
        redo_->setEnabled(false);
        prepareButton_->setEnabled(false);
        prepareAction_->setEnabled(false);
        prepareRecordButton_->setEnabled(false);
        prepareRecordAction_->setEnabled(false);
        armedTrackModel_->checkEditing(false);
        punchEnabled_->setEnabled(false);
        punchRangeButton_->setEnabled(false);
        monitorMode_->setEnabled(false);
        inputLatency_->setEnabled(false);
        armed_->setEnabled(false);
        exportButton_->setEnabled(false);
        exportAction_->setEnabled(false);
        for (const auto &combos : {outputs_, inputs_, monitors_})
            for (auto *combo : combos)
                combo->setEnabled(false);
    }
    shown_ = view;
    int manualCloseReady = 0;
    if (closeRequested_ && !closing_ && !closeAfterSave_ && !closePromptActive_) {
        manualCloseReady = manual_->closeReadiness();
        if (manualCloseReady < 0) {
            closeRequested_ = false;
            closeBarrier_ = 0;
        }
    }
    if (closeRequested_ && !closing_ && !closeAfterSave_ && !closePromptActive_ && !closeBarrier_ &&
        recording_.snapshot()->stopAcknowledged >= closeDrainToken_ &&
        !recording_.snapshot()->take && !attachingTake_ && !exporter_.snapshot()->busy &&
        view->io == IoOperation::None && manualCloseReady == 1) {
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
    const auto view = inspectorSnapshot();
    if (view->closed && playback_.snapshot()->closed && recording_.snapshot()->closed &&
        exporter_.snapshot()->closed && recoveryScanner_.snapshot()->closed &&
        manual_->snapshot()->closed) {
        event->accept();
        return;
    }
    event->ignore();
    if (closing_ || closeAfterSave_ || closeRequested_)
        return;
    closeErrorSerial_ = view->errorSerial;
    if (auto *focused = focusWidget())
        focused->clearFocus();
    closeRequested_ = true;
    manual_->beginClose();
    recoveryScanner_.cancel();
    if (recoveryDialog_)
        recoveryDialog_->reject();
    if (recoveryPrompt_)
        recoveryPrompt_->done(QMessageBox::No);
    exportBarrier_ = 0;
    exportSelection_.reset();
    if (exportDialog_)
        exportDialog_->reject();
    if (exportPrompt_)
        exportPrompt_->done(QMessageBox::No);
    exporter_.requestCancel();
    playbackPrepareBarrier_ = 0;
    playback_.requestStop();
    closeDrainToken_ = recording_.requestStop();
}
void StudioWindow::confirmClose() {
    QScopedValueRollback<bool> promptGuard(closePromptActive_, true);
    const auto view = inspectorSnapshot();
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
