// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include "project_controller.hpp"
#include "timeline_editor.hpp"
#include "playback_controller.hpp"
#include "recording_controller.hpp"
#include "manual_recording_panel.hpp"
#include "recovery_controller.hpp"
#include "export_controller.hpp"
#include "export_dialog.hpp"
#include "import_inspection_controller.hpp"
#include "stretch_controller.hpp"
#include <QPointer>
#include <QMainWindow>
#include <QCoreApplication>
#include <vector>
class QLabel;
class QDoubleSpinBox;
class QSlider;
class QAction;
class QTimer;
class QGroupBox;
class QComboBox;
class QPushButton;
class QProgressBar;
class QGridLayout;
class QCheckBox;
class QMessageBox;
class QListWidget;
class QListView;
class QSpinBox;
namespace soundcurrent::daw::ui {
class SessionListModel;
class ImportInspectionDialog;
class StretchDialog;
struct MemoryPreferences;
class StudioWindow : public QMainWindow {
    Q_DECLARE_TR_FUNCTIONS(StudioWindow)
  public:
    explicit StudioWindow(QWidget *parent = nullptr, PlaybackControllerOptions = {},
                          RecordingControllerOptions = {}, ExportControllerOptions = {},
                          ManualControlOptions = {}, ControllerOptions = {},
                          std::function<void(HistoryBudget)> historyAccepted = {},
                          std::function<void(MemoryPreferences)> memoryAccepted = {},
                          StretchOptions stretchOptions = {});
    bool requestClipStretch(const Id &, const Id &, StretchSettings,std::optional<StretchContext> = {},const std::optional<WarpSettings> & = {});
    bool applyClipStretch();
    bool auditionClipStretch();
    bool scanStretchRenders();
    std::shared_ptr<const StretchSnapshot> stretchSnapshot() const;
    std::shared_ptr<const ManualControlSnapshot> manualRecordingSnapshot() const;
    void openProject(const std::filesystem::path &);
    bool submitEdit(ProjectCommand); // Shared entry for bindings/UI acceptance.
    std::shared_ptr<const ControllerSnapshot> snapshot() const;
    std::shared_ptr<const PlaybackSnapshot> playbackSnapshot() const;
    bool preparePlayback();
    bool selectTrack(const Id &);
    std::optional<Id> selectedTrack() const;
    ResourceUsage memoryResources() const;
    ResourceLedger resourceLedger() const; // Off-audio GUI ownership facade.
    std::size_t guiResourceBytes() const;
    std::uint64_t displayedRevision() const {
        return guiRevision_;
    }
    bool prepareRecording();
    bool configureArmedRecording(const std::vector<Id> &, Frame frames = 0);
    bool configurePunch(PunchSettings);
    bool configureInputLatency(const Id &, Frame);
    bool inspectTake(const std::filesystem::path &);
    std::shared_ptr<const RecordingSnapshot> recordingSnapshot() const;
    std::shared_ptr<const ExportSnapshot> exportSnapshot() const;
    bool requestExport();
    bool scanRecordings();
    bool inspectForeignProject(const std::filesystem::path &);
    std::shared_ptr<const InspectionSnapshot> importInspectionSnapshot() const;
    std::shared_ptr<const RecoveryScanSnapshot> recoverySnapshot() const;

  protected:
    void closeEvent(QCloseEvent *) override;

  private:
    ProjectController controller_;
    StretchController stretch_;
    QPointer<StretchDialog> stretchDialog_;
    QPointer<QDialog> stretchRecoveryDialog_;
    QString stretchMessage_;
    std::uint64_t stretchAdopting_ = 0;
    bool stretchCanApply() const;
    bool stretchCanRender() const;
    void showClipStretch(const Id &, const Id &);
    void pollStretch(const std::shared_ptr<const ControllerSnapshot> &);
    QPointer<ImportInspectionDialog> importDialog_;
    void showImportInspection();
    QPointer<QDialog> historyDialog_;
    TimelineEditor *timeline_ = nullptr;
    std::shared_ptr<const Session> inspectorSource_, inspectorProjection_;
    std::uint64_t guiEpoch_ = 0, guiRevision_ = 0;
    std::shared_ptr<const Session> guiRefusedSource_;
    std::uint64_t guiRefusedEpoch_ = 0;
    ResourceUsage guiRefusedUsage_{};
    bool guiBlocked_ = false;
    QAction *retryGui_ = nullptr;
    bool syncGui(const std::shared_ptr<const ControllerSnapshot> &, bool editable);
    std::shared_ptr<const Session> projectTrack(std::shared_ptr<const Session>, std::optional<Id>);
    std::optional<Id> inspectorTrack_;
    std::shared_ptr<const ControllerSnapshot>
    inspectorSnapshot(std::shared_ptr<const ControllerSnapshot> canonical = {}) const;
    std::optional<Id> playbackTrack_, recordingTrack_, recordPreparationTrack_, exportTrack_;
    bool polling_ = false;
    std::uint64_t playbackPrepareBarrier_ = 0;
    std::optional<Id> playbackPreparationTrack_;
    bool playbackPrepareMix_ = false;
    QCheckBox *mixTracks_ = nullptr;
    QPushButton *masterButton_ = nullptr;
    PlaybackController playback_;
    RecordingController recording_;
    ExportController exporter_;
    RecoveryController recoveryScanner_;
    QLabel *recoverySummary_;
    QPushButton *reviewRecoveryButton_, *scanRecoveryButton_;
    QAction *scanRecoveryAction_;
    QPointer<QDialog> recoveryDialog_;
    QPointer<QMessageBox> recoveryPrompt_;
    std::uint64_t recoveryEpoch_ = 0;
    std::optional<std::filesystem::path> recoveryJobSeen_;
    std::shared_ptr<const Session> recoveryRequested_;
    QString recordingFaultText(const AudioBridgeFault &) const;
    void pollRecovery();
    void reviewRecordings();
    QAction *exportAction_, *cancelExportAction_;
    QLabel *exportState_;
    QProgressBar *exportProgress_;
    QPushButton *exportButton_, *cancelExportButton_;
    QPointer<QDialog> exportDialog_;
    QPointer<QMessageBox> exportPrompt_;
    std::uint64_t exportBarrier_ = 0, exportPromptJob_ = 0;
    std::optional<ExportSelection> exportSelection_;
    bool exportWorkflowBusy() const;
    void pollExport();
    QAction *prepareRecordAction_, *recordAction_, *recoverAction_;
    QPushButton *prepareRecordButton_, *recordButton_, *recordStopButton_;
    QPushButton *retryTakeButton_, *keepTakeButton_;
    QCheckBox *armed_;
    QCheckBox *multiRecord_;
    QListView *armedTracksList_;
    SessionListModel *armedTrackModel_;
    QSpinBox *recordSeconds_;
    QComboBox *recordReserve_;
    std::uint32_t recordPreparationReserveMilliseconds_ = 10000;
    QLabel *recordRangeLabel_;
    QCheckBox *punchEnabled_ = nullptr;
    QPushButton *punchRangeButton_ = nullptr;
    QLabel *punchSummary_ = nullptr;
    std::optional<PunchSettings> punchShown_;
    QPointer<QDialog> punchDialog_;
    void editPunchRange();
    std::vector<Id> armedTracksSelection_, recordPreparationArms_;
    Frame recordRangeOverride_ = 0, recordPreparationFrames_ = 0;
    bool recordPrepareMix_ = false;
    std::uint64_t armProjectEpoch_ = 0, recordRoutesGeneration_ = 0;
    std::vector<RouteIntent> packedInputsShown_;
    void refreshArms();
    void selectRecordingRoute(bool output, std::size_t, QComboBox *);
    QComboBox *monitorMode_;
    QSpinBox *inputLatency_;
    QLabel *inputLatencyTime_;
    std::optional<Frame> latencyShown_;
    std::optional<Id> latencyTrack_;
    std::uint64_t latencyEpoch_ = 0;
    QGridLayout *recordRoutes_;
    std::vector<QComboBox *> inputs_, monitors_;
    std::shared_ptr<const std::vector<PipeWirePort>> recordPortsShown_;
    QLabel *recordingState_, *inputLevel_, *monitorLevel_;
    QProgressBar *inputMeter_, *monitorMeter_;
    std::uint64_t recordingFollowed_ = 0, recordingError_ = 0, previewShown_ = 0;
    std::uint64_t takeShown_ = 0, attachingTake_ = 0, attachmentRequest_ = 0, closeDrainToken_ = 0;
    bool attachmentFailed_ = false, recordCommandPending_ = false;
    std::uint64_t recordPrepareBarrier_ = 0;
    std::optional<RecordingMonitor> monitoringShown_;
    std::uint64_t recordCommandCompleted_ = 0, recordCommandError_ = 0, recordCommandStop_ = 0;
    bool submitRecording(RecordingCommand);
    bool recordingBusy() const;
    void pollRecording();
    void updateRecordingRoutes(const RecordingSnapshot &);
    void recordSelected();
    QString recordingRoutesProblem(const RecordingSnapshot &) const;
    QString playbackRoutesProblem(const PlaybackSnapshot &) const;
    QString selectedRoutesProblem(const std::vector<QComboBox *> &,
                                  const std::vector<PipeWirePort> &,
                                  std::uint32_t sampleRate, bool endpointInput) const;
    void retryTake();
    std::shared_ptr<const ControllerSnapshot> shown_;
    QLabel *project_, *track_, *state_, *notice_;
    QGroupBox *eq_;
    QTimer *timer_;
    QAction *new_, *open_, *save_, *undo_, *redo_;
    QAction *playAction_, *stopAction_, *prepareAction_;
    QPushButton *prepareButton_, *playButton_, *stopButton_;
    QLabel *playbackState_, *level_;
    QProgressBar *meter_;
    QGridLayout *outputsLayout_;
    std::vector<QComboBox *> outputs_;
    std::shared_ptr<const std::vector<PipeWirePort>> outputsShown_;
    std::uint64_t followedRevision_ = 0, playbackError_ = 0;
    struct BandEditors {
        QDoubleSpinBox *frequency = nullptr, *gain = nullptr, *q = nullptr;
        QSlider *slider = nullptr;
        std::optional<ParameterAddress> frequencyAddress, gainAddress, qAddress;
        std::uint64_t frequencyGesture = 0, gainGesture = 0, qGesture = 0, sliderGesture = 0;
    };
    std::vector<BandEditors> bands_;
    std::uint64_t nextGesture_ = 1, lastError_ = 0;
    bool closing_ = false, closeAfterSave_ = false, closeRequested_ = false;
    bool closeSaveSubmitted_ = false, closePromptActive_ = false;
    std::uint64_t closeBarrier_ = 0;
    std::uint64_t closeErrorSerial_ = 0;
    // Destroy the pane before the project worker and its referenced gesture sequence.
    std::unique_ptr<ManualRecordingPanel> manual_;
    void poll();
    void pollPlayback();
    void playSelected();
    void updateOutputs(const PlaybackSnapshot &);
    std::uint64_t routeProjectEpoch_ = 0;
    std::uint64_t routeRevisionShown_ = 0;
    std::optional<RouteIntent> outputIntentShown_, inputIntentShown_, monitorIntentShown_;
    void populateRoutes(const std::vector<QComboBox *> &, const std::vector<PipeWirePort> &,
                        const RouteIntent &, bool endpointInput);
    void selectRoute(RouteTarget, std::size_t channel, QComboBox *);
    void shutdownWorkers();
    void newProject();
    void confirmClose();
    void rebuildBands(const Session &);
    void updateBands(const Session &);
    void edit(std::size_t band, BandParameter, double, bool final = false, bool slider = false);
};
} // namespace soundcurrent::daw::ui
