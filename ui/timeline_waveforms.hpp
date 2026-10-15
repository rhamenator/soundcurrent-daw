// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include "waveform_cache.hpp"
#include <QPainter>
namespace soundcurrent::daw::ui {
// GUI-only adapter. update() runs from the timeline's control timer; draw() only
// reads prepared immutable data. No disk or child work is performed by painting.
class TimelineWaveforms {
  public:
    explicit TimelineWaveforms(ResourceLedger);
    void context(std::shared_ptr<const Session>, std::filesystem::path, std::uint64_t);
    void update(const std::vector<Id> &visibleAssets);
    void draw(QPainter &, const Clip &, const QRectF &, const QRectF &viewport) const;
    QString status() const;
    void retry();
    void shutdown();
    bool closed() const;
    std::size_t readyCount() const;
    bool pending() const;
  private:
    struct Item { Id asset; WaveformKey key; CachedWaveform result; };
    ResourceLedger memory_;
    ResourceLease metadata_;
    std::unique_ptr<WaveformCache> cache_;
    std::shared_ptr<const Session> session_;
    std::filesystem::path root_;
    std::uint64_t epoch_ = 0;
    std::vector<Item> items_;
    bool closing_ = false, refused_ = false;
};
} // namespace soundcurrent::daw::ui
