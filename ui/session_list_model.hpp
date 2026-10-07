// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include <soundcurrent/session.hpp>
#include <QAbstractListModel>
#include <QColor>
#include <memory>
#include <functional>
#include <unordered_map>
namespace soundcurrent::daw::ui {
struct TrackDecoration {
    bool checked = false;
    QString suffix, tooltip;
    QColor color;
    bool operator==(const TrackDecoration &) const = default;
};
// GUI-only immutable borrow. No per-row QObject, widget, or retained display string.
class SessionListModel : public QAbstractListModel {
  public:
    enum class Kind { Tracks, Assets, Clips };
    SessionListModel(Kind, QObject *parent, bool checkable = false);
    int rowCount(const QModelIndex &parent = {}) const override;
    QVariant data(const QModelIndex &, int role) const override;
    Qt::ItemFlags flags(const QModelIndex &) const override;
    bool setData(const QModelIndex &, const QVariant &, int role) override;
    void update(std::shared_ptr<const Session>, std::optional<ChannelLayout> filter = {},
                std::optional<Id> clipTrack = {});
    void decorate(std::unordered_map<std::string, TrackDecoration>);
    void checkEditing(bool);
    int rowForId(const Id &) const;
    std::optional<Id> idAt(int) const;
    std::function<void(const Id &, bool)> checkChanged;
    std::uint64_t resets() const noexcept {
        return resets_;
    }
    std::uint64_t dataReads() const noexcept {
        return reads_;
    }

  private:
    Kind kind_;
    bool checkable_, editable_ = false;
    std::shared_ptr<const Session> session_;
    std::optional<ChannelLayout> filter_;
    std::optional<Id> clipTrack_;
    const Track *clips_ = nullptr;
    std::vector<std::size_t> rows_;
    std::unordered_map<std::string, int> ids_;
    std::unordered_map<std::string, TrackDecoration> decorations_;
    std::uint64_t resets_ = 0;
    mutable std::uint64_t reads_ = 0;
    const Id *identity(int) const;
};
} // namespace soundcurrent::daw::ui
