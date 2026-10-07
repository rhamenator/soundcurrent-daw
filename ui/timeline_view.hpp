// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include <soundcurrent/resource_ledger.hpp>
#include <QAbstractScrollArea>
#include <QCoreApplication>
#include <functional>
#include <memory>
#include <unordered_map>
namespace soundcurrent::daw::ui {
struct TimelinePaintStatistics {
    std::uint64_t snapshotBuilds = 0, paints = 0;
    std::size_t rows = 0, clips = 0, intervalNodes = 0;
};
// GUI-only viewport drawing. Canonical media/state are never mutated by scrolling.
class TimelineView : public QAbstractScrollArea {
    Q_DECLARE_TR_FUNCTIONS(TimelineView)

  public:
    explicit TimelineView(QWidget *parent = nullptr, ResourceLedger = ResourceLedger{});
    struct Prepared;
    std::shared_ptr<Prepared> prepare(std::shared_ptr<const Session>, std::uint64_t epoch) const;
    void commit(std::shared_ptr<Prepared>);
    std::size_t resourceBytes() const {
        return reservation_.bytes();
    }
    void setSnapshot(std::shared_ptr<const Session>, std::uint64_t epoch);
    void setZoom(int);
    void setSelection(std::optional<Id>, std::optional<Id>);
    bool ensureTrackVisible(const Id &);
    bool ensureClipVisible(const Id &, const Id &);
    QRectF clipRectangle(const Id &, const Id &) const;
    TimelinePaintStatistics statistics() const noexcept {
        return stats_;
    }
    std::function<void(std::optional<Id>, std::optional<Id>)> selection;

  protected:
    void paintEvent(QPaintEvent *) override;
    void resizeEvent(QResizeEvent *) override;
    void scrollContentsBy(int, int) override;
    void mousePressEvent(QMouseEvent *) override;
    bool viewportEvent(QEvent *) override;

  private:
    struct Lane {
        std::vector<std::size_t> order;
        std::vector<Frame> ends;
        std::size_t base = 0;
    };
    ResourceLedger memory_;
    ResourceLease reservation_;
    std::shared_ptr<const Session> session_;
    std::uint64_t epoch_ = 0;
    Frame extent_ = 1;
    int zoom_ = 50;
    std::optional<Id> track_, clip_;
    std::unordered_map<std::string, std::size_t> tracks_;
    std::vector<Lane> lanes_;
    TimelinePaintStatistics stats_;
    mutable std::vector<std::size_t> visible_;
    double scale() const;
    void geometry();
    QRectF rectangle(std::size_t row, const Clip &) const;
    void visibleClips(std::size_t row, Frame first, Frame end, std::vector<std::size_t> &,
                      std::size_t *visited = nullptr) const;
    std::pair<std::optional<Id>, std::optional<Id>> hit(const QPoint &) const;
};
} // namespace soundcurrent::daw::ui
