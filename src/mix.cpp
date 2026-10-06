// SPDX-License-Identifier: GPL-3.0-only
#include <soundcurrent/mix.hpp>
#include <algorithm>
#include <cmath>
#include <limits>
#include <set>

namespace soundcurrent::daw {
namespace {
void require(bool ok, const char *message) {
    if (!ok)
        throw ProjectError(ErrorCode::InvalidState, message);
}
const Track &find(const Session &s, const Id &id) {
    const auto it =
        std::find_if(s.tracks.begin(), s.tracks.end(), [&](const auto &t) { return t.id == id; });
    require(it != s.tracks.end(), "Mix track is missing");
    return *it;
}
void add(std::size_t &bytes, std::size_t count, std::size_t unit) {
    require(count <= (SIZE_MAX - bytes) / unit, "Mix payload size overflow");
    bytes += count * unit;
}
} // namespace
MixPlan identityMix(const Session &s, std::span<const Id> tracks, ChannelLayout output) {
    MixPlan p{output, {}};
    for (const auto &id : tracks) {
        const auto &t = find(s, id);
        require(t.layout == output, "Mixed layouts require an explicit channel matrix");
        TrackMix lane{id, {}};
        for (std::uint32_t c = 0; c < output.channels; ++c)
            lane.channels.push_back({c, c, 1});
        p.tracks.push_back(std::move(lane));
    }
    return p;
}
std::size_t mixPayloadBytes(const Session &s, const MixPlan &p, const MixConfig &c) {
    validate(s);
    require(!p.tracks.empty() && p.tracks.size() <= 256 && c.maximumFrames &&
                c.maximumFrames <= 65536 && c.startFrame >= 0 && c.generation &&
                c.maximumRoutingEntries && p.output.channels && p.output.channels <= 256 &&
                ((p.output.kind == LayoutKind::Mono && p.output.channels == 1) ||
                 (p.output.kind == LayoutKind::Stereo && p.output.channels == 2) ||
                 p.output.kind == LayoutKind::Discrete),
            "Invalid mix configuration/layout");
    std::size_t bytes = 4096, entries = 0;
    add(bytes, std::size_t(c.maximumFrames) * p.output.channels, sizeof(double));
    add(bytes, c.maximumFrames, sizeof(float));
    std::set<std::string> ids;
    for (const auto &route : p.tracks) {
        require(ids.insert(route.track.str()).second, "Duplicate mix track");
        const auto &t = find(s, route.track);
        require(!route.channels.empty() &&
                    route.channels.size() <= c.maximumRoutingEntries - entries,
                "Mix routing entry budget exceeded or empty track route");
        entries += route.channels.size();
        std::set<std::pair<std::uint32_t, std::uint32_t>> pairs;
        for (const auto &m : route.channels)
            require(m.source < t.layout.channels && m.destination < p.output.channels &&
                        std::isfinite(m.gain) && std::abs(m.gain) <= 64 &&
                        pairs.emplace(m.source, m.destination).second,
                    "Invalid or duplicate mix channel route");
        add(bytes, route.channels.size(), sizeof(ChannelMix));
        add(bytes, 1, sizeof(PreparedEq) + sizeof(EqLiveDriver) + 8192);
        add(bytes, std::size_t(c.maximumFrames) * t.layout.channels, sizeof(float));
        add(bytes, t.eq.bands.size(), 256 + std::size_t(t.layout.channels) * 16);
    }
    require(bytes <= c.memoryBudgetBytes, "Mix DSP payload exceeds memory admission");
    return bytes;
}
struct PreparedMixGraph::State {
    struct Lane {
        PreparedEq eq;
        EqLiveDriver driver;
        std::vector<float> wet;
        std::array<float *, 256> views{};
        Lane(const Session &s, const Id &id, const MixConfig &c)
            : eq(s, id, c.maximumFrames, c.generation), driver(eq, c.startFrame),
              wet(std::size_t(eq.channels()) * c.maximumFrames, 0.f) {
            for (std::uint32_t n = 0; n < eq.channels(); ++n)
                views[n] = wet.data() + std::size_t(n) * c.maximumFrames;
        }
    };
    MixPlan plan;
    MixConfig config;
    std::vector<std::unique_ptr<Lane>> lanes;
    std::vector<double> sum;
    std::vector<float> silence;
    std::array<const float *, 256> silentViews{};
    std::vector<MixInput> silentInputs;
    Frame frame;
    std::atomic<Frame> published;
    ProcessStatus terminal = ProcessStatus::Ok;
    State(MixPlan p, MixConfig c)
        : plan(std::move(p)), config(c), frame(c.startFrame), published(frame) {}
};
PreparedMixGraph::PreparedMixGraph(const Session &s, MixPlan p, MixConfig c) {
    mixPayloadBytes(s, p, c);
    state_ = std::make_unique<State>(std::move(p), c);
    auto &v = *state_;
    v.sum.resize(std::size_t(c.maximumFrames) * v.plan.output.channels, 0.);
    v.silence.resize(c.maximumFrames, 0.f);
    v.silentViews.fill(v.silence.data());
    for (const auto &t : v.plan.tracks) {
        auto lane = std::make_unique<State::Lane>(s, t.track, c);
        v.silentInputs.emplace_back(v.silentViews.data(), lane->eq.channels());
        v.lanes.push_back(std::move(lane));
    }
}
PreparedMixGraph::~PreparedMixGraph() = default;
MixReport PreparedMixGraph::process(std::span<const MixInput> input, std::span<float *const> output,
                                    std::uint32_t n) noexcept {
    auto &s = *state_;
    MixReport r;
    r.startFrame = s.frame;
    if (!n || n > s.config.maximumFrames || output.size() != s.plan.output.channels ||
        input.size() != s.lanes.size() ||
        std::any_of(output.begin(), output.end(), [](auto *p) { return !p; })) {
        r.status = ProcessStatus::InvalidBuffer;
        return r;
    }
    for (std::size_t t = 0; t < input.size(); ++t)
        if (input[t].size() != s.lanes[t]->eq.channels() ||
            std::any_of(input[t].begin(), input[t].end(), [](auto *p) { return !p; })) {
            r.status = ProcessStatus::InvalidBuffer;
            return r;
        }
    for (auto *p : output)
        std::fill_n(p, n, 0.f);
    if (s.terminal != ProcessStatus::Ok) {
        r.status = s.terminal;
        return r;
    }
    if (s.frame > std::numeric_limits<Frame>::max() - n) {
        s.terminal = r.status = ProcessStatus::TimingError;
        return r;
    }
    for (std::uint32_t c = 0; c < s.plan.output.channels; ++c)
        std::fill_n(s.sum.data() + std::size_t(c) * s.config.maximumFrames, n, 0.);
    for (std::size_t t = 0; t < input.size(); ++t) {
        auto &lane = *s.lanes[t];
        const auto e = lane.driver.process(input[t], {lane.views.data(), lane.eq.channels()}, n);
        r.eventsApplied += e.eventsApplied;
        r.invalidInputSamples += e.invalidInputSamples;
        r.numericFaultSamples += e.numericFaultSamples;
        if (e.status != ProcessStatus::Ok) {
            s.terminal = r.status = e.status;
            return r;
        }
        for (const auto &map : s.plan.tracks[t].channels) {
            auto *sum = s.sum.data() + std::size_t(map.destination) * s.config.maximumFrames;
            const auto *wet = lane.views[map.source];
            for (std::uint32_t f = 0; f < n; ++f)
                sum[f] += double(wet[f]) * map.gain;
        }
    }
    for (std::uint32_t c = 0; c < s.plan.output.channels; ++c)
        for (std::uint32_t f = 0; f < n; ++f) {
            const auto v = static_cast<float>(s.sum[std::size_t(c) * s.config.maximumFrames + f]);
            if (!std::isfinite(v)) {
                ++r.numericFaultSamples;
                s.terminal = ProcessStatus::Stopped;
            } else
                r.peak = std::max(r.peak, std::abs(double(v)));
        }
    if (s.terminal != ProcessStatus::Ok) {
        r.status = s.terminal;
        return r;
    }
    for (std::uint32_t c = 0; c < s.plan.output.channels; ++c)
        for (std::uint32_t f = 0; f < n; ++f)
            output[c][f] = static_cast<float>(s.sum[std::size_t(c) * s.config.maximumFrames + f]);
    s.frame += n;
    s.published.store(s.frame, std::memory_order_release);
    r.frames = n;
    return r;
}
MixReport PreparedMixGraph::processSilence(std::span<float *const> out, std::uint32_t n) noexcept {
    return process(state_->silentInputs, out, n);
}
void PreparedMixGraph::stop() noexcept {
    state_->terminal = ProcessStatus::Stopped;
}
Frame PreparedMixGraph::position() const noexcept {
    return state_->published.load(std::memory_order_acquire);
}
const MixPlan &PreparedMixGraph::plan() const noexcept {
    return state_->plan;
}
const MixConfig &PreparedMixGraph::config() const noexcept {
    return state_->config;
}
PreparedEq &PreparedMixGraph::prepared(std::size_t t) {
    if (t >= state_->lanes.size())
        throw ProjectError(ErrorCode::InvalidParameter, "Mix lane is missing");
    return state_->lanes[t]->eq;
}
SubmitStatus PreparedMixGraph::submit(std::size_t t, const EqEvent &e) noexcept {
    return t < state_->lanes.size() ? state_->lanes[t]->driver.submit(e) : SubmitStatus::Invalid;
}
MixEvent PreparedMixGraph::parameterEvent(const Session &s, const ParameterAddress &a,
                                          Frame at) const {
    for (std::size_t n = 0; n < state_->plan.tracks.size(); ++n)
        if (state_->plan.tracks[n].track == a.trackId)
            return {n, state_->lanes[n]->eq.parameterEvent(s, a, at)};
    throw ProjectError(ErrorCode::InvalidParameter, "Parameter track is not in this mix");
}
MixEvent PreparedMixGraph::enableEvent(const Id &id, bool enabled, Frame at) const {
    for (std::size_t n = 0; n < state_->plan.tracks.size(); ++n)
        if (state_->plan.tracks[n].track == id)
            return {n, state_->lanes[n]->eq.enableEvent(enabled, at)};
    throw ProjectError(ErrorCode::InvalidParameter, "Enable target is not in this mix");
}
SubmitStatus PreparedMixGraph::submitImmediate(std::size_t t, const EqEvent &e,
                                               std::uint64_t rev) noexcept {
    return t < state_->lanes.size() ? state_->lanes[t]->driver.submitImmediate(e, rev)
                                    : SubmitStatus::Invalid;
}
bool PreparedMixGraph::acknowledgement(std::size_t t, ImmediateAcknowledgement &a) noexcept {
    return t < state_->lanes.size() && state_->lanes[t]->driver.acknowledgement(a);
}
} // namespace soundcurrent::daw
