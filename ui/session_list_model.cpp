// SPDX-License-Identifier: GPL-3.0-only
#include "session_list_model.hpp"
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
SessionListModel::SessionListModel(Kind k, QObject *parent, bool checkable)
    : QAbstractListModel(parent), kind_(k), checkable_(checkable) {}
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
    auto &d = decorations_[id->str()];
    if (d.checked == checked)
        return true;
    d.checked = checked;
    emit dataChanged(index, index, {Qt::CheckStateRole});
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
void SessionListModel::update(std::shared_ptr<const Session> s, std::optional<ChannelLayout> f,
                              std::optional<Id> clipTrack) {
    if (s == session_ && f == filter_ && clipTrack == clipTrack_)
        return;
    const Track *clips = nullptr;
    std::vector<std::size_t> rows;
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
    std::unordered_map<std::string, int> ids;
    ids.reserve(rows.size());
    for (std::size_t n = 0; n < rows.size(); ++n) {
        const auto at = rows[n];
        const auto &id = kind_ == Kind::Tracks   ? s->tracks[at].id
                         : kind_ == Kind::Assets ? s->assets[at].id
                                                 : clips->clips[at].id;
        ids.emplace(id.str(), int(n) + (kind_ == Kind::Clips));
    }
    const bool reset = rows != rows_ || ids != ids_;
    if (reset)
        beginResetModel();
    session_ = std::move(s);
    filter_ = f;
    clipTrack_ = std::move(clipTrack);
    clips_ = clips;
    rows_ = std::move(rows);
    ids_ = std::move(ids);
    if (reset) {
        increment(resets_);
        endResetModel();
    } else if (rowCount())
        emit dataChanged(index(0), index(rowCount() - 1));
}
void SessionListModel::decorate(std::unordered_map<std::string, TrackDecoration> d) {
    if (d == decorations_)
        return;
    std::vector<int> changed;
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
    std::sort(changed.begin(), changed.end());
    changed.erase(std::unique(changed.begin(), changed.end()), changed.end());
    for (const auto row : changed)
        emit dataChanged(index(row), index(row));
}
} // namespace soundcurrent::daw::ui
