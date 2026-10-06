// SPDX-License-Identifier: GPL-3.0-only
#include <soundcurrent/mix.hpp>
#include <soundcurrent/rt_object_exchange.hpp>
#include "rt_audit.hpp"
#include <algorithm>
#include <cmath>
#include <iostream>
#include <limits>
#include <numbers>
#include <stdexcept>
using namespace soundcurrent::daw;
namespace {
unsigned checks = 0;
double oracleDifference = 0, peak = 0;
void check(bool ok, const char *message) {
    ++checks;
    if (!ok)
        throw std::runtime_error(message);
}
template <class F> void rejects(F f) {
    bool caught = false;
    try {
        f();
    } catch (const ProjectError &) {
        caught = true;
    }
    check(caught, "Invalid mix admission accepted");
}
Session session(unsigned count = 2) {
    auto s = makeOneTrackSession("Mix — Ελλάδα", "Track 1");
    while (s.tracks.size() < count)
        s.tracks.push_back(makeAudioTrack("Track", {}, s.sampleRate));
    for (auto &t : s.tracks) {
        t.eq.bands.resize(1);
        t.eq.bands[0].frequencyHz = 1000;
    }
    return s;
}
MixPlan stereo(const Session &s) {
    MixPlan p{{LayoutKind::Stereo, 2}, {}};
    for (unsigned t = 0; t < s.tracks.size(); ++t)
        p.tracks.push_back({s.tracks[t].id, {{0, 0, 1.}, {0, 1, t == 0 ? -.5 : 2.}}});
    return p;
}
MixReport audited(PreparedMixGraph &g, std::span<const MixInput> in, std::span<float *const> out,
                  unsigned n) {
    rt_audit::Guard guard;
    return g.process(in, out, n);
}
struct Oracle {
    double b0, b1, b2, a1, a2, x1 = 0, x2 = 0, y1 = 0, y2 = 0;
    Oracle(double gain, double frequency, double q) {
        const double a = std::pow(10., gain / 40.), w = 2 * std::numbers::pi * frequency / 48000.,
                     alpha = std::sin(w) / (2 * q), d = 1 + alpha / a;
        b0 = (1 + alpha * a) / d;
        b1 = -2 * std::cos(w) / d;
        b2 = (1 - alpha * a) / d;
        a1 = b1;
        a2 = (1 - alpha / a) / d;
    }
    float process(float x) {
        const double y = b0 * x + b1 * x1 + b2 * x2 - a1 * y1 - a2 * y2;
        x2 = x1;
        x1 = x;
        y2 = y1;
        y1 = y;
        return static_cast<float>(y);
    }
};
void quality() {
    auto s = session();
    s.tracks[0].eq.bands[0].gainDb = 9;
    s.tracks[1].eq.bands[0].gainDb = -6;
    s.tracks[1].eq.bands[0].q = 1.7;
    PreparedMixGraph g(s, stereo(s), {.maximumFrames = 127});
    std::array<std::vector<float>, 2> input{std::vector<float>(8192), std::vector<float>(8192)};
    for (unsigned f = 0; f < 8192; ++f) {
        input[0][f] = static_cast<float>(1.4 * std::sin(2 * std::numbers::pi * 1000 * f / 48000.));
        input[1][f] = static_cast<float>(.7 * std::sin(2 * std::numbers::pi * 317 * f / 48000.));
    }
    Oracle a(9, 1000, 1), b(-6, 1000, 1.7);
    std::array<std::vector<float>, 2> expected{std::vector<float>(8192), std::vector<float>(8192)};
    for (unsigned f = 0; f < 8192; ++f) {
        const auto x = a.process(input[0][f]), y = b.process(input[1][f]);
        expected[0][f] = static_cast<float>(double(x) + y);
        expected[1][f] = static_cast<float>(-.5 * double(x) + 2 * double(y));
    }
    std::array<std::array<float, 127>, 2> output{};
    for (unsigned at = 0; at < 8192;) {
        const auto n = std::min({8192 - at, 127u, 1u + (at * 17u) % 127u});
        const float *x = input[0].data() + at, *y = input[1].data() + at;
        std::array<MixInput, 2> in{MixInput(&x, 1), MixInput(&y, 1)};
        std::array<float *, 2> out{output[0].data(), output[1].data()};
        const auto r = audited(g, in, out, n);
        check(r.status == ProcessStatus::Ok && r.startFrame == at && r.frames == n,
              "Mix clock/process failed");
        peak = std::max(peak, r.peak);
        for (unsigned c = 0; c < 2; ++c)
            for (unsigned f = 0; f < n; ++f)
                oracleDifference = std::max(oracleDifference,
                                            std::abs(double(output[c][f]) - expected[c][at + f]));
        at += n;
    }
    check(oracleDifference < 2e-6 && peak > 2,
          "Independent DFI/matrix oracle or float headroom failed");
}
void limitsAndEvents() {
    auto s = session();
    const auto p = stereo(s);
    rejects([&] {
        auto bad = p;
        bad.tracks[1].track = bad.tracks[0].track;
        PreparedMixGraph g(s, bad);
    });
    rejects([&] {
        auto bad = p;
        bad.tracks[0].channels.push_back(bad.tracks[0].channels[0]);
        PreparedMixGraph g(s, bad);
    });
    rejects([&] {
        auto bad = p;
        bad.tracks[0].channels[0].gain = INFINITY;
        PreparedMixGraph g(s, bad);
    });
    rejects([&] {
        auto bad = p;
        bad.tracks[0].channels[0].source = 1;
        PreparedMixGraph g(s, bad);
    });
    rejects([&] { PreparedMixGraph g(s, p, {.memoryBudgetBytes = 8192}); });
    rejects([&] { PreparedMixGraph g(s, p, {.maximumRoutingEntries = 1}); });
    rejects([&] { PreparedMixGraph g(s, p, {.generation = 0}); });
    rejects([&] {
        auto bad = p;
        bad.output = {LayoutKind::Mono, 2};
        PreparedMixGraph g(s, bad);
    });
    rejects([&] {
        std::array<Id, 1> ids{s.tracks[0].id};
        identityMix(s, ids, {LayoutKind::Stereo, 2});
    });
    PreparedMixGraph g(s, p, {.maximumFrames = 64, .generation = 7});
    auto event = g.prepared(1).enableEvent(false, 13);
    check(g.submit(1, event) == SubmitStatus::Accepted &&
              g.submit(4, event) == SubmitStatus::Invalid,
          "Lane event admission failed");
    event.generation = 6;
    check(g.submit(0, event) == SubmitStatus::Invalid, "Stale generation accepted");
    const float x = 1, y = .25;
    std::array<float, 64> a{}, b{}, left{}, right{};
    a.fill(x);
    b.fill(y);
    const float *ia = a.data(), *ib = b.data();
    std::array<MixInput, 2> in{MixInput(&ia, 1), MixInput(&ib, 1)};
    std::array<float *, 2> out{left.data(), right.data()};
    check(audited(g, {}, out, 64).status == ProcessStatus::InvalidBuffer && g.position() == 0,
          "Bad input advanced clock");
    const auto r = audited(g, in, out, 64);
    check(r.eventsApplied == 1 && g.position() == 64, "Sample-timed event/clock failed");
    auto updated = s;
    updated.tracks[0].eq.bands[0].gainDb = 3;
    ParameterAddress address{s.tracks[0].id, s.tracks[0].eq.id, s.tracks[0].eq.bands[0].id,
                             BandParameter::GainDb};
    auto reversed = p;
    std::reverse(reversed.tracks.begin(), reversed.tracks.end());
    PreparedMixGraph reordered(s, reversed, {.maximumFrames = 64, .generation = 9});
    const auto bound = reordered.parameterEvent(updated, address, 11);
    check(bound.track == 1 && bound.event.generation == 9 &&
              reordered.enableEvent(s.tracks[1].id, false, 11).track == 0 &&
              reordered.submit(bound) == SubmitStatus::Accepted,
          "Stable parameter IDs followed ordinal instead of track identity");
    event = g.prepared(0).parameterEvent(updated, address, 0);
    check(g.submitImmediate(0, event, 1) == SubmitStatus::Accepted &&
              g.submitImmediate(0, event, 1) == SubmitStatus::OutOfOrder,
          "Immediate lane revision not bounded");
    check(audited(g, in, out, 17).eventsApplied == 1, "Immediate edit not applied");
    ImmediateAcknowledgement receipt;
    check(g.acknowledgement(0, receipt) && receipt.generation == 7 && receipt.frame == 64 &&
              receipt.revision == 1,
          "Immediate lane receipt failed");
    PreparedMixGraph overflow(s, p, {.maximumFrames = 64, .startFrame = INT64_MAX - 1});
    check(audited(overflow, in, out, 2).status == ProcessStatus::TimingError &&
              overflow.position() == INT64_MAX - 1 && left[0] == 0,
          "Timing overflow unsafe");
    check(audited(overflow, in, out, 1).status == ProcessStatus::TimingError,
          "Timing error not sticky");
    PreparedMixGraph sanit(s, p, {.maximumFrames = 64});
    a[0] = NAN;
    check(audited(sanit, in, out, 1).invalidInputSamples == 1 && std::isfinite(left[0]),
          "Nonfinite input not reported");
    auto huge = p;
    huge.tracks[0].channels[0].gain = 64;
    a[0] = std::numeric_limits<float>::max();
    b[0] = 0;
    PreparedMixGraph numeric(s, huge, {.maximumFrames = 64});
    const auto bad = audited(numeric, in, out, 1);
    check(bad.status == ProcessStatus::Stopped && bad.numericFaultSamples && left[0] == 0 &&
              right[0] == 0,
          "Numeric overflow published bad output");
}
void scaleAndRetirement() {
    auto mixed = session();
    mixed.tracks[0].layout = {LayoutKind::Stereo, 2};
    MixPlan matrix{{LayoutKind::Stereo, 2},
                   {{mixed.tracks[0].id, {{0, 1, .5}, {1, 0, 2}}},
                    {mixed.tracks[1].id, {{0, 0, 1}, {0, 1, -.5}}}}};
    PreparedMixGraph hybrid(mixed, matrix, {.maximumFrames = 1});
    const float l = 1, r = .5, m = .25;
    std::array<const float *, 2> pair{&l, &r};
    const float *mono = &m;
    std::array<MixInput, 2> buses{pair, MixInput(&mono, 1)};
    float a = 0, b = 0;
    std::array<float *, 2> destinations{&a, &b};
    check(audited(hybrid, buses, destinations, 1).status == ProcessStatus::Ok && a == 1.25f &&
              b == .375f,
          "Explicit stereo/mono channel matrix lost or guessed a channel");
    auto s = session(32);
    std::vector<Id> ids;
    for (const auto &t : s.tracks)
        ids.push_back(t.id);
    const auto p = identityMix(s, ids, {});
    PreparedMixGraph g(s, p, {.maximumFrames = 64});
    std::array<float, 64> raw{}, output{};
    raw.fill(.125f);
    const float *ptr = raw.data();
    float *out = output.data();
    std::vector<MixInput> inputs(32, MixInput(&ptr, 1));
    for (unsigned n = 0; n < 100; ++n)
        check(audited(g, inputs, {&out, 1}, 64).status == ProcessStatus::Ok,
              "32-track clock failed");
    check(output[0] == 4 && g.position() == 6400, "32-track summing clipped/drifted");
    for (unsigned channels : {1u, 2u, 8u, 32u, 256u}) {
        auto model = session(1);
        model.tracks[0].layout = {LayoutKind::Discrete, channels};
        std::array<Id, 1> track{model.tracks[0].id};
        PreparedMixGraph layout(model, identityMix(model, track, model.tracks[0].layout),
                                {.maximumFrames = 1});
        std::vector<float> values(channels, .125f), rendered(channels);
        std::vector<const float *> read(channels);
        std::vector<float *> write(channels);
        for (unsigned c = 0; c < channels; ++c) {
            read[c] = &values[c];
            write[c] = &rendered[c];
        }
        std::array<MixInput, 1> input{read};
        check(audited(layout, input, write, 1).status == ProcessStatus::Ok && values == rendered,
              "Discrete channel lost");
    }
    RtObjectExchange<PreparedMixGraph> exchange(
        std::make_unique<PreparedMixGraph>(s, p, MixConfig{.maximumFrames = 64}));
    auto next =
        std::make_unique<PreparedMixGraph>(s, p, MixConfig{.maximumFrames = 64, .generation = 2});
    check(exchange.publish(next), "Graph publication failed");
    bool switched = false, retired = false;
    {
        rt_audit::Guard guard;
        switched = exchange.beginReplacement();
        exchange.active().process(inputs, {&out, 1}, 64);
        retired = exchange.finishReplacement();
    }
    check(switched && retired && exchange.collectRetired() == 1, "Graph retirement failed");
}
} // namespace
int main() {
    try {
        rt_audit::reset();
        quality();
        limitsAndEvents();
        scaleAndRetirement();
        const auto c = rt_audit::counts;
        check(c.cppAllocate + c.cppFree + c.cAllocate + c.cFree + c.blockingLock == 0,
              "RT allocation/free/lock detected");
        std::cout << "{\"checks\":" << checks << ",\"oracle_difference\":" << oracleDifference
                  << ",\"peak\":" << peak << ",\"rt_violations\":0}\n";
        return 0;
    } catch (const std::exception &e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
