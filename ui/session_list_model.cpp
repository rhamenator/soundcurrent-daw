// SPDX-License-Identifier: GPL-3.0-only
#include "session_list_model.hpp"
#include "gui_resources.hpp"
#include <QCoreApplication>
#include <QLocale>
#include <QBrush>
#include <limits>
#include <algorithm>
namespace soundcurrent::daw::ui {
namespace {
QString text(const std::string &s) {
    return QString::fromUtf8(s);
}
QString tr(const char *s) {
    return QCoreApplication::translate("SessionListModel", s);
}
void increment(std::uint64_t &v) {
    if (v != UINT64_MAX)
        ++v;
}
} // namespace
SessionListModel::SessionListModel(Kind k, QObject *parent, bool checkable, ResourceLedger memory)
    : QAbstractListModel(parent), memory_(std::move(memory)), kind_(k), checkable_(checkable) {}
int SessionListModel::rowCount(const QModelIndex &parent) const {
    return parent.isValid() ? 0 : int(rows_.size()) + (kind_ == Kind::Clips);
}
const Id *SessionListModel::identity(int row) const {
    if (kind_ == Kind::Clips)
        --row;
    if (!session_ || row < 0 || std::size_t(row) >= rows_.size())
        return nullptr;
    const auto n = rows_[std::size_t(row)];
    if (kind_ == Kind::Tracks)
        return &session_->tracks[n].id;
    if (kind_ == Kind::Assets)
        return &session_->assets[n].id;
    return clips_ ? &clips_->clips[n].id : nullptr;
}
std::optional<Id> SessionListModel::idAt(int row) const {
    if (const auto *id = identity(row))
        return *id;
    return {};
}
int SessionListModel::rowForId(const Id &id) const {
    const auto it = ids_.find(id.str());
    return it == ids_.end() ? -1 : it->second;
}
QVariant SessionListModel::data(const QModelIndex &index, int role) const {
    increment(reads_);
    if (!index.isValid() || index.model() != this || index.column() || index.row() < 0 ||
        index.row() >= rowCount())
        return {};
    if (kind_ == Kind::Clips && index.row() == 0)
        return role == Qt::DisplayRole ? QVariant(tr("Select a clip"))
               : role == Qt::UserRole  ? QVariant(QString())
                                       : QVariant();
    const auto *id = identity(index.row());
    if (!id)
        return {};
    if (role == Qt::UserRole)
        return text(id->str());
    const auto d = decorations_.find(id->str());
    if (checkable_ && role == Qt::CheckStateRole)
        return d != decorations_.end() && d->second.checked ? Qt::Checked : Qt::Unchecked;
    if (role == Qt::ToolTipRole && d != decorations_.end())
        return d->second.tooltip;
    if (role == Qt::ForegroundRole && d != decorations_.end() && d->second.color.isValid())
        return QBrush(d->second.color);
    if (role != Qt::DisplayRole)
        return {};
    const auto n = rows_[std::size_t(index.row() - (kind_ == Kind::Clips))];
    if (kind_ == Kind::Assets)
        return text(session_->assets[n].relativePath);
    if (kind_ == Kind::Clips)
        return tr("Clip %1 · %2 frames")
            .arg(QLocale().toString(n + 1), QLocale().toString(clips_->clips[n].lengthFrames));
    const auto &t = session_->tracks[n];
    auto label = text(t.name);
    if (checkable_)
        label =
            tr("%1 · %2 channel(s) · %3")
                .arg(label, QLocale().toString(t.layout.channels),
                     t.monitoring == RecordingMonitor::Off             ? tr("Monitoring off")
                     : t.monitoring == RecordingMonitor::AutoRecording ? tr("Auto during recording")
                                                                       : tr("Post-EQ monitoring"));
    if (d != decorations_.end())
        label += d->second.suffix;
    return label;
}
Qt::ItemFlags SessionListModel::flags(const QModelIndex &index) const {
    if (!index.isValid() || index.model() != this || index.column() || index.row() < 0 ||
        index.row() >= rowCount())
        return Qt::NoItemFlags;
    auto f = Qt::ItemIsEnabled | Qt::ItemIsSelectable;
    if (checkable_ && editable_)
        f |= Qt::ItemIsUserCheckable;
    return f;
}
bool SessionListModel::setData(const QModelIndex &index, const QVariant &value, int role) {
    if (!checkable_ || !editable_ || role != Qt::CheckStateRole || !index.isValid() ||
        index.column() || index.model() != this)
        return false;
    const auto id = idAt(index.row());
    if (!id || (value.toInt() != Qt::Checked && value.toInt() != Qt::Unchecked))
        return false;
    const bool checked = value.toInt() == Qt::Checked;
    const auto previous = decorations_.find(id->str());
    if ((previous != decorations_.end() && previous->second.checked) == checked)
        return true;
    try {
        // Reserve a conservative copy allowance before the trial map is built.
        auto staging = guiCharge("GUI check change");
        mapAllowance(staging, decorations_.size() + 1, id->str().capacity() + 1,
                     sizeof(std::pair<const std::string, TrackDecoration>));
        for (const auto &[key, value] : decorations_) {
            staging.add(key.capacity() + 1);
            staging.add(std::size_t(value.suffix.capacity()) +
                            std::size_t(value.tooltip.capacity()),
                        sizeof(QChar));
        }
        auto work = memory_.reserve(staging.bytes());
        auto next = decorations_;
        next[id->str()].checked = checked;
        decorate(std::move(next));
    } catch (const std::exception &) {
        return false;
    }
    if (checkChanged)
        checkChanged(*id, checked);
    return true;
}
void SessionListModel::checkEditing(bool v) {
    if (editable_ == v)
        return;
    editable_ = v;
    if (rowCount())
        emit dataChanged(index(0), index(rowCount() - 1), {Qt::CheckStateRole});
}
struct SessionListModel::Prepared {
    ResourceLease reservation;
    std::vector<std::size_t> rows;
    std::unordered_map<std::string, int> ids;
    std::shared_ptr<const Session> session;
    std::optional<ChannelLayout> filter;
    std::optional<Id> clipTrack;
    const Track *clips = nullptr;
    bool reuse = false;
};
std::shared_ptr<SessionListModel::Prepared>
SessionListModel::prepare(std::shared_ptr<const Session> s, std::optional<ChannelLayout> f,
                          std::optional<Id> clipTrack) const {
    if (s == session_ && f == filter_ && clipTrack == clipTrack_)
        return {};
    if (!s) {
        auto p = std::make_shared<Prepared>();
        p->filter = f;
        p->clipTrack = std::move(clipTrack);
        return p;
    }
    // Same inventory/filter: retain existing indices while borrowing the next snapshot.
    bool same = bool(s) == bool(session_) && f == filter_ && clipTrack == clipTrack_;
    if (same && s) {
        if (kind_ == Kind::Tracks) {
            same = s->tracks.size() == session_->tracks.size();
            for (std::size_t n = 0; same && n < s->tracks.size(); ++n)
                same = s->tracks[n].id == session_->tracks[n].id &&
                       s->tracks[n].layout == session_->tracks[n].layout;
        } else if (kind_ == Kind::Assets) {
            same = s->assets.size() == session_->assets.size() &&
                   s->sampleRate == session_->sampleRate;
            for (std::size_t n = 0; same && n < s->assets.size(); ++n)
                same = s->assets[n].id == session_->assets[n].id &&
                       s->assets[n].layout == session_->assets[n].layout &&
                       s->assets[n].sampleRate == session_->assets[n].sampleRate;
        } else {
            const Track *next = nullptr;
            for (const auto &t : s->tracks)
                if (clipTrack && t.id == *clipTrack)
                    next = &t;
            same = bool(next) == bool(clips_);
            if (same && next) {
                same = next->clips.size() == clips_->clips.size();
                for (std::size_t n = 0; same && n < next->clips.size(); ++n)
                    same = next->clips[n].id == clips_->clips[n].id;
            }
        }
    }
    if (same) {
        auto p = std::make_shared<Prepared>();
        p->reuse = true;
        p->session = std::move(s);
        p->filter = f;
        p->clipTrack = clipTrack;
        if (p->session && clipTrack)
            for (const auto &t : p->session->tracks)
                if (t.id == *clipTrack)
                    p->clips = &t;
        return p;
    }
    const Track *clips = nullptr;
    const auto count = !s                      ? 0
                       : kind_ == Kind::Tracks ? s->tracks.size()
                       : kind_ == Kind::Assets ? s->assets.size()
                                               : [&] {
                                                     for (const auto &t : s->tracks)
                                                         if (clipTrack && t.id == *clipTrack)
                                                             return t.clips.size();
                                                     return std::size_t{0};
                                                 }();
    auto allowance = guiCharge("GUI list indices");
    allowance.add(count, sizeof(std::size_t));
    std::size_t keys = 0;
    if (s) {
        const auto key = [&](const Id &id) {
            PayloadCharge sum("GUI keys", SIZE_MAX);
            sum.add(keys);
            sum.add(id.str().capacity() + 1);
            keys = sum.bytes();
        };
        if (kind_ == Kind::Tracks)
            for (const auto &t : s->tracks)
                key(t.id);
        else if (kind_ == Kind::Assets)
            for (const auto &a : s->assets)
                key(a.id);
        else
            for (const auto &t : s->tracks)
                if (clipTrack && t.id == *clipTrack)
                    for (const auto &c : t.clips)
                        key(c.id);
    }
    mapAllowance(allowance, count, keys, sizeof(std::pair<const std::string, int>));
    auto prepared = std::make_shared<Prepared>();
    prepared->reservation = memory_.reserve(allowance.bytes());
    auto &rows = prepared->rows;
    if (s) {
        if (kind_ == Kind::Tracks) {
            rows.reserve(s->tracks.size());
            for (std::size_t n = 0; n < s->tracks.size(); ++n)
                if (!f || s->tracks[n].layout == *f)
                    rows.push_back(n);
        } else if (kind_ == Kind::Assets) {
            rows.reserve(s->assets.size());
            for (std::size_t n = 0; n < s->assets.size(); ++n)
                if ((!f || s->assets[n].layout == *f) && s->assets[n].sampleRate == s->sampleRate)
                    rows.push_back(n);
        } else if (clipTrack) {
            const auto it = std::find_if(s->tracks.begin(), s->tracks.end(),
                                         [&](const auto &t) { return t.id == *clipTrack; });
            if (it != s->tracks.end()) {
                clips = &*it;
                rows.resize(clips->clips.size());
                for (std::size_t n = 0; n < rows.size(); ++n)
                    rows[n] = n;
            }
        }
    }
    if (rows.size() > std::size_t(std::numeric_limits<int>::max() - (kind_ == Kind::Clips)))
        throw ResourceLimitError("Qt list row representation", rows.size(),
                                 std::numeric_limits<int>::max());
    auto &ids = prepared->ids;
    ids.reserve(rows.size());
    for (std::size_t n = 0; n < rows.size(); ++n) {
        const auto at = rows[n];
        const auto &id = kind_ == Kind::Tracks   ? s->tracks[at].id
                         : kind_ == Kind::Assets ? s->assets[at].id
                                                 : clips->clips[at].id;
        ids.emplace(id.str(), int(n) + (kind_ == Kind::Clips));
    }
    auto actual = guiCharge("GUI list indices");
    actual.add(rows.capacity(), sizeof(std::size_t));
    mapCharge(actual, ids);
    prepared->reservation.resize(actual.bytes());
    prepared->session = std::move(s);
    prepared->filter = f;
    prepared->clipTrack = std::move(clipTrack);
    prepared->clips = clips;
    return prepared;
}
void SessionListModel::commit(std::shared_ptr<Prepared> p) {
    if (!p)
        return;
    const bool reset = !p->reuse && (p->rows != rows_ || p->ids != ids_);
    if (reset)
        beginResetModel();
    session_ = std::move(p->session);
    filter_ = p->filter;
    clipTrack_ = std::move(p->clipTrack);
    clips_ = p->clips;
    if (!p->reuse) {
        rows_ = std::move(p->rows);
        ids_ = std::move(p->ids);
        indicesReservation_ = std::move(p->reservation);
    }
    if (reset) {
        increment(resets_);
        endResetModel();
    } else if (rowCount())
        emit dataChanged(index(0), index(rowCount() - 1));
}
void SessionListModel::update(std::shared_ptr<const Session> s, std::optional<ChannelLayout> f,
                              std::optional<Id> clipTrack) {
    commit(prepare(std::move(s), f, std::move(clipTrack)));
}
std::size_t SessionListModel::resourceBytes() const {
    return indicesReservation_.bytes() + decorationReservation_.bytes();
}
void SessionListModel::decorate(std::unordered_map<std::string, TrackDecoration> d) {
    if (d == decorations_)
        return;
    auto charge = guiCharge("GUI list decorations");
    mapCharge(charge, d);
    charge.add(d.size() + decorations_.size(), sizeof(int));
    for (const auto &[id, value] : d) {
        charge.add(std::size_t(value.suffix.capacity()) + std::size_t(value.tooltip.capacity()),
                   sizeof(QChar));
    }
    auto reservation = memory_.reserve(charge.bytes());
    std::vector<int> changed;
    changed.reserve(d.size() + decorations_.size());
    for (const auto &[id, v] : decorations_) {
        const auto it = d.find(id);
        if (it == d.end() || it->second != v)
            if (const auto p = ids_.find(id); p != ids_.end())
                changed.push_back(p->second);
    }
    for (const auto &[id, v] : d) {
        const auto it = decorations_.find(id);
        if (it == decorations_.end() || it->second != v)
            if (const auto p = ids_.find(id); p != ids_.end())
                changed.push_back(p->second);
    }
    decorations_ = std::move(d);
    decorationReservation_ = std::move(reservation);
    std::sort(changed.begin(), changed.end());
    changed.erase(std::unique(changed.begin(), changed.end()), changed.end());
    for (const auto row : changed)
        emit dataChanged(index(row), index(row));
}
} // namespace soundcurrent::daw::ui
