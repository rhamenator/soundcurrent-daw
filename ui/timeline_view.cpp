// SPDX-License-Identifier: GPL-3.0-only
#include "timeline_view.hpp"
#include <soundcurrent/waveform_projection.hpp>
#include "gui_resources.hpp"
#include <QPainter>
#include <QTimer>
#include <QScrollBar>
#include <QMouseEvent>
#include <QHelpEvent>
#include <QToolTip>
#include <QLocale>
#include <algorithm>
#include <numeric>
#include <cmath>
#include <limits>
namespace soundcurrent::daw::ui {
namespace {
constexpr int header = 35, labelWidth = 70;
QString text(const std::string &s) {
    return QString::fromUtf8(s);
}
void increment(std::uint64_t &v) {
    if (v != UINT64_MAX)
        ++v;
}
Frame clippedFrame(long double v, Frame extent) {
    if (v <= 0)
        return 0;
    if (v >= static_cast<long double>(extent))
        return extent;
    return Frame(v);
}
} // namespace
TimelineView::TimelineView(QWidget *parent, ResourceLedger memory)
    : QAbstractScrollArea(parent), memory_(std::move(memory)), waveforms_(memory_) {
    setObjectName("audioTimeline");
    setLayoutDirection(Qt::LeftToRight);
    setAccessibleName(tr("Audio clip timeline"));
    setMinimumHeight(180);
    setFocusPolicy(Qt::StrongFocus);
    horizontalScrollBar()->setSingleStep(20);
    verticalScrollBar()->setSingleStep(1);
    waveformTimer_ = new QTimer(this); waveformTimer_->setInterval(80);
    connect(waveformTimer_, &QTimer::timeout, this, [this] { pollWaveforms(); });
    waveformTimer_->start();
}
double TimelineView::scale() const {
    return 800. / double(extent_) * std::exp2((zoom_ - 50) / 10.);
}
void TimelineView::geometry() {
    waveformsDirty_ = true;
    const auto rows = session_ ? session_->tracks.size() : 0;
    const auto shown = std::size_t(std::max(1, (viewport()->height() - header) / rowHeight()));
    // Qt uses signed-int scrollbar positions. Future larger-than-INT_MAX row
    // inventories need paging; do not silently wrap or truncate an admitted view.
    const auto maximum = rows > shown ? rows - shown : 0;
    if (maximum > std::size_t(std::numeric_limits<int>::max()))
        throw ResourceLimitError("Qt timeline row representation", maximum,
                                 std::numeric_limits<int>::max());
    verticalScrollBar()->setRange(0, int(maximum));
    verticalScrollBar()->setPageStep(int(shown));
    horizontalScrollBar()->setRange(
        0, std::max(0, int(std::ceil(90. + double(extent_) * scale())) - viewport()->width()));
    horizontalScrollBar()->setPageStep(std::max(1, viewport()->width() - labelWidth));
}
struct TimelineView::Prepared {
    ResourceLease reservation, rootCredit;
    std::filesystem::path root;
    std::unordered_map<std::string, std::size_t> tracks;
    std::vector<Lane> lanes;
    std::vector<std::size_t> visible;
    std::shared_ptr<const Session> session;
    std::uint64_t epoch = 0;
    Frame extent = 1;
    bool reuse = false;
};
std::shared_ptr<TimelineView::Prepared> TimelineView::prepare(std::shared_ptr<const Session> s,
                                                              std::uint64_t epoch, std::filesystem::path root) const {
    if (!root.empty() && (!root.is_absolute() || root.native().size() > 4096))
        throw ProjectError(ErrorCode::InvalidParameter, "Invalid timeline root");
    if (s == session_ && epoch == epoch_ && root == root_)
        return {};
    if (!s) {
        auto p = std::make_shared<Prepared>();
        p->epoch = epoch;
        if (!root.empty()) p->rootCredit = memory_.reserve(65536);
        p->root = std::filesystem::path(root.native().data(), root.native().data()+root.native().size());
        return p;
    }
    bool same = bool(s) == bool(session_);
    if (same && s) {
        same = s->tracks.size() == session_->tracks.size() && s->sampleRate == session_->sampleRate;
        for (std::size_t n = 0; same && n < s->tracks.size(); ++n)
            same = s->tracks[n].id == session_->tracks[n].id &&
                   s->tracks[n].clips == session_->tracks[n].clips;
    }
    if (same) {
        auto p = std::make_shared<Prepared>();
        p->reuse = true;
        p->session = std::move(s);
        p->epoch = epoch;
        if (!root.empty()) p->rootCredit = memory_.reserve(65536);
        p->root = std::filesystem::path(root.native().data(), root.native().data()+root.native().size());
        p->extent = extent_;
        return p;
    }
    auto allowance = guiCharge("GUI timeline indices");
    const auto count = s ? s->tracks.size() : 0;
    if (count > std::size_t(INT_MAX))
        throw ResourceLimitError("Qt timeline row representation", count, INT_MAX);
    allowance.add(count, sizeof(Lane));
    std::size_t maxClips = 0, keys = 0;
    if (s)
        for (const auto &t : s->tracks) {
            PayloadCharge sum("Timeline keys", SIZE_MAX);
            sum.add(keys);
            sum.add(t.id.str().capacity() + 1);
            keys = sum.bytes();
            allowance.add(t.clips.size(), sizeof(std::size_t));
            maxClips = std::max(maxClips, t.clips.size());
            if (!t.clips.empty()) {
                std::size_t base = 1;
                while (base < t.clips.size()) {
                    if (base > SIZE_MAX / 4)
                        throw ResourceLimitError("Timeline interval index", SIZE_MAX, SIZE_MAX,
                                                 true);
                    base *= 2;
                }
                allowance.add(base, sizeof(Frame) * 2);
            }
        }
    allowance.add(maxClips, sizeof(std::size_t));
    mapAllowance(allowance, count, keys, sizeof(std::pair<const std::string, std::size_t>));
    auto prepared = std::make_shared<Prepared>();
    prepared->reservation = memory_.reserve(allowance.bytes());
    auto &tracks = prepared->tracks;
    auto &lanes = prepared->lanes;
    prepared->visible.reserve(maxClips);
    Frame extent = s ? std::max<Frame>(1, s->sampleRate) : 1;
    if (s) {
        if (s->tracks.size() > std::size_t(std::numeric_limits<int>::max()))
            throw ResourceLimitError("Qt timeline row representation", s->tracks.size(),
                                     std::numeric_limits<int>::max());
        tracks.reserve(s->tracks.size());
        lanes.resize(s->tracks.size());
        for (std::size_t row = 0; row < s->tracks.size(); ++row) {
            const auto &t = s->tracks[row];
            tracks.emplace(t.id.str(), row);
            auto &lane = lanes[row];
            lane.order.resize(t.clips.size());
            std::iota(lane.order.begin(), lane.order.end(), std::size_t{0});
            std::sort(lane.order.begin(), lane.order.end(), [&](auto a, auto b) {
                return t.clips[a].startFrame < t.clips[b].startFrame;
            });
            if (!lane.order.empty()) {
                lane.base = 1;
                while (lane.base < lane.order.size()) {
                    if (lane.base > SIZE_MAX / 4)
                        throw ResourceLimitError("Timeline interval index", SIZE_MAX, SIZE_MAX,
                                                 true);
                    lane.base *= 2;
                }
                lane.ends.resize(lane.base * 2, 0);
                for (std::size_t n = 0; n < lane.order.size(); ++n) {
                    const auto &c = t.clips[lane.order[n]];
                    lane.ends[lane.base + n] = c.startFrame + c.lengthFrames;
                    extent = std::max(extent, c.startFrame + c.lengthFrames);
                }
                for (std::size_t n = lane.base - 1; n > 0; --n)
                    lane.ends[n] = std::max(lane.ends[n * 2], lane.ends[n * 2 + 1]);
            }
        }
    }
    auto actual = guiCharge("GUI timeline indices");
    mapCharge(actual, tracks);
    actual.add(lanes.capacity(), sizeof(Lane));
    actual.add(prepared->visible.capacity(), sizeof(std::size_t));
    for (const auto &lane : lanes) {
        actual.add(lane.order.capacity(), sizeof(std::size_t));
        actual.add(lane.ends.capacity(), sizeof(Frame));
    }
    prepared->reservation.resize(actual.bytes());
    prepared->session = std::move(s);
    prepared->epoch = epoch;
    if (!root.empty()) prepared->rootCredit = memory_.reserve(65536);
    prepared->root = std::filesystem::path(root.native().data(), root.native().data()+root.native().size());
    prepared->extent = extent;
    return prepared;
}
void TimelineView::commit(std::shared_ptr<Prepared> p) {
    if (!p)
        return;
    const bool newProject = epoch_ != p->epoch;
    session_ = std::move(p->session);
    epoch_ = p->epoch;
    root_ = std::move(p->root); rootCredit_ = std::move(p->rootCredit);
    waveforms_.context(session_, root_, epoch_);
    waveformStatus_.clear(); setAccessibleDescription({});
    waveformsDirty_ = true;
    if (!p->reuse) {
        tracks_ = std::move(p->tracks);
        lanes_ = std::move(p->lanes);
        visible_ = std::move(p->visible);
        reservation_ = std::move(p->reservation);
        increment(stats_.snapshotBuilds);
    }
    extent_ = p->extent;
    geometry();
    if (newProject) {
        verticalScrollBar()->setValue(0);
        horizontalScrollBar()->setValue(0);
    }
    viewport()->update();
}
void TimelineView::setSnapshot(std::shared_ptr<const Session> s, std::uint64_t epoch, std::filesystem::path root) {
    commit(prepare(std::move(s), epoch, std::move(root)));
}
void TimelineView::setZoom(int z) {
    z = std::clamp(z, 0, 100);
    if (z == zoom_)
        return;
    zoom_ = z;
    geometry();
    viewport()->update();
}
void TimelineView::setSelection(std::optional<Id> t, std::optional<Id> c) {
    if (t == track_ && c == clip_)
        return;
    track_ = std::move(t);
    clip_ = std::move(c);
    viewport()->update();
}
void TimelineView::resizeEvent(QResizeEvent *e) {
    QAbstractScrollArea::resizeEvent(e);
    geometry();
}
void TimelineView::scrollContentsBy(int, int) {
    waveformsDirty_ = true;
    viewport()->update();
}
QRectF TimelineView::rectangle(std::size_t row, const Clip &c) const {
    return {labelWidth + double(c.startFrame) * scale() - horizontalScrollBar()->value(),
            header + (double(row) - verticalScrollBar()->value()) * rowHeight(),
            std::max(.002, double(c.lengthFrames) * scale()), double(rowHeight()-18)};
}
QRectF TimelineView::clipRectangle(const Id &track, const Id &clip) const {
    const auto p = tracks_.find(track.str());
    if (!session_ || p == tracks_.end())
        return {};
    const auto &t = session_->tracks[p->second];
    const auto c =
        std::find_if(t.clips.begin(), t.clips.end(), [&](const auto &v) { return v.id == clip; });
    return c == t.clips.end() ? QRectF{} : rectangle(p->second, *c);
}
bool TimelineView::ensureTrackVisible(const Id &id) {
    const auto p = tracks_.find(id.str());
    if (p == tracks_.end())
        return false;
    const auto first = std::size_t(verticalScrollBar()->value());
    const auto shown = std::size_t(std::max(1, (viewport()->height() - header) / rowHeight()));
    if (p->second < first)
        verticalScrollBar()->setValue(int(p->second));
    else if (p->second >= first + shown)
        verticalScrollBar()->setValue(int(p->second - shown + 1));
    return true;
}
bool TimelineView::ensureClipVisible(const Id &track, const Id &clip) {
    auto r = clipRectangle(track, clip);
    if (r.isEmpty() || !ensureTrackVisible(track))
        return false;
    r = clipRectangle(track, clip);
    if (r.left() < labelWidth)
        horizontalScrollBar()->setValue(horizontalScrollBar()->value() +
                                        int(std::floor(r.left() - labelWidth)));
    else if (r.right() > viewport()->width())
        horizontalScrollBar()->setValue(
            horizontalScrollBar()->value() +
            int(std::ceil(std::min(r.right() - viewport()->width(), r.left() - labelWidth))));
    return true;
}
void TimelineView::visibleClips(std::size_t row, Frame first, Frame end,
                                std::vector<std::size_t> &out, std::size_t *visited) const {
    out.clear();
    if (!session_ || row >= lanes_.size())
        return;
    const auto &lane = lanes_[row];
    const auto &clips = session_->tracks[row].clips;
    const auto walk = [&](auto &self, std::size_t node, std::size_t lo, std::size_t hi) -> void {
        if (visited)
            ++*visited;
        if (lo >= lane.order.size() || lane.ends[node] <= first ||
            clips[lane.order[lo]].startFrame > end)
            return;
        if (hi - lo == 1) {
            out.push_back(lane.order[lo]);
            return;
        }
        const auto mid = lo + (hi - lo) / 2;
        self(self, node * 2, lo, mid);
        self(self, node * 2 + 1, mid, hi);
    };
    if (lane.base)
        walk(walk, 1, 0, lane.base);
    // Preserve original clip stacking and therefore original hit precedence.
    std::sort(out.begin(), out.end());
}
void TimelineView::paintEvent(QPaintEvent *) {
    increment(stats_.paints);
    stats_.rows = stats_.clips = stats_.intervalNodes = 0;
    QPainter p(viewport());
    p.fillRect(viewport()->rect(), palette().base());
    if (!session_)
        return;
    const auto horizontal = horizontalScrollBar()->value();
    const auto k = scale();
    const auto first = clippedFrame(static_cast<long double>(horizontal) / k, extent_);
    const auto end = clippedFrame(
        (static_cast<long double>(horizontal) + std::max(1, viewport()->width() - labelWidth)) / k,
        extent_);
    p.setPen(palette().text().color());
    for (unsigned tick = 0; tick <= 8; ++tick) {
        const auto f = double(extent_) * tick / 8.;
        p.drawText(QPointF(labelWidth + f * k - horizontal, 20),
                   QLocale().toString(f / session_->sampleRate, 'f', 2) + tr(" s"));
    }
    p.drawText(QPointF(labelWidth, 33), waveformStatus_);
    const auto begin = std::size_t(verticalScrollBar()->value());
    const auto shown =
        std::size_t(std::max(0, viewport()->height() - header) + rowHeight() - 1) / rowHeight();
    const auto limit = std::min(session_->tracks.size(), begin + shown);
    for (std::size_t row = begin; row < limit; ++row) {
        ++stats_.rows;
        const auto &t = session_->tracks[row];
        const auto y = header + double(row - begin) * rowHeight();
        p.setPen(palette().text().color());
        p.drawText(QRectF(0, y, labelWidth - 5, rowHeight()-12),
                   Qt::AlignLeft | Qt::AlignTop | Qt::TextWordWrap, text(t.name));
        p.save();
        p.setClipRect(QRectF(labelWidth, header, viewport()->width() - labelWidth,
                             viewport()->height() - header));
        p.setPen(palette().mid().color());
        p.drawLine(QPointF(labelWidth, y + rowHeight()-12), QPointF(viewport()->width(), y + rowHeight()-12));
        visibleClips(row, first, end, visible_, &stats_.intervalNodes);
        for (const auto ordinal : visible_) {
            const auto &c = t.clips[ordinal];
            auto r = rectangle(row, c);
            if (!r.intersects(QRectF(viewport()->rect())))
                continue;
            ++stats_.clips;
            p.setPen(QPen(
                track_ == std::optional<Id>(t.id) && clip_ == std::optional<Id>(c.id)
                    ? palette().highlight().color()
                    : QColor(Qt::black),
                track_ == std::optional<Id>(t.id) && clip_ == std::optional<Id>(c.id) ? 3 : 1));
            p.setBrush(QColor::fromHsv(int((row % 360) * 47) % 360, 150, 195));
            p.drawRect(r);
            waveforms_.draw(p, c, r, QRectF(labelWidth,header,viewport()->width()-labelWidth,viewport()->height()-header));
        }
        p.restore();
    }
}
void TimelineView::pollWaveforms() {
    if (!session_ || root_.empty() || (!waveformsDirty_ && !waveforms_.pending())) return;
    const auto before=waveforms_.readyCount();
    // Still views only poll results; do not rebuild asset keys and scan the
    // project on every timer tick while many source workers finish in sequence.
    if (waveformsDirty_) {
        constexpr auto maximum = TimelineWaveforms::maximumVisibleSources;
        std::vector<Id> assets; assets.reserve(maximum + 1);
        std::vector<WaveformWindow> windows; windows.reserve(maximum + 1);
        const auto k = scale(); const auto horizontal = horizontalScrollBar()->value();
        const auto first = clippedFrame(static_cast<long double>(horizontal)/k, extent_);
        const auto edge = clippedFrame((static_cast<long double>(horizontal)+std::max(1,viewport()->width()-labelWidth))/k,extent_);
        const auto end = edge < extent_ ? edge + 1 : edge;
        const auto begin = std::size_t(verticalScrollBar()->value());
        const auto shown = std::size_t(std::max(0,viewport()->height()-header)+rowHeight()-1)/rowHeight();
        for (auto row=begin; row<std::min(session_->tracks.size(),begin+shown) && assets.size()<=maximum; ++row) {
            visibleClips(row,first,end,visible_);
            for (auto n:visible_) {
                const auto &clip=session_->tracks[row].clips[n]; const auto &id=clip.assetId;
                const auto asset=std::find_if(session_->assets.begin(),session_->assets.end(),[&](const auto &a){return a.id==id;});
                if(asset==session_->assets.end()) {
                    // Let the adapter refuse the entire invalid view; never hide
                    // a missing asset by silently preparing the remaining subset.
                    assets.push_back(id);windows.emplace_back();break;
                }
                const auto map=clipSourceMap(clip,asset->sampleRate,session_->sampleRate);
                const auto sample=[&](Frame frame) {
                    const auto p=map.at(std::clamp<Frame>(frame-clip.startFrame,0,clip.lengthFrames));
                    const auto whole=std::uint64_t(p.frame), frames=std::uint64_t(asset->frames);
                    return whole>=frames ? frames : whole+(p.fraction!=0);
                };
                const auto left=sample(first), right=sample(end);
                const auto found=std::find(assets.begin(),assets.end(),id);
                if(found==assets.end()) {assets.push_back(id);windows.push_back({left,right-left});}
                else {
                    auto &window=windows[std::size_t(found-assets.begin())];
                    const auto stop=std::max(window.first+window.frames,right);
                    window.first=std::min(window.first,left);window.frames=stop-window.first;
                }
                if (assets.size()>maximum) break;
            }
        }
        waveforms_.update(assets,windows);
    } else waveforms_.poll();
    const auto message=waveforms_.status();
    if (waveformsDirty_ || before!=waveforms_.readyCount() || message!=waveformStatus_) viewport()->update();
    waveformStatus_=message; setAccessibleDescription(message); waveformsDirty_=false;
}
void TimelineView::setWaveformChannel(std::optional<std::uint32_t> channel) {
    waveforms_.setChannel(channel);
    waveformStatus_=waveforms_.status();setAccessibleDescription(waveformStatus_);
    viewport()->update();
}
void TimelineView::retryWaveforms() {
    waveforms_.retry(); waveformsDirty_=true; waveformStatus_.clear();
    setAccessibleDescription({}); viewport()->update();
}
void TimelineView::shutdownWaveforms() { waveformTimer_->stop(); waveforms_.shutdown(); }
bool TimelineView::waveformsClosed() const { return waveforms_.closed(); }
std::pair<std::optional<Id>, std::optional<Id>> TimelineView::hit(const QPoint &point) const {
    if (!session_ || point.y() < header)
        return {};
    const auto row =
        std::size_t(verticalScrollBar()->value()) + std::size_t((point.y() - header) / rowHeight());
    if (row >= session_->tracks.size())
        return {};
    const auto &t = session_->tracks[row];
    if (point.x() < labelWidth)
        return {t.id, {}};
    const auto frame = clippedFrame(
        (static_cast<long double>(point.x() - labelWidth) + horizontalScrollBar()->value()) /
            scale(),
        extent_);
    visibleClips(row, frame, frame, visible_);
    for (auto it = visible_.rbegin(); it != visible_.rend(); ++it)
        if (rectangle(row, t.clips[*it]).contains(point))
            return {t.id, t.clips[*it].id};
    return {t.id, {}};
}
void TimelineView::mousePressEvent(QMouseEvent *e) {
    if (e->button() != Qt::LeftButton) {
        QAbstractScrollArea::mousePressEvent(e);
        return;
    }
    const auto [track, clip] = hit(e->position().toPoint());
    if (selection)
        selection(track, clip);
    e->accept();
}
bool TimelineView::viewportEvent(QEvent *e) {
    if (e->type() == QEvent::ToolTip) {
        auto *help = static_cast<QHelpEvent *>(e);
        const auto [track, clip] = hit(help->pos());
        if (track && clip) {
            const auto &t = session_->tracks[tracks_.at(track->str())];
            const auto c = std::find_if(t.clips.begin(), t.clips.end(),
                                        [&](const auto &v) { return v.id == *clip; });
            QToolTip::showText(help->globalPos(),
                               tr("Start %1 · source %2 + %3/%4 · length %5 project frames")
                                   .arg(QLocale().toString(c->startFrame),
                                        QLocale().toString(c->sourceFrame),
                                        QLocale().toString(qulonglong(c->sourceTiming.fraction)),
                                        QLocale().toString(qulonglong(c->sourceTiming.denominator)),
                                        QLocale().toString(c->lengthFrames)),
                               viewport());
        } else
            QToolTip::hideText();
        return true;
    }
    return QAbstractScrollArea::viewportEvent(e);
}
} // namespace soundcurrent::daw::ui
