// SPDX-License-Identifier: GPL-3.0-only
#include "timeline_waveforms.hpp"
#include <soundcurrent/waveform_projection.hpp>
#include <QCoreApplication>
#include <algorithm>
#include <cmath>
namespace soundcurrent::daw::ui {
namespace { QString tr(const char *s) { return QCoreApplication::translate("TimelineWaveforms", s); } }
TimelineWaveforms::TimelineWaveforms(ResourceLedger memory) : memory_(std::move(memory)) {}
void TimelineWaveforms::context(std::shared_ptr<const Session> session, std::filesystem::path root, std::uint64_t epoch) {
    if (session == session_ && root == root_ && epoch == epoch_) return;
    if (root != root_ || epoch != epoch_) {
        if (cache_) cache_->invalidateSources();
    }
    items_.clear(); refused_ = false;
    session_ = std::move(session); root_ = std::move(root); epoch_ = epoch;
}
void TimelineWaveforms::update(const std::vector<Id> &assets) {
    if (closing_ || !session_ || root_.empty() || !epoch_) return;
    try {
        if (assets.size() > 32) throw ProjectError(ErrorCode::ResourceLimit, "Visible waveform source count");
        if (!cache_) {
            auto credit = memory_.reserve(128 * 1024);
            WaveformCacheOptions options; options.controller.memory = memory_;
            auto cache = std::make_unique<WaveformCache>(options);
            items_.reserve(32); metadata_ = std::move(credit); cache_ = std::move(cache);
        }
        std::vector<Item> next; next.reserve(32);
        std::vector<WaveformKey> keys; keys.reserve(32);
        for (const auto &id : assets) {
            const auto a = std::find_if(session_->assets.begin(), session_->assets.end(), [&](const auto &a) { return a.id == id; });
            if (a == session_->assets.end() || !a->relativePath.starts_with("media/"))
                throw ProjectError(ErrorCode::InvalidState, "Waveform asset missing");
            const auto channels = a->layout.channels;
            if (!channels || a->frames <= 0 || a->sha256.size() != 64)
                throw ProjectError(ErrorCode::InvalidState, "Invalid waveform source format");
            // Aim at <= 65,536 channel-bins at the finest level. Short sources
            // retain per-sample detail; large sources need finer window support.
            std::uint64_t width = 1;
            const auto target = std::max<std::uint64_t>(1, 65536 / channels);
            while ((a->frames / width + (a->frames % width != 0)) > target) width *= 2;
            WaveformKey key; std::copy(a->sha256.begin(), a->sha256.end(), key.source.sha256.begin());
            key.source.frames=std::uint64_t(a->frames); key.source.rate=a->sampleRate; key.source.channels=channels;
            key.relative=a->relativePath.substr(6); key.baseFramesPerBin=width;
            keys.push_back(key); next.push_back({id, std::move(key), {}});
        }
        cache_->setVisible(root_ / "media", epoch_, std::move(keys));
        cache_->poll();
        for (auto &item : next) item.result = cache_->lookup(root_ / "media", epoch_, item.key);
        items_ = std::move(next); refused_ = false;
    } catch (const std::exception &) {
        // A rejected complete view must never display the previous view's data.
        items_.clear(); refused_ = true;
    }
}
QString TimelineWaveforms::status() const {
    if (root_.empty()) return {};
    if (refused_) return tr("Waveforms unavailable: visible source/resource limit");
    if (std::any_of(items_.begin(), items_.end(), [](const auto &i) { return i.result.phase == CachedWaveformPhase::Fault; }))
        return tr("Some source waveforms are unavailable; use Retry waveforms");
    if (std::any_of(items_.begin(), items_.end(), [](const auto &i) { return !i.result.waveform; }))
        return tr("Preparing source waveforms…");
    return items_.empty() ? QString() : tr("Source PCM · shaded edges need finer detail · >2 channels combined");
}
bool TimelineWaveforms::pending() const {
    return std::any_of(items_.begin(),items_.end(),[](const auto &i) {
        return i.result.phase==CachedWaveformPhase::Pending || i.result.phase==CachedWaveformPhase::Loading;
    });
}
std::size_t TimelineWaveforms::readyCount() const {
    return std::count_if(items_.begin(), items_.end(), [](const auto &i) { return bool(i.result.waveform); });
}
void TimelineWaveforms::draw(QPainter &p, const Clip &clip, const QRectF &rect, const QRectF &viewport) const {
    const auto item = std::find_if(items_.begin(), items_.end(), [&](const auto &i) { return i.asset == clip.assetId; });
    if (!session_ || item == items_.end() || !item->result.waveform || rect.width() < 2) return;
    const auto &wave = *item->result.waveform;
    const auto k = rect.width() / double(clip.lengthFrames);
    const auto area = rect.adjusted(3, 5, -3, -5).intersected(viewport);
    if (area.isEmpty()) return;
    bool saved = false;
    try {
        std::size_t level = 0;
        const auto binPixels = [&](std::size_t l) {
            const auto span = waveformBinProjectRange(wave, clip, session_->sampleRate, l, 0);
            return double(span.second - span.first) * k;
        };
        // At most about one bin per pixel; for combined multichannel envelopes,
        // also bound channel/bin work per visible clip to roughly 65,536.
        const auto minimumPixels = std::max(1., area.width() * wave.source().channels / 65536.);
        while (level + 1 < wave.levels().size() && binPixels(level) < minimumPixels) ++level;
        const auto first = std::clamp<Frame>(Frame(std::floor((area.left()-rect.left())/k)), 0, clip.lengthFrames);
        const auto end = std::clamp<Frame>(Frame(std::ceil((area.right()-rect.left())/k)), first, clip.lengthFrames);
        const auto span = waveformCoverage(wave, clip, session_->sampleRate, level, first, end);
        p.save(); saved = true; p.setClipRect(area, Qt::IntersectClip);
        p.fillRect(area, QColor(35,45,55));
        const auto channels = wave.source().channels;
        const unsigned lanes = channels <= 2 ? channels : 1;
        for (unsigned lane = 0; lane < lanes; ++lane) {
            const auto middle = rect.top() + 5 + (rect.height()-10) * (lane+.5) / lanes;
            const auto height = (rect.height()-10) / (2*lanes);
            p.setPen(QColor(100, 115, 135)); p.drawLine(QPointF(area.left(), middle), QPointF(area.right(), middle));
            for (auto bin = span.firstFullBin; bin < span.endFullBin; ++bin) {
                auto peak = wave.peak(level, bin, lane);
                if (channels > 2) for (unsigned c = 1; c < channels; ++c) {
                    const auto v = wave.peak(level, bin, c);
                    peak.minimum = std::min(peak.minimum, v.minimum); peak.maximum = std::max(peak.maximum, v.maximum);
                }
                const auto position = waveformBinProjectRange(wave, clip, session_->sampleRate, level, bin);
                const auto left = rect.left() + double(position.first)*k;
                const auto right = rect.left() + double(position.second)*k;
                const auto over = peak.minimum < -1 || peak.maximum > 1;
                p.setPen(over ? QColor(255, 80, 75) : QColor(225, 245, 255));
                const auto top = middle - std::clamp(peak.maximum, -1., 1.)*height;
                const auto bottom = middle - std::clamp(peak.minimum, -1., 1.)*height;
                if (right-left <= 2) p.drawLine(QPointF((left+right)/2,top),QPointF((left+right)/2,bottom));
                else p.fillRect(QRectF(left, top, right-left, std::max(1.,bottom-top)), p.pen().color());
            }
        }
        // Do not draw extrema from samples outside a trimmed/visible interval.
        const auto shade = [&](double left, double right) { if (right > left) p.fillRect(QRectF(left,area.top(),right-left,area.height()),QBrush(QColor(240,205,120),Qt::BDiagPattern)); };
        if (span.firstFullBin == span.endFullBin) shade(area.left(),area.right());
        else {
            if (span.needsFinerLeft) shade(area.left(),rect.left()+double(waveformBinProjectRange(wave,clip,session_->sampleRate,level,span.firstFullBin).first)*k);
            if (span.needsFinerRight) shade(rect.left()+double(waveformBinProjectRange(wave,clip,session_->sampleRate,level,span.endFullBin-1).second)*k,area.right());
        }
        p.restore(); saved = false;
    } catch (const ProjectError &) { if (saved) p.restore(); }
}
void TimelineWaveforms::retry() {
    if (closing_) return;
    items_.clear(); refused_=false;
    try { if (cache_) cache_->invalidateSources(); }
    catch (const std::exception &) { refused_=true; }
}
void TimelineWaveforms::shutdown() { closing_ = true; items_.clear(); if (cache_) cache_->requestShutdown(); }
bool TimelineWaveforms::closed() const { return !cache_ || cache_->closed(); }
} // namespace soundcurrent::daw::ui
