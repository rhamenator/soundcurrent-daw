// SPDX-License-Identifier: GPL-3.0-only
#include <soundcurrent/mix_playback.hpp>
#include <algorithm>

namespace soundcurrent::daw {
namespace {
PlaybackConfig configFor(const Session &s, const Id &id, const MixPlaybackConfig &c) {
    const auto it =
        std::find_if(s.tracks.begin(), s.tracks.end(), [&](const auto &t) { return t.id == id; });
    if (it == s.tracks.end())
        throw ProjectError(ErrorCode::InvalidState, "Playback mix track missing");
    PlaybackConfig p;
    p.sampleRate = s.sampleRate;
    p.layout = it->layout;
    p.maximumCallbackFrames = c.graph.maximumFrames;
    p.slabFrames = c.slabFrames;
    p.startFrame = c.graph.startFrame;
    p.endFrame = c.endFrame;
    p.generation = c.graph.generation;
    p.memoryBudgetBytes = std::min<std::size_t>(c.graph.memoryBudgetBytes, 256 * 1024 * 1024);
    return preparePlaybackConfig(p);
}
} // namespace
std::size_t mixPlaybackPayloadBytes(const Session &s, const MixPlan &p,
                                    const MixPlaybackConfig &c) {
    auto bytes = mixPayloadBytes(s, p, c.graph);
    for (const auto &t : p.tracks) {
        const auto pipe = configFor(s, t.track, c);
        // Pipes + planar raw scratch + disk float/double decode buffers. Binding/file
        // metadata is separately bounded by MixReader, with additional payload checks.
        const auto samples =
            std::size_t(pipe.layout.channels) *
            (std::size_t(captureSlabs + 3) * pipe.slabFrames + c.graph.maximumFrames);
        if (bytes > SIZE_MAX - 8192 || samples > (SIZE_MAX - bytes - 8192) / sizeof(float))
            throw ProjectError(ErrorCode::InvalidState, "Playback mix payload overflow");
        bytes += samples * sizeof(float);
        bytes += 8192;
    }
    if (bytes > c.graph.memoryBudgetBytes)
        throw ProjectError(ErrorCode::InvalidState,
                           "Playback mix buffers exceed total memory admission");
    return bytes;
}
struct MixPlayback::State {
    struct Lane {
        PlaybackPipe pipe;
        std::vector<float> raw;
        std::array<float *, 256> write{};
        std::array<const float *, 256> read{};
        PlaybackReport report;
        Lane(PlaybackConfig c)
            : pipe(c), raw(std::size_t(c.maximumCallbackFrames) * c.layout.channels, 0.f) {
            for (std::uint32_t n = 0; n < c.layout.channels; ++n) {
                write[n] = raw.data() + std::size_t(n) * c.maximumCallbackFrames;
                read[n] = write[n];
            }
        }
    };
    MixPlaybackConfig config;
    PreparedMixGraph graph;
    std::vector<std::unique_ptr<Lane>> lanes;
    std::vector<MixInput> inputs;
    PlaybackStatus terminal = PlaybackStatus::Running;
    std::atomic<std::uint64_t> missing{0};
    State(const Session &s, MixPlan p, MixPlaybackConfig c)
        : config(c), graph(s, std::move(p), c.graph) {}
};
MixPlayback::MixPlayback(const Session &s, MixPlan p, MixPlaybackConfig c) {
    mixPlaybackPayloadBytes(s, p, c);
    state_ = std::make_unique<State>(s, std::move(p), c);
    for (const auto &route : state_->graph.plan().tracks) {
        auto lane = std::make_unique<State::Lane>(configFor(s, route.track, c));
        state_->inputs.emplace_back(lane->read.data(), lane->pipe.config().layout.channels);
        state_->lanes.push_back(std::move(lane));
    }
}
MixPlayback::~MixPlayback() = default;
MixPlaybackReport MixPlayback::process(std::span<float *const> out, std::uint32_t n,
                                       std::span<const LiveMixInput> live) noexcept {
    auto &s = *state_;
    MixPlaybackReport r;
    r.startFrame = s.graph.position();
    if (!n || n > s.config.graph.maximumFrames || out.size() != s.graph.plan().output.channels ||
        std::any_of(out.begin(), out.end(), [](auto *p) { return !p; })) {
        r.status = PlaybackStatus::InvalidBuffer;
        return r;
    }
    for (std::size_t c = 0; c < out.size(); ++c)
        for (std::size_t prior = 0; prior < c; ++prior)
            if (out[c] == out[prior]) {
                r.status = PlaybackStatus::InvalidBuffer;
                return r;
            }
    // Validate before consuming any file pipe or moving the common cursor.
    std::array<bool, 256> replaced{};
    for (const auto &replacement : live) {
        if (replacement.track >= s.lanes.size() || replaced[replacement.track] ||
            replacement.input.size() != s.lanes[replacement.track]->pipe.config().layout.channels ||
            std::any_of(replacement.input.begin(), replacement.input.end(),
                        [](auto *p) { return !p; })) {
            r.status = PlaybackStatus::InvalidBuffer;
            return r;
        }
        replaced[replacement.track] = true;
    }
    const auto silence = [&] {
        for (auto *p : out)
            std::fill_n(p, n, 0.f);
    };
    if (s.terminal != PlaybackStatus::Running) {
        r.status = s.terminal;
        silence();
        return r;
    }
    const auto frames =
        static_cast<std::uint32_t>(std::min<Frame>(n, s.config.endFrame - r.startFrame));
    if (!frames) {
        s.terminal = r.status = PlaybackStatus::Complete;
        silence();
        return r;
    }
    for (auto &lane : s.lanes) {
        auto &report = lane->report = lane->pipe.render(
            {lane->write.data(), lane->pipe.config().layout.channels}, frames, r.startFrame);
        r.missingTrackFrames += report.missingFrames;
        r.staleTrackFrames += report.staleFrames;
        r.underflowTracks += report.missingFrames != 0;
        if ((report.status != PlaybackStatus::Running &&
             report.status != PlaybackStatus::Underflow &&
             report.status != PlaybackStatus::Complete) ||
            report.timelineFrames != frames) {
            s.terminal = r.status = report.status;
            if (r.status == PlaybackStatus::Running)
                s.terminal = r.status = PlaybackStatus::TimingError;
            for (auto &l : s.lanes)
                l->pipe.stop();
            s.graph.stop();
            silence();
            return r;
        }
    }
    // File pipes still consume this block, preserving their shared offset and
    // failure accounting. Copy every live plane before clearing outputs, so a
    // native in-place view cannot erase another lane's input.
    for (const auto &replacement : live) {
        auto &lane = *s.lanes[replacement.track];
        for (std::size_t c = 0; c < replacement.input.size(); ++c)
            std::copy_n(replacement.input[c], frames, lane.write[c]);
    }
    silence();
    r.mix = s.graph.process(s.inputs, out, frames);
    if (r.mix.status != ProcessStatus::Ok) {
        s.terminal = r.status = PlaybackStatus::ProcessorFailed;
        for (auto &l : s.lanes)
            l->pipe.stop();
        return r;
    }
    r.timelineFrames = frames;
    const auto prior = s.missing.load(std::memory_order_relaxed);
    s.missing.store(prior + std::min(r.missingTrackFrames, UINT64_MAX - prior),
                    std::memory_order_release);
    r.status = s.graph.position() == s.config.endFrame ? PlaybackStatus::Complete
               : r.underflowTracks                     ? PlaybackStatus::Underflow
                                                       : PlaybackStatus::Running;
    if (r.status == PlaybackStatus::Complete)
        s.terminal = r.status;
    return r;
}
void MixPlayback::stop() noexcept {
    state_->terminal = PlaybackStatus::Stopped;
    state_->graph.stop();
    for (auto &l : state_->lanes)
        l->pipe.stop();
}
PreparedMixGraph &MixPlayback::graph() noexcept {
    return state_->graph;
}
PlaybackPipe &MixPlayback::pipe(std::size_t t) {
    if (t >= state_->lanes.size())
        throw ProjectError(ErrorCode::InvalidParameter, "Playback mix lane missing");
    return state_->lanes[t]->pipe;
}
const PlaybackReport &MixPlayback::laneReport(std::size_t t) const {
    if (t >= state_->lanes.size())
        throw ProjectError(ErrorCode::InvalidParameter, "Playback mix lane missing");
    return state_->lanes[t]->report;
}
const MixPlaybackConfig &MixPlayback::config() const noexcept {
    return state_->config;
}
Frame MixPlayback::position() const noexcept {
    return state_->graph.position();
}
bool MixPlayback::readerDone() const noexcept {
    return std::all_of(state_->lanes.begin(), state_->lanes.end(),
                       [](const auto &l) { return l->pipe.readerDone(); });
}
std::uint64_t MixPlayback::missingTrackFrames() const noexcept {
    return state_->missing.load(std::memory_order_acquire);
}
} // namespace soundcurrent::daw
