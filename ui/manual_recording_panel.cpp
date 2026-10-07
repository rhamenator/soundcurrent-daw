// SPDX-License-Identifier: GPL-3.0-only
#include "manual_recording_panel.hpp"
#include <soundcurrent/routing.hpp>
#include <QComboBox>
#include <QGridLayout>
#include <QLabel>
#include <QListWidget>
#include <QLocale>
#include <QMessageBox>
#include <QPushButton>
#include <QSignalBlocker>
#include <QSpinBox>
#include <QVBoxLayout>
#include <QWheelEvent>
#include <algorithm>
#include <limits>
namespace soundcurrent::daw::ui {
namespace {
QString text(std::string_view v) {
    return QString::fromUtf8(v.data(), static_cast<qsizetype>(v.size()));
}
const Track *track(const Session &s, const Id &id) {
    const auto it =
        std::find_if(s.tracks.begin(), s.tracks.end(), [&](const auto &t) { return t.id == id; });
    return it == s.tracks.end() ? nullptr : &*it;
}
ChannelPortIntent intent(const PipeWirePort &p) {
    return {p.nodeName, p.portName, p.mediaClass, p.input};
}
class RouteCombo final : public QComboBox {
  public:
    using QComboBox::QComboBox;
    void wheelEvent(QWheelEvent *e) override {
        hasFocus() ? QComboBox::wheelEvent(e) : e->ignore();
    }
};
class IntegerControl final : public QSpinBox {
  public:
    using QSpinBox::QSpinBox;
    void wheelEvent(QWheelEvent *e) override {
        hasFocus() ? QSpinBox::wheelEvent(e) : e->ignore();
    }
};
bool transport(const ManualControlSnapshot &s) {
    return s.phase == ManualControlPhase::Preparing || s.phase == ManualControlPhase::Ready ||
           s.phase == ManualControlPhase::Playing || s.phase == ManualControlPhase::Finalizing;
}
} // namespace
ManualRecordingPanel::ManualRecordingPanel(ProjectController &p, std::uint64_t &sequence,
                                           std::function<bool()> mayPrepare,
                                           ManualControlOptions options, QWidget *parent)
    : QGroupBox(tr("Manual recording and repeated takes"), parent), project_(p),
      barrierSequence_(sequence), mayPrepare_(std::move(mayPrepare)), worker_(std::move(options)) {
    setObjectName("manualRecordingPanel");
    auto *layout = new QVBoxLayout(this);
    auto *buttons = new QGridLayout;
    auto button = [&](const QString &label, const char *name, int row, int col) {
        auto *b = new QPushButton(label, this);
        b->setObjectName(name);
        buttons->addWidget(b, row, col);
        return b;
    };
    prepare_ = button(tr("Prepare transport"), "manualPrepare", 0, 0);
    play_ = button(tr("Play / monitor"), "manualPlay", 0, 1);
    prepareTake_ = button(tr("Prepare next take"), "manualPrepareTake", 0, 2);
    punchIn_ = button(tr("Punch In"), "manualPunchIn", 1, 0);
    punchOut_ = button(tr("Punch Out"), "manualPunchOut", 1, 1);
    stop_ = button(tr("Stop"), "manualStop", 1, 2);
    cancel_ = button(tr("Cancel recording"), "manualCancel", 2, 2);
    layout->addLayout(buttons);
    auto *settings = new QGridLayout;
    seconds_ = new IntegerControl(this);
    seconds_->setObjectName("manualSeconds");
    seconds_->setRange(1, 86400);
    seconds_->setValue(600);
    seconds_->setSuffix(tr(" s"));
    seconds_->setAccessibleName(tr("Maximum transport duration"));
    reserve_ = new IntegerControl(this);
    reserve_->setObjectName("manualReserve");
    reserve_->setRange(2, 20);
    reserve_->setValue(10);
    reserve_->setSuffix(tr(" s"));
    reserve_->setAccessibleName(tr("Disk stall reserve"));
    settings->addWidget(new QLabel(tr("Maximum transport duration"), this), 0, 0);
    settings->addWidget(seconds_, 0, 1);
    settings->addWidget(new QLabel(tr("Disk stall reserve"), this), 1, 0);
    settings->addWidget(reserve_, 1, 1);
    layout->addLayout(settings);
    arms_ = new QListWidget(this);
    arms_->setObjectName("manualArms");
    arms_->setAccessibleName(tr("Tracks armed for manual recording"));
    arms_->setMaximumHeight(120);
    layout->addWidget(arms_);
    routes_ = new QGridLayout;
    layout->addLayout(routes_);
    status_ = new QLabel(this);
    status_->setObjectName("manualStatus");
    receipt_ = new QLabel(this);
    receipt_->setObjectName("manualReceipt");
    for (auto *label : {status_, receipt_}) {
        label->setWordWrap(true);
        label->setTextFormat(Qt::PlainText);
        layout->addWidget(label);
    }
    groups_ = new RouteCombo(this);
    groups_->setObjectName("manualGroups");
    groups_->setAccessibleName(tr("Recorded take previews"));
    layout->addWidget(groups_);
    preview_ = new QLabel(this);
    preview_->setObjectName("manualPreview");
    preview_->setWordWrap(true);
    preview_->setTextFormat(Qt::PlainText);
    layout->addWidget(preview_);
    auto *actions = new QHBoxLayout;
    add_ = new QPushButton(tr("Add complete take"), this);
    addPartial_ = new QPushButton(tr("Add verified complete lanes"), this);
    keep_ = new QPushButton(tr("Keep files for recovery"), this);
    add_->setObjectName("manualAdd");
    addPartial_->setObjectName("manualAddPartial");
    keep_->setObjectName("manualKeep");
    for (auto *b : {add_, addPartial_, keep_})
        actions->addWidget(b);
    layout->addLayout(actions);
    connect(prepare_, &QPushButton::clicked, this, [this] { prepare(); });
    connect(play_, &QPushButton::clicked, this, [this] { activate(); });
    connect(prepareTake_, &QPushButton::clicked, this, [this] {
        if (pending_ || nextTake_ || punchPending_ || closingIntent_)
            return;
        ManualControlCommand c;
        c.kind = ManualControlKind::PrepareTake;
        c.generation = snapshot()->generation;
        send(std::move(c));
    });
    connect(punchIn_, &QPushButton::clicked, this, [this] { punch(ManualPunchAction::In); });
    connect(punchOut_, &QPushButton::clicked, this, [this] { punch(ManualPunchAction::Out); });
    connect(stop_, &QPushButton::clicked, this, [this] { stop(); });
    connect(cancel_, &QPushButton::clicked, this, [this] { stop(true); });
    connect(add_, &QPushButton::clicked, this, [this] { adopt(false); });
    connect(addPartial_, &QPushButton::clicked, this, [this] { adopt(true); });
    connect(keep_, &QPushButton::clicked, this, [this] { keep(); });
}
std::shared_ptr<const ManualControlSnapshot> ManualRecordingPanel::snapshot() const {
    return worker_.snapshot();
}
bool ManualRecordingPanel::busy() const {
    const auto s = snapshot();
    return barrier_ || pending_ || attaching_ || transport(*s) || !s->groups.empty();
}
bool ManualRecordingPanel::send(ManualControlCommand c) {
    if (pending_ || closingIntent_)
        return false;
    const auto r = worker_.submit(std::move(c));
    if (r.admission != ManualControlAdmission::Accepted) {
        receipt_->setText(tr("Recording command could not be queued. Please retry."));
        return false;
    }
    pending_ = r.sequence;
    // Disable dependent gestures immediately, before the next timer snapshot.
    // Priority Stop/Cancel remain usable during endpoint construction or IO.
    for (auto *b : {prepare_, play_, prepareTake_, punchIn_, punchOut_})
        b->setEnabled(false);
    for (const auto &combos : {inputs_, outputs_})
        for (auto *c : combos)
            c->setEnabled(false);
    arms_->setEnabled(false);
    seconds_->setEnabled(false);
    reserve_->setEnabled(false);
    stop_->setEnabled(true);
    cancel_->setEnabled(true);
    return true;
}
void ManualRecordingPanel::prepare() {
    const auto p = project_.snapshot();
    if (busy() || closingIntent_ || !mayPrepare_() || !p->session || p->session->tracks.empty() ||
        p->io != IoOperation::None || !snapshot()->supported)
        return;
    barrierArms_.clear();
    for (int n = 0; n < arms_->count(); ++n)
        if (arms_->item(n)->checkState() == Qt::Checked)
            barrierArms_.emplace_back(arms_->item(n)->data(Qt::UserRole).toString().toStdString());
    if (barrierArms_.empty()) {
        receipt_->setText(tr("Arm at least one track before preparing."));
        return;
    }
    ProjectCommand b{CommandKind::Barrier};
    b.barrier = barrierSequence_++;
    if (project_.submit(b) == Admission::Accepted) {
        barrier_ = b.barrier;
        barrierSeconds_ = seconds_->value();
        barrierReserve_ = reserve_->value();
        prepare_->setEnabled(false);
        arms_->setEnabled(false);
        seconds_->setEnabled(false);
        reserve_->setEnabled(false);
        stop_->setEnabled(true);
    }
}
void ManualRecordingPanel::activate() {
    const auto s = snapshot();
    if (s->phase != ManualControlPhase::Ready || pending_ || closingIntent_ || !prepared_)
        return;
    ManualControlCommand c;
    c.kind = ManualControlKind::Activate;
    c.generation = s->generation;
    c.inputs = selectedPorts(false);
    c.outputs = selectedPorts(true);
    if (c.inputs.size() != prepared_->options.run.nativeInputs ||
        c.outputs.size() != prepared_->plan.output.channels) {
        receipt_->setText(tr("Select every input and output port before playing."));
        return;
    }
    send(std::move(c));
}
void ManualRecordingPanel::punch(ManualPunchAction action) {
    const auto s = snapshot();
    if (pending_ || punchPending_ || closingIntent_ || s->phase != ManualControlPhase::Playing ||
        (action == ManualPunchAction::In ? !nextTake_ || activeTake_ : !activeTake_))
        return;
    ManualControlCommand c;
    c.kind = ManualControlKind::Punch;
    c.generation = s->generation;
    c.action = action;
    c.take = action == ManualPunchAction::In ? nextTake_ : 0;
    if (send(std::move(c))) {
        punchPending_ = pending_;
        receipt_->setText(tr("Punch queued; waiting for the audio boundary."));
    }
}
void ManualRecordingPanel::stop(bool cancel) {
    barrier_ = 0;
    stopToken_ = worker_.requestStop(cancel);
    for (auto *b : {play_, prepareTake_, punchIn_, punchOut_})
        b->setEnabled(false);
}
void ManualRecordingPanel::beginClose() {
    closingIntent_ = true;
    stop();
}
void ManualRecordingPanel::cancelClose() {
    closingIntent_ = false;
}
void ManualRecordingPanel::requestShutdown() {
    worker_.requestShutdown();
}
int ManualRecordingPanel::closeReadiness() {
    const auto s = snapshot();
    if (s->stopAcknowledged < stopToken_ || transport(*s) || pending_ || attaching_ || prompting_)
        return 0;
    if (s->groups.empty())
        return 1;
    if (std::all_of(s->groups.begin(), s->groups.end(), [&](const auto &g) {
            return consumed_.contains({g.generation, g.group->take});
        }))
        return 0;
    prompting_ = true;
    QMessageBox prompt(QMessageBox::Question, tr("Recorded takes"),
                       tr("Recorded files have not all been added to the project. Keep them for "
                          "later recovery, or return to review the previews?"),
                       QMessageBox::NoButton, this);
    prompt.setObjectName("manualClosePrompt");
    auto *keep = prompt.addButton(tr("Keep for recovery and close"), QMessageBox::AcceptRole);
    prompt.addButton(tr("Review takes"), QMessageBox::RejectRole);
    prompt.exec();
    prompting_ = false;
    if (prompt.clickedButton() != keep) {
        cancelClose();
        return -1;
    }
    for (const auto &g : s->groups)
        if (worker_.acknowledgeGroup(g.generation, g.group->take))
            consumed_.insert({g.generation, g.group->take});
    return 0; // Wait for the immutable worker snapshot to reflect consumption.
}
std::vector<PipeWirePort> ManualRecordingPanel::selectedPorts(bool output) const {
    std::vector<PipeWirePort> result;
    for (auto *c : output ? outputs_ : inputs_) {
        if (c->currentIndex() <= 0 || !c->isEnabled())
            return {};
        const auto index = c->currentData().toULongLong();
        if (index >= inventory_.size())
            return {};
        result.push_back(inventory_[std::size_t(index)]);
    }
    return result;
}
void ManualRecordingPanel::setRoute(bool output, std::size_t channel, QComboBox *combo) {
    const auto p = project_.snapshot();
    if (!prepared_ || !p->session || pending_ || snapshot()->phase != ManualControlPhase::Ready ||
        closingIntent_ || combo->currentIndex() < 0)
        return;
    RouteAddress address{prepared_->session->tracks.front().id, RouteTarget::Output};
    std::size_t laneChannel = channel;
    if (output && prepared_->session->master)
        address = {prepared_->session->master->id, RouteTarget::Master};
    if (!output) {
        for (const auto &arm : prepared_->arms)
            if (channel < arm.binding.inputChannels.size()) {
                address = {arm.binding.track, RouteTarget::Input};
                break;
            } else
                channel -= arm.binding.inputChannels.size();
    }
    if (!output)
        laneChannel = channel;
    std::optional<ChannelPortIntent> port;
    if (combo->currentIndex() > 0) {
        const auto index = combo->currentData().toULongLong();
        if (index >= inventory_.size())
            return;
        port = intent(inventory_[std::size_t(index)]);
    }
    ProjectCommand c{CommandKind::Routing};
    c.routeAddress = address;
    c.routePatch = RouteChannelPatch{static_cast<std::uint32_t>(laneChannel), "pipewire", port};
    if (project_.submit(std::move(c)) != Admission::Accepted)
        receipt_->setText(tr("Route change could not be queued. Please retry."));
    inventoryRevision_ = 0;
}
void ManualRecordingPanel::refreshRoutes(const ControllerSnapshot &p,
                                         const ManualControlSnapshot &s) {
    if (!prepared_ || !p.session)
        return;
    if (inventory_ == s.ports && inventoryRevision_ == p.modelRevision)
        return;
    inventory_ = s.ports;
    inventoryRevision_ = p.modelRevision;
    if (inputs_.size() != prepared_->options.run.nativeInputs ||
        outputs_.size() != prepared_->plan.output.channels) {
        while (auto *item = routes_->takeAt(0)) {
            delete item->widget();
            delete item;
        }
        inputs_.clear();
        outputs_.clear();
        auto make = [&](bool output, std::size_t channel, QString label) {
            auto *combo = new RouteCombo(this);
            combo->setObjectName(
                QStringLiteral("manual%1%2").arg(output ? "Output" : "Input").arg(channel));
            combo->setAccessibleName(label);
            const auto row = routes_->rowCount();
            routes_->addWidget(new QLabel(label, this), row, 0);
            routes_->addWidget(combo, row, 1);
            (output ? outputs_ : inputs_).push_back(combo);
            connect(combo, &QComboBox::activated, this,
                    [this, output, channel, combo] { setRoute(output, channel, combo); });
        };
        std::size_t packed = 0;
        for (const auto &arm : prepared_->arms) {
            const auto *t = track(*p.session, arm.binding.track);
            for (std::size_t n = 0; n < arm.binding.inputChannels.size(); ++n)
                make(false, packed++,
                     tr("%1 input %2")
                         .arg(t ? text(t->name) : tr("Missing track"))
                         .arg(QLocale().toString(static_cast<qulonglong>(n + 1))));
        }
        for (std::size_t n = 0; n < prepared_->plan.output.channels; ++n)
            make(true, n,
                 tr("Master output %1").arg(QLocale().toString(static_cast<qulonglong>(n + 1))));
    }
    auto populate = [&](QComboBox *c, const RouteIntent &route, std::size_t channel, bool output) {
        QSignalBlocker blocked(c);
        c->clear();
        c->addItem(tr("Unassigned"));
        std::vector<ChannelPortIntent> candidates;
        for (std::size_t n = 0; n < inventory_.size(); ++n)
            if (inventory_[n].input == output) {
                candidates.push_back(intent(inventory_[n]));
                c->addItem(text(inventory_[n].nodeName) + QStringLiteral(" / ") +
                               text(inventory_[n].portName),
                           QVariant::fromValue(qulonglong(n)));
            }
        const auto match = matchRouteIntent(route, channel, "pipewire", candidates);
        if (match.status == RouteMatchStatus::Found)
            c->setCurrentIndex(int(*match.index + 1));
        else if (match.status != RouteMatchStatus::Unassigned) {
            c->setItemText(0, tr("Saved port missing or ambiguous — select a port"));
        }
    };
    std::size_t packed = 0;
    for (const auto &arm : prepared_->arms) {
        const auto *t = track(*p.session, arm.binding.track);
        for (std::size_t n = 0; n < arm.binding.inputChannels.size(); ++n)
            populate(inputs_[packed++], t ? t->input : RouteIntent{}, n, false);
    }
    const auto *t = track(*p.session, prepared_->session->tracks.front().id);
    const auto route = p.session->master ? p.session->master->output
                       : t               ? t->output
                                         : RouteIntent{};
    for (std::size_t n = 0; n < outputs_.size(); ++n)
        populate(outputs_[n], route, n, true);
}
std::optional<ManualControlGroup> ManualRecordingPanel::selectedGroup() const {
    if (groups_->currentIndex() < 0)
        return {};
    const auto take = groups_->currentData().toULongLong();
    const auto s = snapshot();
    for (const auto &g : s->groups)
        if (g.group->take == take && !consumed_.contains({g.generation, take}))
            return g;
    return {};
}
void ManualRecordingPanel::refreshGroups(const ManualControlSnapshot &s) {
    std::vector<std::pair<QString, QVariant>> items;
    for (const auto &g : s.groups)
        if (!consumed_.contains({g.generation, g.group->take}))
            items.push_back({tr("Take %1 — %2")
                                 .arg(QLocale().toString(static_cast<qulonglong>(g.group->take)),
                                      g.group->canceled ? tr("canceled; recovery files retained")
                                      : g.group->complete() ? tr("complete")
                                                            : tr("partial / failed")),
                             QVariant::fromValue(qulonglong(g.group->take))});
    bool changed = groups_->count() != int(items.size());
    for (int n = 0; !changed && n < groups_->count(); ++n)
        changed = groups_->itemText(n) != items[std::size_t(n)].first ||
                  groups_->itemData(n) != items[std::size_t(n)].second;
    if (changed) {
        const auto selected = groups_->currentData();
        QSignalBlocker blocked(groups_);
        groups_->clear();
        for (const auto &[label, value] : items)
            groups_->addItem(label, value);
        const auto index = groups_->findData(selected);
        if (index >= 0)
            groups_->setCurrentIndex(index);
    }
    const auto g = selectedGroup();
    QString summary =
        tr("Take previews will appear here. Adding a take changes the project; Save is separate.");
    bool hasVerified = false;
    if (g) {
        summary = tr("Take range: %1 to %2 frames")
                      .arg(QLocale().toString(static_cast<qlonglong>(g->group->beginFrame)),
                           QLocale().toString(static_cast<qlonglong>(g->group->endFrame)));
        for (const auto &lane : g->group->lanes) {
            const bool verified = lane.outcome == ManualLaneOutcome::Complete && lane.result &&
                                  !lane.error && !lane.verificationError;
            hasVerified |= verified;
            const auto project = project_.snapshot();
            const auto *t =
                project->session ? track(*project->session, lane.spec.trackId) : nullptr;
            summary +=
                tr("\n%1 — %2 captured frames; %3")
                    .arg(t ? text(t->name) : tr("Missing track"),
                         QLocale().toString(static_cast<qlonglong>(lane.capturedFrames)),
                         verified ? tr("verified complete") : tr("retained for review / recovery"));
        }
    }
    preview_->setText(summary);
    const auto p = project_.snapshot();
    const bool scope = g && p->session && g->root == p->root &&
                       (!prepared_ || p->session->id == prepared_->session->id);
    const bool available = scope && !attaching_ && p->io == IoOperation::None && !closingIntent_;
    add_->setEnabled(available && !g->group->canceled && g->group->complete());
    addPartial_->setEnabled(available && !g->group->canceled && !g->group->complete() &&
                            hasVerified);
    keep_->setEnabled(bool(g) && !attaching_ && !closingIntent_);
}
void ManualRecordingPanel::adopt(bool partial) {
    const auto g = selectedGroup();
    const auto p = project_.snapshot();
    if (!g || attaching_ || closingIntent_ || g->root != p->root || !p->session ||
        g->group->canceled || (!partial && !g->group->complete()) || p->io != IoOperation::None)
        return;
    auto records = std::make_shared<std::vector<RecordingResult>>();
    for (const auto &lane : g->group->lanes)
        if (lane.outcome == ManualLaneOutcome::Complete && lane.result && !lane.error &&
            !lane.verificationError)
            records->push_back(*lane.result);
    if (records->empty())
        return;
    ProjectCommand c{CommandKind::AttachRecording};
    c.path = p->root;
    c.recordings = records;
    c.attachmentRequest = barrierSequence_++;
    const auto request = c.attachmentRequest;
    if (project_.submit(std::move(c)) != Admission::Accepted)
        return;
    attaching_ = g;
    add_->setEnabled(false);
    addPartial_->setEnabled(false);
    keep_->setEnabled(false);
    attachmentCount_ = p->attachedRecordings;
    attachmentRequest_ = request;
    attachmentAssets_.clear();
    for (const auto &r : *records)
        attachmentAssets_.push_back(r.asset.id);
}
void ManualRecordingPanel::keep() {
    if (attaching_ || closingIntent_)
        return;
    const auto g = selectedGroup();
    if (g && worker_.acknowledgeGroup(g->generation, g->group->take)) {
        consumed_.insert({g->generation, g->group->take});
        receipt_->setText(tr("Files kept in the project for later recovery."));
    }
}
void ManualRecordingPanel::poll() {
    const auto p = project_.snapshot();
    auto s = snapshot();
    std::erase_if(consumed_, [&](const auto &key) {
        return std::none_of(s->groups.begin(), s->groups.end(), [&](const auto &g) {
            return std::pair{g.generation, g.group->take} == key;
        });
    });
    if (p->projectEpoch != epoch_ && !busy()) {
        epoch_ = p->projectEpoch;
        prepared_.reset();
        inputs_.clear();
        outputs_.clear();
        while (auto *item = routes_->takeAt(0)) {
            delete item->widget();
            delete item;
        }
        arms_->clear();
        consumed_.clear();
        followed_ = 0;
    }
    if (!busy() && p->session) {
        const auto oldSelection = [&] {
            std::set<std::string> ids;
            for (int n = 0; n < arms_->count(); ++n)
                if (arms_->item(n)->checkState() == Qt::Checked)
                    ids.insert(arms_->item(n)->data(Qt::UserRole).toString().toStdString());
            return ids;
        }();
        bool rebuild = arms_->count() != int(p->session->tracks.size());
        for (int n = 0; !rebuild && n < arms_->count(); ++n)
            rebuild = arms_->item(n)->data(Qt::UserRole).toString() !=
                      text(p->session->tracks[std::size_t(n)].id.str());
        if (rebuild) {
            arms_->clear();
            for (const auto &t : p->session->tracks) {
                auto *item = new QListWidgetItem(text(t.name), arms_);
                item->setFlags(item->flags() | Qt::ItemIsUserCheckable);
                item->setData(Qt::UserRole, text(t.id.str()));
                item->setCheckState(oldSelection.contains(t.id.str()) ? Qt::Checked
                                                                      : Qt::Unchecked);
            }
        }
    }
    if (barrier_ && p->lastBarrier == barrier_) {
        barrier_ = 0;
        try {
            if (!p->barrierSession || p->barrierSession->tracks.empty() ||
                p->barrierRoot != p->root)
                throw ProjectError(ErrorCode::InvalidState,
                                   "Manual preparation prefix unavailable");
            ManualControlPreparation prep;
            prep.session = p->barrierSession;
            prep.root = p->barrierRoot;
            prep.modelRevision = p->barrierRevision;
            const auto &session = *prep.session;
            std::vector<Id> ids;
            for (const auto &t : session.tracks)
                ids.push_back(t.id);
            prep.plan = session.master ? session.master->plan
                                       : identityMix(session, ids, session.tracks.front().layout);
            std::uint32_t packed = 0;
            Frame maxDelay = 0;
            for (const auto &id : barrierArms_) {
                const auto *t = track(session, id);
                if (!t || packed + t->layout.channels > 256)
                    throw ProjectError(ErrorCode::InvalidState,
                                       "Manual capture inputs unavailable");
                ManualRecordingArm arm{{id, {}, t->inputLatencyFrames, t->monitoring}, {}};
                for (std::uint32_t n = 0; n < t->layout.channels; ++n)
                    arm.binding.inputChannels.push_back(packed++);
                maxDelay = std::max(maxDelay, t->inputLatencyFrames);
                prep.arms.push_back(std::move(arm));
            }
            auto &run = prep.options.run;
            run.nativeInputs = packed;
            run.backend = CaptureBackend::PipeWire;
            run.playback.graph.maximumFrames = 2048;
            run.playback.graph.startFrame = session.playheadFrame;
            const auto duration = Frame(barrierSeconds_) * session.sampleRate;
            if (session.playheadFrame > std::numeric_limits<Frame>::max() - duration - maxDelay)
                throw ProjectError(ErrorCode::InvalidState, "Manual transport duration overflows");
            run.playback.endFrame = session.playheadFrame + duration + maxDelay;
            run.capture.sampleRate = session.sampleRate;
            run.capture.maximumCallbackFrames = 2048;
            run.capture = withCaptureReserve(run.capture, std::uint32_t(barrierReserve_) * 1000);
            ManualControlCommand c;
            c.preparation = prep;
            if (send(std::move(c))) {
                prepared_ = std::move(prep);
                followed_ = 0;
                inventoryRevision_ = 0;
                nextTake_ = activeTake_ = 0;
                receipt_->clear();
            }
        } catch (const std::exception &e) {
            receipt_->setText(tr("Could not prepare recording: %1").arg(text(e.what())));
        }
    }
    for (const auto &r : s->commands) {
        if (r.sequence == pending_) {
            pending_ = 0;
            if (r.result != ManualControlResult::Applied) {
                receipt_->setText(
                    tr("Recording command was not completed: %1").arg(text(r.diagnostic)));
                if (punchPending_ == r.sequence)
                    punchPending_ = 0;
            } else if (r.kind == ManualControlKind::PrepareTake)
                nextTake_ = r.take;
        }
        worker_.acknowledgeCommand(r.sequence);
    }
    for (const auto &r : s->punches) {
        if (r.command.revision == punchPending_)
            punchPending_ = 0;
        if (r.result == ManualPunchResult::Applied) {
            if (r.command.action == ManualPunchAction::In) {
                activeTake_ = r.activeTake;
                nextTake_ = 0;
            } else
                activeTake_ = 0;
            receipt_->setText(tr("%1 applied at frame %2")
                                  .arg(r.command.action == ManualPunchAction::In ? tr("Punch In")
                                                                                 : tr("Punch Out"),
                                       QLocale().toString(static_cast<qlonglong>(r.appliedFrame))));
        } else {
            const auto reason = [&] {
                switch (r.result) {
                case ManualPunchResult::WrongGeneration:
                    return tr("transport has changed");
                case ManualPunchResult::Late:
                    return tr("requested position has passed");
                case ManualPunchResult::InvalidFrame:
                    return tr("position is outside the prepared range");
                case ManualPunchResult::AlreadyRecording:
                    return tr("already recording");
                case ManualPunchResult::NotRecording:
                    return tr("no active recording");
                case ManualPunchResult::TakeUnavailable:
                    return tr("take is no longer available");
                case ManualPunchResult::TransportStopped:
                    return tr("transport has stopped");
                case ManualPunchResult::CaptureFailed:
                    return tr("capture failed; files retained for recovery");
                case ManualPunchResult::Applied:
                    break;
                }
                return tr("unknown recording state");
            }();
            receipt_->setText(tr("Punch refused: %1.").arg(reason));
        }
        worker_.acknowledgePunch(r.command.revision);
    }
    if (s->stopAcknowledged >= stopToken_ && !transport(*s))
        nextTake_ = activeTake_ = punchPending_ = 0;
    if (attaching_) {
        if (p->attachmentCompleted.request == attachmentRequest_ && !p->attachmentCompleted.error &&
            p->attachedRecordings > attachmentCount_ &&
            p->lastAttachedAssets == attachmentAssets_) {
            if (worker_.acknowledgeGroup(attaching_->generation, attaching_->group->take))
                consumed_.insert({attaching_->generation, attaching_->group->take});
            attaching_.reset();
            receipt_->setText(tr("Take added. Save to persist the project changes. Reprepare "
                                 "playback to hear new clips."));
        } else if ((p->attachmentCompleted.request == attachmentRequest_ &&
                    p->attachmentCompleted.error) ||
                   p->attachmentRejected.request == attachmentRequest_) {
            const auto &result = p->attachmentRejected.request == attachmentRequest_
                                     ? p->attachmentRejected
                                     : p->attachmentCompleted;
            attaching_.reset();
            receipt_->setText(tr("Could not add the take: %1. Files and preview are retained.")
                                  .arg(text(result.diagnostic)));
        }
    }
    if (prepared_ && transport(*s) && p->session && p->modelRevision > followed_ &&
        worker_.follow(s->generation, p->root, p->session, p->modelRevision))
        followed_ = p->modelRevision;
    if (s->errorSerial != errorSeen_) {
        errorSeen_ = s->errorSerial;
        receipt_->setText(
            tr("Recording failed: %1. Available files are retained.").arg(text(s->diagnostic)));
    }
    refreshRoutes(*p, *s);
    refreshGroups(*s);
    const bool ready = s->phase == ManualControlPhase::Ready;
    const bool playing = s->phase == ManualControlPhase::Playing;
    const bool commandReady = !pending_ && !closingIntent_ && s->stopAcknowledged >= stopToken_;
    prepare_->setEnabled(!busy() && !closingIntent_ && s->supported && p->session &&
                         p->io == IoOperation::None && mayPrepare_());
    arms_->setEnabled(!busy() && !closingIntent_);
    seconds_->setEnabled(!busy() && !closingIntent_);
    reserve_->setEnabled(!busy() && !closingIntent_);
    for (auto *c : inputs_)
        c->setEnabled(ready && commandReady);
    for (auto *c : outputs_)
        c->setEnabled(ready && commandReady);
    // Port completeness is checked from descriptors, independent of widget enabled state.
    auto complete = [&](const auto &combos) {
        return !combos.empty() && std::all_of(combos.begin(), combos.end(),
                                              [](auto *c) { return c->currentIndex() > 0; });
    };
    play_->setEnabled(ready && commandReady && complete(inputs_) && complete(outputs_));
    prepareTake_->setEnabled((ready || playing) && commandReady && !nextTake_ && !punchPending_ &&
                             s->groups.size() + s->occupiedSlots < manualPunchSlots);
    punchIn_->setEnabled(playing && commandReady && nextTake_ && !activeTake_ && !punchPending_);
    punchOut_->setEnabled(playing && commandReady && activeTake_ && !punchPending_);
    stop_->setEnabled((transport(*s) || barrier_ || pending_) && !closingIntent_);
    cancel_->setEnabled((transport(*s) || pending_) && !closingIntent_);
    QString status = !s->supported ? tr("Manual recording is unavailable on this platform.")
                     : s->phase == ManualControlPhase::Preparing || barrier_
                         ? tr("Preparing recording…")
                     : ready   ? tr("Ready. Select explicit ports, then Play / monitor.")
                     : playing ? tr("Playing / monitoring — frame %1")
                                     .arg(QLocale().toString(static_cast<qlonglong>(s->position)))
                     : s->phase == ManualControlPhase::Finalizing
                         ? tr("Stopping; draining and verifying recordings…")
                     : s->closed ? tr("Recording worker closed.")
                                 : tr("Transport stopped.");
    if ((ready || playing) && s->parametersPending)
        status += tr(" EQ changes pending audio acknowledgement.");
    status_->setText(status);
}
} // namespace soundcurrent::daw::ui
