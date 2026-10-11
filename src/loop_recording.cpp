// SPDX-License-Identifier: GPL-3.0-only
#include <soundcurrent/loop_recording.hpp>
#include <algorithm>
#include <limits>
#include <set>

namespace soundcurrent::daw {
namespace {
void require(bool ok, const char *message) {
    if (!ok) throw ProjectError(ErrorCode::InvalidState, message);
}
Frame period(const LoopRecordingIntent &i) {
    require(i.sampleRate >= 8000 && i.sampleRate <= 384000 && i.generation &&
                i.streamBegin >= 0 && i.projectLoop.beginFrame >= 0 &&
                i.projectLoop.endFrame > i.projectLoop.beginFrame &&
                !i.tracks.empty() && i.tracks.size() <= 256 && (!i.passes || *i.passes),
            "Invalid loop recording intent");
    return i.projectLoop.endFrame - i.projectLoop.beginFrame;
}
const LoopRecordedSource &sourceFor(std::span<const LoopRecordedSource> sources, const Id &id) {
    const auto found = std::find_if(sources.begin(), sources.end(),
        [&](const auto &s) { return s.recording.spec.trackId == id; });
    require(found != sources.end(), "Missing loop recording arm");
    return *found;
}
} // namespace
LoopRecordingPlan prepareLoopRecording(const Session &s, std::vector<DuplexRecordingLane> lanes,
                                       DuplexRecordingOptions options,
                                       std::optional<std::uint64_t> passes) {
    validate(s);
    require(s.loop.enabled && !s.punch.enabled && !options.punch && !options.musicalPunch,
            "Loop recording needs an enabled loop and no conflicting punch");
    LoopRecordingIntent intent{s.id, s.sampleRate, options.playback.graph.generation,
        options.playback.graph.startFrame, {s.loop.startFrame, s.loop.endFrame}, passes, {}};
    Frame delay = 0;
    std::set<std::string> tracks, assets;
    for (auto &lane : lanes) {
        const auto &r = lane.spec;
        const auto t = std::find_if(s.tracks.begin(), s.tracks.end(),
            [&](const auto &v) { return v.id == r.trackId; });
        require(t != s.tracks.end() && r.projectId == s.id &&
                    r.capture.sampleRate == s.sampleRate && r.capture.layout == t->layout &&
                    r.inputLatencyFrames == t->inputLatencyFrames &&
                    r.inputLatencyFrames >= 0 && r.inputLatencyFrames <= Frame(s.sampleRate)*60 &&
                    !r.capture.deferredStart && !r.recoveredFrom &&
                    tracks.insert(r.trackId.str()).second && assets.insert(r.assetId.str()).second,
                "Invalid loop recording arm identity/rate/layout/latency");
        delay = std::max(delay, r.inputLatencyFrames);
        intent.tracks.push_back(r.trackId);
    }
    const auto length = period(intent);
    const auto limit = std::numeric_limits<Frame>::max() - delay;
    require(intent.streamBegin < limit, "Loop recording has no representable capture range");
    Frame end = limit;
    if (passes) {
        require(*passes <= std::uint64_t((limit-intent.streamBegin)/length),
                "Loop recording pass count overflows the stream clock");
        end = intent.streamBegin + Frame(*passes)*length;
    }
    options.playback.loop = intent.projectLoop;
    options.playback.endFrame = end;
    options.musicalPunch = PunchRange{intent.streamBegin,end};
    for (auto &lane : lanes)
        lane.spec.capture.startFrame = intent.streamBegin + lane.spec.inputLatencyFrames;
    // The duplex constructor reuses this same musical plan and admits the full
    // payload before any capture pool. Its native end includes every lane delay.
    (void)prepareMusicalPunch(*options.musicalPunch, s.sampleRate, lanes);
    return {std::move(intent),std::move(lanes),std::move(options)};
}
LoopRecordingExtent loopRecordingExtent(const LoopRecordingIntent &i,
                                        std::span<const LoopRecordedSource> sources) {
    const auto length = period(i);
    require(sources.size() == i.tracks.size(), "Loop recording source group is incomplete");
    const auto available = std::numeric_limits<Frame>::max() - i.streamBegin;
    if (i.passes)
        require(*i.passes <= std::uint64_t(available/length), "Loop recording intent extent overflows");
    std::set<std::string> tracks, assets;
    Frame common = available;
    for (const auto &track : i.tracks) {
        require(tracks.insert(track.str()).second, "Duplicate loop recording arm");
        const auto &source = sourceFor(sources,track);
        const auto &r = source.recording;
        (void)prepareCaptureConfig(r.spec.capture);
        validateRelativeMediaPath(r.asset.relativePath);
        require(r.spec.projectId == i.project && r.asset.id == r.spec.assetId &&
                    r.asset.sampleRate == i.sampleRate && r.spec.capture.sampleRate == i.sampleRate &&
                    r.asset.layout == r.spec.capture.layout && r.asset.frames > 0 &&
                    r.spec.inputLatencyFrames >= 0 && r.spec.inputLatencyFrames <= Frame(i.sampleRate)*60 &&
                    i.streamBegin <= std::numeric_limits<Frame>::max()-r.spec.inputLatencyFrames &&
                    r.spec.capture.startFrame == i.streamBegin+r.spec.inputLatencyFrames &&
                    !r.spec.capture.deferredStart && !r.spec.recoveredFrom &&
                    r.asset.frames <= std::numeric_limits<Frame>::max()-r.spec.capture.startFrame &&
                    source.origin.generation == i.generation &&
                    source.origin.backend != CaptureBackend::Unknown &&
                    std::uint32_t(source.origin.backend) <= std::uint32_t(CaptureBackend::Asio) &&
                    source.origin.rateNumerator == 1 && source.origin.rateDenominator == i.sampleRate &&
                    source.endReason != CaptureEndReason::Unknown &&
                    std::uint32_t(source.endReason) <= std::uint32_t(CaptureEndReason::RecoveredCheckpoint) &&
                    std::uint32_t(r.captureStatus) <= std::uint32_t(CaptureStatus::WriterFailed) &&
                    r.captureStatus != CaptureStatus::Running &&
                    r.asset.sha256.size() == 64 &&
                    std::all_of(r.asset.sha256.begin(),r.asset.sha256.end(),[](char c) {
                        return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f');
                    }) &&
                    (!i.passes || r.asset.frames <= Frame(*i.passes)*length) &&
                    assets.insert(r.asset.id.str()).second,
                "Loop raw source alignment/identity/origin mismatch");
        common = std::min(common,r.asset.frames);
    }
    return {common,common%length,std::uint64_t(common/length)};
}
std::optional<LoopRecordingWindow> loopRecordingWindow(const LoopRecordingIntent &i,
    std::span<const LoopRecordedSource> sources,std::uint64_t pass) {
    const auto extent = loopRecordingExtent(i,sources);
    if (pass >= extent.windows()) return {};
    const auto length = period(i);
    const auto offset = Frame(pass)*length; // pass < windows bounds multiplication.
    const auto frames = std::min(length,extent.commonFrames-offset);
    LoopRecordingWindow result{pass,i.streamBegin+offset,i.streamBegin+offset+frames,
                               i.projectLoop.beginFrame,frames!=length,{}};
    result.sources.reserve(i.tracks.size());
    for (const auto &track : i.tracks) {
        const auto &raw = sourceFor(sources,track);
        result.sources.push_back({track,raw.recording.asset.id,offset,frames,raw.origin});
    }
    return result;
}
Session withLoopTakeLanes(const Session &s,const LoopRecordingIntent &i,
    std::span<const LoopRecordedSource> sources,std::uint64_t first,std::uint64_t count,
    std::string prefix,bool allowPartial,StateBudget budget) {
    validate(s,budget);
    require(s.id == i.project && s.sampleRate == i.sampleRate && validUtf8(prefix) &&
                !prefix.empty() && prefix.size() <= 4096 && prefix.find('\0') == std::string::npos,
            "Loop take candidate project/name mismatch");
    const auto extent = loopRecordingExtent(i,sources);
    require(count && first < extent.windows() && count <= extent.windows()-first,
            "Loop take page is outside the captured extent");
    require(allowPartial || first+count <= extent.completePasses,
            "Partial loop pass requires explicit retention");
    require(count <= SIZE_MAX, "Loop take page exceeds addressable size");
    PayloadCharge charge("Loop take page staging",budget.memoryBudgetBytes);
    charge.add(sessionPayloadBytes(s,budget));
    charge.add(sources.size(),sizeof(Asset)+8192);
    charge.add(std::size_t(count),sources.size()*(sizeof(TakeLane)+sizeof(Clip)+2048+prefix.size()));
    auto copy = s;
    copy.assets.reserve(copy.assets.size()+sources.size());
    for (const auto &raw : sources) {
        const auto &r = raw.recording;
        const auto t = std::find_if(copy.tracks.begin(),copy.tracks.end(),
            [&](const auto &v) { return v.id == r.spec.trackId; });
        require(t != copy.tracks.end() && t->layout == r.asset.layout,
                "Loop take track is missing or changed layout");
        const auto existing = std::find_if(copy.assets.begin(),copy.assets.end(),
            [&](const auto &v) { return v.id == r.asset.id; });
        if (existing == copy.assets.end()) copy.assets.push_back(r.asset);
        else require(*existing == r.asset, "Loop raw asset identity was rebound");
        require(std::size_t(count) <= SIZE_MAX-t->takeLanes.size(), "Loop take page size overflows");
        t->takeLanes.reserve(t->takeLanes.size()+std::size_t(count));
    }
    for (std::uint64_t pass=first;pass<first+count;++pass) {
        const auto window = *loopRecordingWindow(i,sources,pass);
        const auto group = Id::generate();
        for (const auto &view : window.sources) {
            const auto t = std::find_if(copy.tracks.begin(),copy.tracks.end(),
                [&](const auto &v) { return v.id == view.track; });
            TakeLane lane;lane.groupId=group;lane.name=prefix+std::to_string(pass+1);
            lane.captureStartFrame=window.streamBegin;lane.captureEndFrame=window.streamEnd;
            Clip clip;clip.assetId=view.asset;clip.sourceFrame=view.sourceFrame;
            clip.startFrame=window.projectBegin;clip.lengthFrames=view.frames;
            lane.clips.push_back(std::move(clip));t->takeLanes.push_back(std::move(lane));
        }
    }
    validate(copy,budget);
    return copy;
}
} // namespace soundcurrent::daw
