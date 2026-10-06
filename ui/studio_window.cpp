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
StudioWindow::StudioWindow(QWidget *parent, PlaybackControllerOptions options)
    : QMainWindow(parent), playback_(std::move(options)) {
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
            if (playback_.snapshot()->phase == PlaybackPhase::Playing)
                playback_.requestStop();
            else
                playSelected();
        });
    stopAction_ = transportMenu->addAction(tr("Stop"), QKeySequence(Qt::SHIFT | Qt::Key_Space),
                                           this, [this] { playback_.requestStop(); });
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
    eq_ = new QGroupBox(tr("Track equalizer"), body);
    eq_->setObjectName(QStringLiteral("equalizerGroup"));
    eq_->setLayout(new QGridLayout);
    layout->addWidget(eq_);
    track_ = new QLabel(body);
    track_->setWordWrap(true);
    layout->addWidget(track_);
    notice_ = new QLabel(
        tr("Development preview: project editing and first-track playback are available on "
           "Linux. Recording and export controls are being integrated."),
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
    const auto available = screen()->availableGeometry();
    resize(std::min(1000, available.width()), std::min(640, available.height()));
    for (auto *label : {project_, track_, state_, notice_, playbackState_, level_})
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
    ProjectCommand command;
    command.kind = CommandKind::Open;
    command.path = root;
    playback_.requestStop();
    submitEdit(std::move(command));
}
void StudioWindow::newProject() {
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
        view->io == IoOperation::Create || view->io == IoOperation::Open)
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
    const bool prepare = allow && p->supported && idle && model->session &&
                         model->io != IoOperation::Create && model->io != IoOperation::Open;
    prepareButton_->setEnabled(prepare);
    prepareAction_->setEnabled(prepare);
    playButton_->setEnabled(allow && p->phase == PlaybackPhase::Ready);
    playAction_->setEnabled(
        allow && (p->phase == PlaybackPhase::Ready || p->phase == PlaybackPhase::Playing));
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
void StudioWindow::shutdownWorkers() {
    playback_.requestShutdown();
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
    pollPlayback();
    if (view->closed && playback_.snapshot()->closed && closing_) {
        close();
        return;
    }
    if (view->errorSerial != lastError_) {
        lastError_ = view->errorSerial;
        closeAfterSave_ = false;
        closeSaveSubmitted_ = false;
        closeRequested_ = false;
        closeBarrier_ = 0;
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
    new_->setEnabled(view->io == IoOperation::None && !view->dirty && !closing_ &&
                     !closeRequested_);
    open_->setEnabled(view->io == IoOperation::None && !view->dirty && !closing_ &&
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
    state_->setText(closing_                                   ? tr("Closing project…")
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
    if (view->closed && playback_.snapshot()->closed) {
        event->accept();
        return;
    }
    event->ignore();
    if (closing_ || closeAfterSave_ || closeRequested_)
        return;
    if (auto *focused = focusWidget())
        focused->clearFocus();
    ProjectCommand barrier;
    barrier.kind = CommandKind::Barrier;
    barrier.barrier = nextGesture_++;
    const auto token = barrier.barrier;
    if (submitEdit(std::move(barrier))) {
        closeRequested_ = true;
        closeBarrier_ = token;
    }
}
void StudioWindow::confirmClose() {
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
