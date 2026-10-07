// SPDX-License-Identifier: GPL-3.0-only
#include "timeline_editor.hpp"
#include <QComboBox>
#include <QFormLayout>
#include <QGraphicsView>
#include <QGraphicsScene>
#include <QGraphicsRectItem>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QLocale>
#include <QPushButton>
#include <QSignalBlocker>
#include <QSlider>
#include <QSpinBox>
#include <QSplitter>
#include <QTimer>
#include <QVBoxLayout>
#include <QScopedValueRollback>
#include <QWheelEvent>
#include <algorithm>
#include <charconv>
#include <cmath>
#include <limits>
namespace soundcurrent::daw::ui {
namespace {
class FocusCombo : public QComboBox {
    void wheelEvent(QWheelEvent *e) override {
        if (hasFocus())
            QComboBox::wheelEvent(e);
        else
            e->ignore();
    }
};
QString text(const std::string &s) {
    return QString::fromUtf8(s);
}
void field(QLineEdit *w, Frame f, bool force) {
    if (force || !w->hasFocus())
        w->setText(QString::number(f));
}
} // namespace
TimelineEditor::TimelineEditor(QWidget *parent) : QGroupBox(tr("Tracks and timeline"), parent) {
    setObjectName("timelineEditor");
    auto *body = new QVBoxLayout(this);
    auto *splitter = new QSplitter(this);
    tracks_ = new QListWidget;
    tracks_->setObjectName("timelineTracks");
    tracks_->setAccessibleName(tr("Audio tracks"));
    tracks_->setMinimumWidth(140);
    scene_ = new QGraphicsScene(this);
    view_ = new QGraphicsView(scene_);
    view_->setObjectName("audioTimeline");
    view_->setAccessibleName(tr("Audio clip timeline"));
    view_->setMinimumHeight(180);
    view_->setAlignment(Qt::AlignLeft | Qt::AlignTop);
    splitter->addWidget(tracks_);
    splitter->addWidget(view_);
    splitter->setStretchFactor(1, 1);
    splitter->setSizes({170, 700});
    body->addWidget(splitter);
    auto *trackRow = new QHBoxLayout;
    name_ = new QLineEdit;
    name_->setObjectName("selectedTrackName");
    name_->setAccessibleName(tr("Selected track name"));
    layout_ = new FocusCombo;
    layout_->setObjectName("newTrackLayout");
    layout_->addItem(tr("Mono"), 0);
    layout_->addItem(tr("Stereo"), 1);
    layout_->addItem(tr("Discrete channels"), 2);
    channels_ = new QSpinBox;
    channels_->setObjectName("newTrackChannels");
    channels_->setRange(1, 256);
    channels_->setValue(2);
    channels_->setAccessibleName(tr("New track channel count"));
    trackRow->addWidget(new QLabel(tr("Track name")));
    trackRow->addWidget(name_, 1);
    trackRow->addWidget(layout_);
    trackRow->addWidget(channels_);
    auto button = [&](QHBoxLayout *row, const char *caption, const char *object,
                      std::function<void()> action) {
        auto *b = new QPushButton(tr(caption));
        b->setObjectName(object);
        mutations_.push_back(b);
        row->addWidget(b);
        connect(b, &QPushButton::clicked, this, [this, action] { operation(action); });
        return b;
    };
    body->addLayout(trackRow);
    trackRow = new QHBoxLayout;
    button(trackRow, "Add track", "addAudioTrack", [this] {
        if (!model_)
            return;
        ChannelLayout l;
        if (layout_->currentData().toInt() == 1)
            l = {LayoutKind::Stereo, 2};
        else if (layout_->currentData().toInt() == 2)
            l = {LayoutKind::Discrete, std::uint32_t(channels_->value())};
        auto t = makeAudioTrack(tr("Audio %1")
                                    .arg(QLocale().toString(model_->tracks.size() + 1))
                                    .toUtf8()
                                    .toStdString(),
                                l, model_->sampleRate);
        const auto id = t.id;
        mutate({InsertTrack{std::move(t), {}}});
        pendingTrack_ = id;
    });
    button(trackRow, "Rename", "renameAudioTrack", [this] {
        if (track_)
            mutate({RenameTrack{*track_, name_->text().toUtf8().toStdString()}});
    });
    button(trackRow, "Remove track", "removeAudioTrack", [this] {
        if (track_)
            mutate({RemoveTrack{*track_}});
    });
    button(trackRow, "Up", "moveAudioTrackUp", [this] {
        if (!track())
            return;
        const auto it = std::find_if(model_->tracks.begin(), model_->tracks.end(),
                                     [&](const auto &t) { return t.id == *track_; });
        if (it != model_->tracks.begin())
            mutate({MoveTrack{*track_, (it - 1)->id}});
    });
    button(trackRow, "Down", "moveAudioTrackDown", [this] {
        if (!track())
            return;
        const auto it = std::find_if(model_->tracks.begin(), model_->tracks.end(),
                                     [&](const auto &t) { return t.id == *track_; });
        if (it + 1 != model_->tracks.end())
            mutate({MoveTrack{*track_, it + 2 == model_->tracks.end()
                                           ? std::optional<Id>{}
                                           : std::optional<Id>((it + 2)->id)}});
    });
    body->addLayout(trackRow);
    auto *ranges = new QFormLayout;
    clips_ = new FocusCombo;
    clips_->setObjectName("timelineClips");
    clips_->setAccessibleName(tr("Clips in selected track"));
    ranges->addRow(tr("Selected clip"), clips_);
    connect(clips_, &QComboBox::currentIndexChanged, this, [this] {
        if (refreshing_)
            return;
        const auto id = clips_->currentData().toString();
        clip_ = id.isEmpty() ? std::optional<Id>{} : std::optional<Id>(Id(id.toStdString()));
        refresh(true);
        if (selectionChanged)
            selectionChanged();
    });
    auto frameField = [&](const char *object, const char *caption) {
        auto *w = new QLineEdit;
        w->setObjectName(object);
        w->setAccessibleName(tr(caption));
        ranges->addRow(tr(caption), w);
        return w;
    };
    start_ = frameField("clipStartFrame", "Timeline start frame");
    source_ = frameField("clipSourceFrame", "Source start frame");
    length_ = frameField("clipLengthFrames", "Length in frames");
    split_ = frameField("clipSplitFrame", "Split at timeline frame");
    body->addLayout(ranges);
    auto *clipRow = new QHBoxLayout;
    button(clipRow, "Apply range", "applyClipRange", [this] {
        if (clip_ && track_)
            mutate({SetClipRange{*track_, *clip_, frame(start_), frame(source_), frame(length_)}});
    });
    button(clipRow, "Split", "splitAudioClip", [this] {
        if (clip_ && track_)
            mutate({SplitClip{*track_, *clip_, Id::generate(), frame(split_)}});
    });
    button(clipRow, "Duplicate", "duplicateAudioClip", [this] {
        if (const auto *c = clip()) {
            if (c->startFrame > std::numeric_limits<Frame>::max() - c->lengthFrames)
                throw ProjectError(ErrorCode::InvalidState, "Clip position overflow");
            auto copy = *c;
            copy.id = Id::generate();
            copy.startFrame += copy.lengthFrames;
            mutate({InsertClip{*track_, std::move(copy), {}}});
        }
    });
    button(clipRow, "Remove clip", "removeAudioClip", [this] {
        if (clip_ && track_)
            mutate({RemoveClip{*track_, *clip_}});
    });
    destination_ = new FocusCombo;
    destination_->setObjectName("clipDestinationTrack");
    destination_->setAccessibleName(tr("Clip destination track"));
    clipRow->addWidget(destination_, 1);
    button(clipRow, "Move to track", "moveClipToTrack", [this] {
        if (clip_ && track_ && destination_->currentIndex() >= 0)
            mutate({MoveClip{*track_,
                             Id(destination_->currentData().toString().toStdString()),
                             *clip_,
                             frame(start_),
                             {}}});
    });
    body->addLayout(clipRow);
    auto *assetRow = new QHBoxLayout;
    asset_ = new FocusCombo;
    asset_->setObjectName("timelineAsset");
    asset_->setAccessibleName(tr("Recorded media asset"));
    assetRow->addWidget(asset_, 1);
    button(assetRow, "Insert media clip", "insertMediaClip", [this] {
        if (!track_ || !model_ || asset_->currentIndex() < 0)
            return;
        const auto id = Id(asset_->currentData().toString().toStdString());
        const auto it = std::find_if(model_->assets.begin(), model_->assets.end(),
                                     [&](const auto &a) { return a.id == id; });
        if (it == model_->assets.end())
            return;
        Clip c;
        c.assetId = id;
        c.startFrame = model_->playheadFrame;
        c.lengthFrames = it->frames;
        mutate({InsertClip{*track_, std::move(c), {}}});
    });
    zoom_ = new QSlider(Qt::Horizontal);
    zoom_->setObjectName("timelineZoom");
    zoom_->setRange(0, 100);
    zoom_->setValue(50);
    zoom_->setAccessibleName(tr("Timeline zoom"));
    assetRow->addWidget(new QLabel(tr("Zoom")));
    assetRow->addWidget(zoom_);
    body->addLayout(assetRow);
    status_ = new QLabel;
    status_->setObjectName("timelineStatus");
    status_->setWordWrap(true);
    status_->setTextFormat(Qt::PlainText);
    body->addWidget(status_);
    connect(zoom_, &QSlider::valueChanged, this, [this] { draw(); });
    connect(tracks_, &QListWidget::currentRowChanged, this, [this](int row) {
        if (refreshing_ || !model_ || row < 0 || std::size_t(row) >= model_->tracks.size())
            return;
        const auto id = model_->tracks[std::size_t(row)].id;
        // Reconciliation can rebuild the list. Let its current-row event finish.
        QTimer::singleShot(0, this, [this, id] { selectTrack(id); });
    });
    connect(scene_, &QGraphicsScene::selectionChanged, this, [this] {
        if (refreshing_)
            return;
        const auto items = scene_->selectedItems();
        if (items.empty()) {
            clip_.reset();
            refresh(true, false);
            return;
        }
        const auto *item = items.front();
        const auto trackId = item->data(0).toString(), clipId = item->data(1).toString();
        if (trackId.isEmpty() || clipId.isEmpty())
            return;
        if (auto *focused = window()->focusWidget())
            focused->clearFocus();
        track_ = Id(trackId.toStdString());
        clip_ = Id(clipId.toStdString());
        refresh(true, false);
        // Window reconciliation may observe a newly published model and redraw
        // the scene. Never delete scene items inside their mouse event stack.
        QTimer::singleShot(0, this, [this] {
            if (selectionChanged)
                selectionChanged();
        });
    });
    refresh(true);
}
TimelineEditor::~TimelineEditor() {
    // Child scenes can emit selectionChanged during QWidget child teardown,
    // after this class's members have been destroyed.
    for (auto *sender : findChildren<QObject *>())
        QObject::disconnect(sender, nullptr, this, nullptr);
}
const Track *TimelineEditor::track() const {
    if (!model_ || !track_)
        return nullptr;
    const auto it = std::find_if(model_->tracks.begin(), model_->tracks.end(),
                                 [&](const auto &t) { return t.id == *track_; });
    return it == model_->tracks.end() ? nullptr : &*it;
}
const Clip *TimelineEditor::clip() const {
    const auto *t = track();
    if (!t || !clip_)
        return nullptr;
    const auto it = std::find_if(t->clips.begin(), t->clips.end(),
                                 [&](const auto &c) { return c.id == *clip_; });
    return it == t->clips.end() ? nullptr : &*it;
}
Frame TimelineEditor::frame(QLineEdit *w) const {
    const auto bytes = w->text().toLatin1();
    Frame n = 0;
    const auto [end, error] =
        std::from_chars(bytes.constData(), bytes.constData() + bytes.size(), n);
    if (error != std::errc{} || end != bytes.constData() + bytes.size() || n < 0)
        throw ProjectError(ErrorCode::InvalidParameter,
                           "Frame must be a nonnegative decimal integer");
    return n;
}
void TimelineEditor::operation(const std::function<void()> &f) {
    if (!editable_)
        return;
    try {
        f();
    } catch (const std::exception &e) {
        status_->setText(tr("Edit failed: %1").arg(text(e.what())));
    }
}
void TimelineEditor::mutate(std::vector<SessionEdit> edits) {
    if (!submit || !submit(std::move(edits)))
        throw ProjectError(ErrorCode::InvalidState, "Edit was not admitted; please retry");
}
bool TimelineEditor::selectTrack(const Id &id) {
    if (!model_ || std::none_of(model_->tracks.begin(), model_->tracks.end(),
                                [&](const auto &t) { return t.id == id; }))
        return false;
    if (track_ == std::optional<Id>(id))
        return true;
    if (auto *focused = window()->focusWidget())
        focused->clearFocus();
    track_ = id;
    clip_.reset();
    pendingTrack_.reset();
    refresh(true);
    if (selectionChanged)
        selectionChanged();
    return true;
}
void TimelineEditor::updateModel(std::shared_ptr<const Session> s, std::uint64_t epoch,
                                 bool editable) {
    const bool changed = s != model_ || epoch != epoch_;
    const bool force = epoch != epoch_;
    const auto previous = track_;
    if (force) {
        track_.reset();
        clip_.reset();
        pendingTrack_.reset();
    }
    model_ = std::move(s);
    epoch_ = epoch;
    if (pendingTrack_ && model_ &&
        std::any_of(model_->tracks.begin(), model_->tracks.end(),
                    [&](const auto &t) { return t.id == *pendingTrack_; })) {
        track_ = pendingTrack_;
        pendingTrack_.reset();
        clip_.reset();
    }
    if (!track()) {
        track_ = model_ && !model_->tracks.empty() ? std::optional<Id>(model_->tracks.front().id)
                                                   : std::nullopt;
        clip_.reset();
    }
    if (!clip())
        clip_.reset();
    const bool enabledChanged = editable_ != editable;
    editable_ = editable;
    if (changed || enabledChanged || previous != track_)
        refresh(force);
    if (previous != track_ && selectionChanged)
        selectionChanged();
}
void TimelineEditor::refresh(bool force, bool redraw) {
    QScopedValueRollback<bool> guard(refreshing_, true);
    QSignalBlocker block(tracks_);
    const auto previousDestination = destination_->currentData();
    const auto previousAsset = asset_->currentData();
    tracks_->clear();
    destination_->clear();
    asset_->clear();
    const auto *t = track();
    const auto *c = clip();
    {
        QSignalBlocker blocked(clips_);
        clips_->clear();
        clips_->addItem(tr("Select a clip"), QString());
        if (t)
            for (std::size_t n = 0; n < t->clips.size(); ++n) {
                const auto &item = t->clips[n];
                clips_->addItem(
                    tr("Clip %1 · %2 frames")
                        .arg(QLocale().toString(n + 1), QLocale().toString(item.lengthFrames)),
                    text(item.id.str()));
                if (c && item.id == c->id)
                    clips_->setCurrentIndex(clips_->count() - 1);
            }
    }
    if (model_)
        for (const auto &item : model_->tracks) {
            tracks_->addItem(text(item.name));
            tracks_->item(tracks_->count() - 1)->setData(Qt::UserRole, text(item.id.str()));
            if (t && item.id == t->id)
                tracks_->setCurrentRow(tracks_->count() - 1);
            if (t && item.layout == t->layout)
                destination_->addItem(text(item.name), text(item.id.str()));
        }
    if (model_ && t)
        for (const auto &a : model_->assets)
            if (a.layout == t->layout && a.sampleRate == model_->sampleRate)
                asset_->addItem(text(a.relativePath), text(a.id.str()));
    if (const auto index = destination_->findData(previousDestination); index >= 0)
        destination_->setCurrentIndex(index);
    if (const auto index = asset_->findData(previousAsset); index >= 0)
        asset_->setCurrentIndex(index);
    if (force || !name_->hasFocus())
        name_->setText(t ? text(t->name) : QString());
    name_->setEnabled(editable_ && t);
    for (auto *w : {start_, source_, length_, split_})
        w->setEnabled(editable_ && c);
    if (c) {
        field(start_, c->startFrame, force);
        field(source_, c->sourceFrame, force);
        field(length_, c->lengthFrames, force);
        field(split_, c->startFrame + c->lengthFrames / 2, force);
    } else
        for (auto *w : {start_, source_, length_, split_})
            if (force || !w->hasFocus())
                w->clear();
    for (auto *b : mutations_) {
        const auto n = b->objectName();
        bool usable = bool(t);
        if (n == "addAudioTrack")
            usable = bool(model_);
        else if (n == "insertMediaClip")
            usable = t && asset_->count();
        else if (n.contains("Clip") || n == "applyClipRange")
            usable = bool(c);
        b->setEnabled(editable_ && usable);
    }
    status_->setText(
        editable_ ? tr("Select a clip to edit its exact frame range. Raw media is preserved.")
                  : tr("Stop audio preparation or recording to edit tracks and clips."));
    if (redraw)
        draw();
}
void TimelineEditor::draw() {
    QScopedValueRollback<bool> guard(refreshing_, true);
    QSignalBlocker block(scene_);
    scene_->clear();
    if (!model_)
        return;
    Frame extent = std::max<Frame>(1, model_->sampleRate);
    for (const auto &t : model_->tracks)
        for (const auto &c : t.clips)
            extent = std::max(extent, c.startFrame + c.lengthFrames);
    const double scale = 800. / double(extent) * std::exp2((zoom_->value() - 50) / 10.);
    for (unsigned tick = 0; tick <= 8; ++tick) {
        const double f = double(extent) * tick / 8.;
        auto *label =
            scene_->addText(QLocale().toString(f / model_->sampleRate, 'f', 2) + tr(" s"));
        label->setPos(70 + f * scale, 0);
    }
    for (std::size_t row = 0; row < model_->tracks.size(); ++row) {
        const auto &t = model_->tracks[row];
        const double y = 35 + double(row) * 56;
        auto *label = scene_->addText(text(t.name));
        label->setPos(0, y);
        label->setTextWidth(65);
        scene_->addLine(70, y + 44, 70 + double(extent) * scale, y + 44,
                        QPen(palette().mid().color()));
        for (const auto &c : t.clips) {
            auto *rect =
                scene_->addRect(70 + double(c.startFrame) * scale, y,
                                std::max(.002, double(c.lengthFrames) * scale), 38, QPen(Qt::black),
                                QBrush(QColor::fromHsv(int(row * 47) % 360, 150, 195)));
            rect->setFlag(QGraphicsItem::ItemIsSelectable);
            rect->setData(0, text(t.id.str()));
            rect->setData(1, text(c.id.str()));
            rect->setToolTip(tr("Start %1 · source %2 · length %3 frames")
                                 .arg(QLocale().toString(c.startFrame),
                                      QLocale().toString(c.sourceFrame),
                                      QLocale().toString(c.lengthFrames)));
            if (track_ == std::optional<Id>(t.id) && clip_ == std::optional<Id>(c.id))
                rect->setSelected(true);
        }
    }
    scene_->setSceneRect(0, 0, 90 + double(extent) * scale,
                         std::max(180., 35 + double(model_->tracks.size()) * 56));
}
} // namespace soundcurrent::daw::ui
