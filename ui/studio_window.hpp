// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include "project_controller.hpp"
#include "playback_controller.hpp"
#include "recording_controller.hpp"
#include "export_controller.hpp"
#include "export_dialog.hpp"
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
namespace soundcurrent::daw::ui {
class StudioWindow : public QMainWindow {
  public:
    explicit StudioWindow(QWidget *parent = nullptr, PlaybackControllerOptions = {},
                          RecordingControllerOptions = {}, ExportControllerOptions = {});
    void openProject(const std::filesystem::path &);
    bool submitEdit(ProjectCommand); // Shared entry for bindings/UI acceptance.
    std::shared_ptr<const ControllerSnapshot> snapshot() const;
    std::shared_ptr<const PlaybackSnapshot> playbackSnapshot() const;
    bool preparePlayback();
    bool prepareRecording();
    bool inspectTake(const std::filesystem::path &);
    std::shared_ptr<const RecordingSnapshot> recordingSnapshot() const;
    std::shared_ptr<const ExportSnapshot> exportSnapshot() const;
    bool requestExport();
    static QString tr(const char *source, const char *comment = nullptr, int count = -1) {
        return QCoreApplication::translate("StudioWindow", source, comment, count);
    }

  protected:
    void closeEvent(QCloseEvent *) override;

  private:
    ProjectController controller_;
    PlaybackController playback_;
    RecordingController recording_;
    ExportController exporter_;
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
    QComboBox *monitorMode_;
    QGridLayout *recordRoutes_;
    std::vector<QComboBox *> inputs_, monitors_;
    std::shared_ptr<const std::vector<PipeWirePort>> recordPortsShown_;
    QLabel *recordingState_, *inputLevel_, *monitorLevel_;
    QProgressBar *inputMeter_, *monitorMeter_;
    std::uint64_t recordingFollowed_ = 0, recordingError_ = 0, previewShown_ = 0;
    std::uint64_t takeShown_ = 0, attachingTake_ = 0, attachmentError_ = 0, closeDrainToken_ = 0;
    bool attachmentFailed_ = false, recordCommandPending_ = false;
    std::uint64_t recordPrepareBarrier_ = 0;
    std::optional<RecordingMonitor> monitoringShown_;
    std::uint64_t recordCommandCompleted_ = 0, recordCommandError_ = 0, recordCommandStop_ = 0;
    bool submitRecording(RecordingCommand);
    bool recordingBusy() const;
    void pollRecording();
    void updateRecordingRoutes(const RecordingSnapshot &);
    void recordSelected();
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
