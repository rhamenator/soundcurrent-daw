// SPDX-License-Identifier: GPL-3.0-only
#include "timeline_editor.hpp"
#include "session_list_model.hpp"
#include "timeline_view.hpp"
#include "accelerating_spinbox.hpp"
#include <soundcurrent/clip_timing.hpp>
#include <QCheckBox>
#include <QComboBox>
#include <QFormLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListView>
#include <QItemSelectionModel>
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
class FocusDoubleSpin : public widgets::AcceleratingDoubleSpinBox {
    void wheelEvent(QWheelEvent *e) override {
        if (hasFocus()) widgets::AcceleratingDoubleSpinBox::wheelEvent(e);
        else e->ignore();
    }
};
void numericField(QDoubleSpinBox *w, double value, bool force) {
    if (force || !w->hasFocus()) {
        w->setValue(value);
        w->setProperty("canonicalValue",value);
        w->setProperty("displayValue",w->value());
    }
}
double numericValue(QDoubleSpinBox *w) {
    return w->property("canonicalValue").isValid() &&
           w->value() == w->property("displayValue").toDouble() ?
        w->property("canonicalValue").toDouble() : w->value();
}
QString text(const std::string &s) {
    return QString::fromUtf8(s);
}
void field(QLineEdit *w, Frame f, bool force) {
    if (force || !w->hasFocus())
        w->setText(QString::number(f));
}
} // namespace
TimelineEditor::TimelineEditor(QWidget *parent, ResourceLedger memory)
    : QGroupBox(tr("Tracks and timeline"), parent) {
    setObjectName("timelineEditor");
    auto *body = new QVBoxLayout(this);
    auto *splitter = new QSplitter(this);
    tracks_ = new QListView;
    trackList_ = new SessionListModel(SessionListModel::Kind::Tracks, this, false, memory);
    tracks_->setModel(trackList_);
    tracks_->setUniformItemSizes(true);
    tracks_->setLayoutMode(QListView::Batched);
    tracks_->setBatchSize(128);
    tracks_->setObjectName("timelineTracks");
    tracks_->setAccessibleName(tr("Audio tracks"));
    tracks_->setMinimumWidth(140);
    view_ = new TimelineView(this, memory);
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
    button(trackRow, QT_TRANSLATE_NOOP("TimelineEditor", "Add track"), "addAudioTrack", [this] {
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
    button(trackRow, QT_TRANSLATE_NOOP("TimelineEditor", "Rename"), "renameAudioTrack", [this] {
        if (track_)
            mutate({RenameTrack{*track_, name_->text().toUtf8().toStdString()}});
    });
    button(trackRow, QT_TRANSLATE_NOOP("TimelineEditor", "Remove track"), "removeAudioTrack", [this] {
        if (track_)
            mutate({RemoveTrack{*track_}});
    });
    button(trackRow, QT_TRANSLATE_NOOP("TimelineEditor", "Up"), "moveAudioTrackUp", [this] {
        if (!track())
            return;
        const auto it = std::find_if(model_->tracks.begin(), model_->tracks.end(),
                                     [&](const auto &t) { return t.id == *track_; });
        if (it != model_->tracks.begin())
            mutate({MoveTrack{*track_, (it - 1)->id}});
    });
    button(trackRow, QT_TRANSLATE_NOOP("TimelineEditor", "Down"), "moveAudioTrackDown", [this] {
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
    clipList_ = new SessionListModel(SessionListModel::Kind::Clips, this, false, memory);
    clips_->setModel(clipList_);
    clips_->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
    clips_->setMinimumContentsLength(16);
    clips_->setObjectName("timelineClips");
    clips_->setAccessibleName(tr("Clips in selected track"));
    ranges->addRow(tr("Selected clip"), clips_);
    connect(clips_, &QComboBox::currentIndexChanged, this, [this] {
        if (refreshing_)
            return;
        const auto id = clips_->currentData().toString();
        select(track_,
               id.isEmpty() ? std::optional<Id>{} : std::optional<Id>(Id(id.toStdString())));
        if (track_ && clip_)
            view_->ensureClipVisible(*track_, *clip_);
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
    start_ = frameField("clipStartFrame", QT_TRANSLATE_NOOP("TimelineEditor", "Timeline start frame"));
    source_ = frameField("clipSourceFrame", QT_TRANSLATE_NOOP("TimelineEditor", "Source start frame"));
    source_->setToolTip(tr("Integer source frame; the exact fractional part is preserved. Use project crop for edits between source frames."));
    length_ = frameField("clipLengthFrames", QT_TRANSLATE_NOOP("TimelineEditor", "Length in project frames"));
    split_ = frameField("clipSplitFrame", QT_TRANSLATE_NOOP("TimelineEditor", "Split at timeline frame"));
    consumed_ = frameField("clipConsumedProjectFrames", QT_TRANSLATE_NOOP("TimelineEditor", "Crop offset in project frames"));
    consumed_->setToolTip(tr("Positive values remove the beginning; negative values reveal earlier source audio. Timeline start and length are project frames."));
    sourceTiming_ = new QLabel;
    sourceTiming_->setObjectName("clipSourceTiming");
    sourceTiming_->setWordWrap(true);
    sourceTiming_->setTextInteractionFlags(Qt::TextSelectableByMouse);
    ranges->addRow(tr("Exact source position"), sourceTiming_);
    body->addLayout(ranges);
    auto *clipRow = new QHBoxLayout;
    button(clipRow, QT_TRANSLATE_NOOP("TimelineEditor", "Apply range"), "applyClipRange", [this] {
        if (clip_ && track_)
            mutate({SetClipRange{*track_, *clip_, frame(start_), frame(source_), frame(length_)}});
    });
    button(clipRow, QT_TRANSLATE_NOOP("TimelineEditor", "Apply project crop"), "cropAudioClip", [this] {
        if (clip_ && track_)
            mutate({CropClip{*track_, *clip_, frame(start_), frame(consumed_,true), frame(length_)}});
    });
    button(clipRow, QT_TRANSLATE_NOOP("TimelineEditor", "Split"), "splitAudioClip", [this] {
        if (clip_ && track_)
            mutate({SplitClip{*track_, *clip_, Id::generate(), frame(split_)}});
    });
    button(clipRow, QT_TRANSLATE_NOOP("TimelineEditor", "Duplicate"), "duplicateAudioClip", [this] {
        if (const auto *c = clip()) {
            if (c->startFrame > std::numeric_limits<Frame>::max() - c->lengthFrames)
                throw ProjectError(ErrorCode::InvalidState, "Clip position overflow");
            auto copy = *c;
            copy.id = Id::generate();
            copy.startFrame += copy.lengthFrames;
            mutate({InsertClip{*track_, std::move(copy), {}}});
        }
    });
    button(clipRow, QT_TRANSLATE_NOOP("TimelineEditor", "Remove clip"), "removeAudioClip", [this] {
        if (clip_ && track_)
            mutate({RemoveClip{*track_, *clip_}});
    });
    destination_ = new FocusCombo;
    destinations_ = new SessionListModel(SessionListModel::Kind::Tracks, this, false, memory);
    destination_->setModel(destinations_);
    destination_->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
    destination_->setMinimumContentsLength(16);
    destination_->setObjectName("clipDestinationTrack");
    destination_->setAccessibleName(tr("Clip destination track"));
    clipRow->addWidget(destination_, 1);
    button(clipRow, QT_TRANSLATE_NOOP("TimelineEditor", "Move to track"), "moveClipToTrack", [this] {
        if (clip_ && track_ && destination_->currentIndex() >= 0)
            mutate({MoveClip{*track_,
                             Id(destination_->currentData().toString().toStdString()),
                             *clip_,
                             frame(start_),
                             {}}});
    });
    body->addLayout(clipRow);
    auto *processingToggle = new QPushButton(tr("Clip gain and fades"));
    processingToggle->setObjectName("clipProcessingToggle");
    processingToggle->setCheckable(true);
    body->addWidget(processingToggle);
    auto *processingBody = new QWidget;
    processingBody->setObjectName("clipProcessingFields");
    auto *processingForm = new QFormLayout(processingBody);
    clipGain_ = new FocusDoubleSpin;
    clipGain_->setObjectName("clipGainDb");
    clipGain_->setRange(-120,60);clipGain_->setDecimals(6);clipGain_->setSingleStep(.1);
    clipGain_->setAccessibleName(tr("Clip gain in decibels"));
    auto *gainRow = new QHBoxLayout;
    gainRow->addWidget(clipGain_);
    clipMuted_ = new QCheckBox(tr("Mute clip"));clipMuted_->setObjectName("clipMuted");
    clipInverted_ = new QCheckBox(tr("Invert clip polarity"));clipInverted_->setObjectName("clipPolarityInverted");
    gainRow->addWidget(clipMuted_);gainRow->addWidget(clipInverted_);
    processingForm->addRow(tr("Clip gain (dB)"),gainRow);
    const auto fadeFields = [&](bool out) {
        auto *row = new QHBoxLayout;
        auto *start = new QLineEdit;auto *end = new QLineEdit;
        start->setObjectName(out ? "clipFadeOutStart" : "clipFadeInStart");
        end->setObjectName(out ? "clipFadeOutEnd" : "clipFadeInEnd");
        start->setAccessibleName(out ? tr("Fade out start frame") : tr("Fade in start frame"));
        end->setAccessibleName(out ? tr("Fade out end frame") : tr("Fade in end frame"));
        start->setLayoutDirection(Qt::LeftToRight);end->setLayoutDirection(Qt::LeftToRight);
        auto *curve = new FocusCombo;curve->setObjectName(out ? "clipFadeOutCurve" : "clipFadeInCurve");
        curve->setAccessibleName(out ? tr("Fade out curve") : tr("Fade in curve"));
        curve->addItem(tr("Linear"),int(ClipFadeCurve::Linear));
        curve->addItem(tr("Equal power"),int(ClipFadeCurve::EqualPower));
        curve->addItem(tr("Smoothstep"),int(ClipFadeCurve::Smoothstep));
        auto *shape = new FocusDoubleSpin;shape->setObjectName(out ? "clipFadeOutShape" : "clipFadeInShape");
        shape->setRange(.25,4);shape->setDecimals(6);shape->setSingleStep(.05);
        shape->setAccessibleName(out ? tr("Fade out shape") : tr("Fade in shape"));
        row->addWidget(new QLabel(tr("Start")));row->addWidget(start);
        row->addWidget(new QLabel(tr("End")));row->addWidget(end);
        row->addWidget(curve);row->addWidget(shape);
        processingForm->addRow(out ? tr("Fade out") : tr("Fade in"),row);
        if (out) {fadeOutStart_=start;fadeOutEnd_=end;fadeOutCurve_=curve;fadeOutShape_=shape;}
        else {fadeInStart_=start;fadeInEnd_=end;fadeInCurve_=curve;fadeInShape_=shape;}
    };
    fadeFields(false);fadeFields(true);
    auto *explanation = new QLabel(tr("Fade windows use clip-relative frames: start is included, end is excluded. Set both to 0 to disable. Signed positions preserve fades after trimming or splitting; raw media stays unchanged."));
    explanation->setWordWrap(true);explanation->setTextFormat(Qt::PlainText);
    processingForm->addRow(explanation);
    auto *applyProcessing = new QHBoxLayout;
    button(applyProcessing,QT_TRANSLATE_NOOP("TimelineEditor", "Apply clip processing"),"applyClipProcessing",[this] {
        if (!clip_ || !track_) return;
        ClipProcessing p;
        p.gainDb=numericValue(clipGain_);p.muted=clipMuted_->isChecked();p.polarityInverted=clipInverted_->isChecked();
        p.fadeIn={frame(fadeInStart_,true),frame(fadeInEnd_,true),ClipFadeCurve(fadeInCurve_->currentData().toInt()),numericValue(fadeInShape_)};
        p.fadeOut={frame(fadeOutStart_,true),frame(fadeOutEnd_,true),ClipFadeCurve(fadeOutCurve_->currentData().toInt()),numericValue(fadeOutShape_)};
        mutate({SetClipProcessing{*track_,*clip_,p}});
    });
    processingForm->addRow(applyProcessing);
    body->addWidget(processingBody);processingBody->hide();
    connect(processingToggle,&QPushButton::toggled,processingBody,&QWidget::setVisible);
    auto *assetRow = new QHBoxLayout;
    asset_ = new FocusCombo;
    assets_ = new SessionListModel(SessionListModel::Kind::Assets, this, false, memory);
    asset_->setModel(assets_);
    asset_->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
    asset_->setMinimumContentsLength(16);
    asset_->setObjectName("timelineAsset");
    asset_->setAccessibleName(tr("Recorded media asset"));
    assetRow->addWidget(asset_, 1);
    button(assetRow, QT_TRANSLATE_NOOP("TimelineEditor", "Insert media clip"), "insertMediaClip", [this] {
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
        c.lengthFrames = SourceFrameMap(it->sampleRate,model_->sampleRate).projectFramesForSource(it->frames);
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
    connect(tracks_->selectionModel(), &QItemSelectionModel::currentChanged, this,
            [this](const QModelIndex &index, const QModelIndex &) {
                if (refreshing_)
                    return;
                const auto id = trackList_->idAt(index.row());
                if (id)
                    QTimer::singleShot(0, this, [this, id] { selectTrack(*id); });
            });
    view_->selection = [this](std::optional<Id> track, std::optional<Id> clip) {
        if (refreshing_)
            return;
        select(track ? track : track_, clip);
    };
    refresh(true);
}
TimelineEditor::~TimelineEditor() {
    // Child controls can emit selection changes during QWidget child teardown,
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
Frame TimelineEditor::frame(QLineEdit *w, bool allowSigned) const {
    const auto bytes = w->text().toLatin1();
    Frame n = 0;
    const auto [end, error] =
        std::from_chars(bytes.constData(), bytes.constData() + bytes.size(), n);
    if (error != std::errc{} || end != bytes.constData() + bytes.size() || (!allowSigned && n < 0))
        throw ProjectError(ErrorCode::InvalidParameter,
                           allowSigned ? "Frame must be a signed decimal integer" :
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
struct TimelineEditor::Prepared {
    std::shared_ptr<const Session> model;
    std::uint64_t epoch = 0;
    bool editable = false, force = false;
    std::optional<Id> track, clip, pending;
    std::shared_ptr<SessionListModel::Prepared> tracks, clips, destinations, assets;
    std::shared_ptr<TimelineView::Prepared> view;
};
std::shared_ptr<TimelineEditor::Prepared>
TimelineEditor::prepare(std::shared_ptr<const Session> s, std::uint64_t epoch, bool editable,
                        std::optional<Id> selected, std::optional<Id> clip, bool specific) const {
    auto p = std::make_shared<Prepared>();
    p->model = std::move(s);
    p->epoch = epoch;
    p->editable = editable;
    p->force = epoch != epoch_;
    p->track = specific ? selected : p->force ? std::optional<Id>{} : track_;
    p->clip = specific ? clip : p->force ? std::optional<Id>{} : clip_;
    p->pending = p->force || specific ? std::optional<Id>{} : pendingTrack_;
    const Track *t = nullptr;
    if (p->model) {
        if (p->pending)
            for (const auto &candidate : p->model->tracks)
                if (candidate.id == *p->pending) {
                    p->track = candidate.id;
                    p->clip.reset();
                    p->pending.reset();
                    break;
                }
        if (p->track)
            for (const auto &candidate : p->model->tracks)
                if (candidate.id == *p->track)
                    t = &candidate;
        if (!t && !p->model->tracks.empty()) {
            t = &p->model->tracks.front();
            p->track = t->id;
            p->clip.reset();
        }
    }
    if (!t) {
        p->track.reset();
        p->clip.reset();
    }
    if (t && p->clip && std::none_of(t->clips.begin(), t->clips.end(), [&](const auto &c) {
            return c.id == *p->clip;
        }))
        p->clip.reset();
    p->tracks = trackList_->prepare(p->model);
    p->clips = clipList_->prepare(p->model, {}, p->track);
    p->destinations = destinations_->prepare(
        t ? p->model : nullptr, t ? std::optional<ChannelLayout>(t->layout) : std::nullopt);
    p->assets = assets_->prepare(t ? p->model : nullptr,
                                 t ? std::optional<ChannelLayout>(t->layout) : std::nullopt);
    p->view = view_->prepare(p->model, epoch);
    return p;
}
std::shared_ptr<TimelineEditor::Prepared>
TimelineEditor::prepareModel(std::shared_ptr<const Session> s, std::uint64_t epoch,
                             bool editable) const {
    return prepare(std::move(s), epoch, editable, {}, {}, false);
}
std::optional<Id> TimelineEditor::preparedTrack(const std::shared_ptr<Prepared> &p) const {
    return p ? p->track : track_;
}
void TimelineEditor::commitModel(std::shared_ptr<Prepared> p) {
    if (!p)
        return;
    const auto previous = track_;
    const auto *previousClip = clip();
    const auto previousProcessing = previousClip ? previousClip->processing : ClipProcessing{};
    const auto previousDestination = destination_->currentData().toString();
    const auto previousAsset = asset_->currentData().toString();
    const bool changed = model_ != p->model || epoch_ != p->epoch || track_ != p->track ||
                         clip_ != p->clip || editable_ != p->editable;
    {
        QScopedValueRollback<bool> guard(refreshing_, true);
        QSignalBlocker a(tracks_), b(clips_), c(destination_), d(asset_);
        trackList_->commit(std::move(p->tracks));
        clipList_->commit(std::move(p->clips));
        destinations_->commit(std::move(p->destinations));
        assets_->commit(std::move(p->assets));
        view_->commit(std::move(p->view));
        model_ = std::move(p->model);
        epoch_ = p->epoch;
        editable_ = p->editable;
        track_ = std::move(p->track);
        clip_ = std::move(p->clip);
        pendingTrack_ = std::move(p->pending);
    }
    if (changed) {
        const auto *currentClip = clip();
        const auto currentProcessing = currentClip ? currentClip->processing : ClipProcessing{};
        // Stored changes (including Undo/Redo) must update these controls even
        // while focused. Unrelated state publication preserves in-progress input.
        refresh(p->force, true, previousDestination, previousAsset,
                previousProcessing != currentProcessing);
    }
    if (previous != track_ && selectionChanged)
        selectionChanged();
}
void TimelineEditor::updateModel(std::shared_ptr<const Session> s, std::uint64_t epoch,
                                 bool editable) {
    commitModel(prepareModel(std::move(s), epoch, editable));
}
void TimelineEditor::editing(bool editable) {
    if (editable_ != editable) {
        editable_ = editable;
        refresh(false, false);
    }
}
std::size_t TimelineEditor::resourceBytes() const {
    return trackList_->resourceBytes() + destinations_->resourceBytes() + assets_->resourceBytes() +
           clipList_->resourceBytes() + view_->resourceBytes();
}
bool TimelineEditor::select(std::optional<Id> id, std::optional<Id> clip) {
    if (!model_ || !id ||
        std::none_of(model_->tracks.begin(), model_->tracks.end(),
                     [&](const auto &t) { return t.id == *id; }))
        return false;
    if (track_ == id && clip_ == clip)
        return true;
    if (auto *focused = window()->focusWidget())
        focused->clearFocus();
    try {
        auto p = prepare(model_, epoch_, editable_, id, clip, true);
        if (selectionAdmission && !selectionAdmission(model_, p->track)) {
            refresh(false, false);
            return false;
        }
        p->force = true;
        commitModel(std::move(p));
        view_->ensureTrackVisible(*id);
        return true;
    } catch (const std::exception &e) {
        refresh(false, false);
        status_->setText(tr("View could not be updated: %1").arg(text(e.what())));
        return false;
    }
}
bool TimelineEditor::selectTrack(const Id &id) {
    return select(id, track_ == std::optional<Id>(id) ? clip_ : std::optional<Id>{});
}
void TimelineEditor::refresh(bool force, bool redraw, std::optional<QString> destination,
                             std::optional<QString> asset, bool forceProcessing) {
    QScopedValueRollback<bool> guard(refreshing_, true);
    QSignalBlocker block(tracks_);
    const auto previousDestination = destination.value_or(destination_->currentData().toString());
    const auto previousAsset = asset.value_or(asset_->currentData().toString());
    const auto *t = track();
    const auto *c = clip();
    trackList_->update(model_);
    if (t)
        tracks_->setCurrentIndex(trackList_->index(trackList_->rowForId(t->id)));
    else
        tracks_->setCurrentIndex({});
    {
        QSignalBlocker blocked(clips_);
        clipList_->update(model_, {}, track_);
        clips_->setCurrentIndex(c ? clipList_->rowForId(c->id) : 0);
    }
    {
        QSignalBlocker blocked(destination_);
        destinations_->update(t ? model_ : nullptr,
                              t ? std::optional<ChannelLayout>(t->layout) : std::nullopt);
        const auto id = previousDestination;
        const auto row = id.isEmpty() ? -1 : destinations_->rowForId(Id(id.toStdString()));
        destination_->setCurrentIndex(row >= 0 ? row : (destinations_->rowCount() ? 0 : -1));
    }
    {
        QSignalBlocker blocked(asset_);
        assets_->update(t ? model_ : nullptr,
                        t ? std::optional<ChannelLayout>(t->layout) : std::nullopt);
        const auto id = previousAsset;
        const auto row = id.isEmpty() ? -1 : assets_->rowForId(Id(id.toStdString()));
        asset_->setCurrentIndex(row >= 0 ? row : (assets_->rowCount() ? 0 : -1));
    }
    if (force || !name_->hasFocus())
        name_->setText(t ? text(t->name) : QString());
    name_->setEnabled(editable_ && t);
    for (auto *w : {fadeInStart_,fadeInEnd_,fadeOutStart_,fadeOutEnd_}) w->setEnabled(editable_ && c);
    for (auto *w : {clipGain_,fadeInShape_,fadeOutShape_}) w->setEnabled(editable_ && c);
    for (auto *w : {clipMuted_,clipInverted_}) w->setEnabled(editable_ && c);
    for (auto *w : {fadeInCurve_,fadeOutCurve_}) w->setEnabled(editable_ && c);
    const auto p = c ? c->processing : ClipProcessing{};
    forceProcessing = forceProcessing || force;
    numericField(clipGain_,p.gainDb,forceProcessing);numericField(fadeInShape_,p.fadeIn.shape,forceProcessing);numericField(fadeOutShape_,p.fadeOut.shape,forceProcessing);
    if (forceProcessing || !clipMuted_->hasFocus()) clipMuted_->setChecked(p.muted);
    if (forceProcessing || !clipInverted_->hasFocus()) clipInverted_->setChecked(p.polarityInverted);
    field(fadeInStart_,p.fadeIn.startFrame,forceProcessing);field(fadeInEnd_,p.fadeIn.endFrame,forceProcessing);
    field(fadeOutStart_,p.fadeOut.startFrame,forceProcessing);field(fadeOutEnd_,p.fadeOut.endFrame,forceProcessing);
    if (forceProcessing || !fadeInCurve_->hasFocus()) fadeInCurve_->setCurrentIndex(fadeInCurve_->findData(int(p.fadeIn.curve)));
    if (forceProcessing || !fadeOutCurve_->hasFocus()) fadeOutCurve_->setCurrentIndex(fadeOutCurve_->findData(int(p.fadeOut.curve)));
    for (auto *w : {start_, source_, length_, split_, consumed_})
        w->setEnabled(editable_ && c);
    field(consumed_,0,force);
    if (c) {
        field(start_, c->startFrame, force);
        field(source_, c->sourceFrame, force);
        field(length_, c->lengthFrames, force);
        field(split_, c->startFrame + c->lengthFrames / 2, force);
        const auto asset=std::find_if(model_->assets.begin(),model_->assets.end(),
                                     [&](const auto &a){return a.id==c->assetId;});
        sourceTiming_->setText(tr("%1 + %2/%3 source frames · %4 Hz → %5 Hz project")
            .arg(QLocale().toString(c->sourceFrame),
                 QLocale().toString(qulonglong(c->sourceTiming.fraction)),
                 QLocale().toString(qulonglong(c->sourceTiming.denominator)),
                 QLocale().toString(asset->sampleRate),QLocale().toString(model_->sampleRate)));
    } else
        for (auto *w : {start_, source_, length_, split_, consumed_})
            if (force || !w->hasFocus())
                w->clear();
    if(!c) sourceTiming_->clear();
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
    view_->setSnapshot(model_, epoch_);
    view_->setZoom(zoom_->value());
    view_->setSelection(track_, clip_);
}
} // namespace soundcurrent::daw::ui
