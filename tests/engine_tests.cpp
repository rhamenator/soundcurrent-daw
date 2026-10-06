// SPDX-License-Identifier: GPL-3.0-only
#include <soundcurrent/eq.hpp>
#include <soundcurrent/rt_object_exchange.hpp>
#include "rt_audit.hpp"
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <numbers>
#include <stdexcept>
#ifndef _WIN32
#include <thread>
#else
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif
using namespace soundcurrent::daw;
namespace {
int checks = 0;
double maximumGainError = 0, partitionDifference = 0, oversPeak = 0;
void check(bool ok, const char *message) {
    ++checks;
    if (!ok)
        throw std::runtime_error(message);
}
template <class F> void rejects(F fn) {
    bool rejected = false;
    try {
        fn();
    } catch (const ProjectError &) {
        rejected = true;
    }
    check(rejected, "Expected control-side rejection");
}
Session profile(std::uint32_t rate = 48000, double gain = 0, double frequency = 1000, double q = 1,
                std::uint32_t channels = 1) {
    auto s = makeOneTrackSession("Signal fixture", "Audio");
    s.sampleRate = rate;
    auto &t = s.tracks[0];
    t.eq.bands.resize(1);
    t.eq.bands[0].frequencyHz = frequency;
    t.eq.bands[0].gainDb = gain;
    t.eq.bands[0].q = q;
    t.layout = {channels == 1   ? LayoutKind::Mono
                : channels == 2 ? LayoutKind::Stereo
                                : LayoutKind::Discrete,
                channels};
    validate(s);
    return s;
}
ParameterAddress address(const Session &s, BandParameter p = BandParameter::GainDb) {
    const auto &t = s.tracks[0];
    return {t.id, t.eq.id, t.eq.bands[0].id, p};
}
std::vector<float> sine(std::uint32_t rate, std::uint32_t frames, double frequency, double level) {
    std::vector<float> data(frames);
    for (std::size_t f = 0; f < data.size(); ++f)
        data[f] = static_cast<float>(level *
                                     std::sin(2 * std::numbers::pi * frequency * double(f) / rate));
    return data;
}
double difference(const std::vector<float> &a, const std::vector<float> &b) {
    double result = 0;
    for (std::size_t i = 0; i < a.size(); ++i)
        result = std::max(result, std::abs(double(a[i]) - b[i]));
    return result;
}
EqReport audited(PreparedEq &eq, const float *input, float *output, std::uint32_t count,
                 Frame start, std::span<const EqEvent> events = {}) {
    rt_audit::Guard guard;
    return eq.process({&input, 1}, {&output, 1}, count, start, events);
}
std::vector<float> render(PreparedEq &eq, const std::vector<float> &input, std::uint32_t quantum,
                          const std::vector<EqEvent> &events = {}) {
    std::vector<float> output(input.size());
    std::size_t event = 0;
    for (std::size_t frame = 0; frame < input.size();) {
        auto count =
            static_cast<std::uint32_t>(std::min(std::size_t(quantum), input.size() - frame));
        auto first = event;
        while (event < events.size() && events[event].frame < Frame(frame + count))
            ++event;
        const auto r = audited(eq, input.data() + frame, output.data() + frame, count, Frame(frame),
                               std::span(events).subspan(first, event - first));
        check(r.status == ProcessStatus::Ok && r.invalidInputSamples == 0 &&
                  r.numericFaultSamples == 0,
              "Render process failed");
        frame += count;
    }
    return output;
}
void assertCleanAudit() {
    const auto &c = rt_audit::counts;
    check(!c.cppAllocate && !c.cppFree && !c.cAllocate && !c.cFree && !c.blockingLock,
          "RT allocation/free/blocking lock detected");
}
void staticResponse() {
    for (auto rate : {8000u, 44100u, 48000u, 96000u, 384000u}) {
        for (double freq : {20., 100., 1000., 10000., 20000.})
            if (freq < double(rate) / 2) {
                for (double gain : {-6., 6., 24.}) {
                    auto s = profile(rate, gain, freq);
                    PreparedEq eq(s, s.tracks[0].id, 2048, 1);
                    auto input = sine(rate, rate, freq, .09);
                    auto output = render(eq, input, 127);
                    double in = 0, out = 0;
                    for (std::size_t i = rate / 2; i < input.size(); ++i) {
                        in += double(input[i]) * input[i];
                        out += double(output[i]) * output[i];
                    }
                    const auto error = std::abs(10 * std::log10(out / in) - gain);
                    maximumGainError = std::max(maximumGainError, error);
                    check(error <= .05, "EQ center-frequency gain outside tolerance");
                }
            }
        auto s = profile(rate);
        PreparedEq flat(s, s.tracks[0].id, 2048, 2);
        auto input = sine(rate, rate / 4, 1000, 1.4);
        check(render(flat, input, 512) == input, "Flat EQ is not exact unity or clipped headroom");
        for (double q : {.1, 18.})
            for (double gain : {-24., 24.}) {
                auto p = profile(rate, gain, 1000, q);
                PreparedEq eq(p, p.tracks[0].id, 2048, 3);
                auto x = sine(rate, rate / 2, 1000, .04);
                auto y = render(eq, x, 512);
                double in = 0, out = 0;
                for (std::size_t i = rate / 4; i < x.size(); ++i) {
                    in += double(x[i]) * x[i];
                    out += double(y[i]) * y[i];
                }
                const auto error = std::abs(10 * std::log10(out / in) - gain);
                maximumGainError = std::max(maximumGainError, error);
                check(error < .05, "Extreme Q/gain response failed");
            }
    }
    auto s = profile(48000, 6);
    PreparedEq eq(s, s.tracks[0].id, 2048, 4);
    auto y = render(eq, sine(48000, 48000, 1000, .9), 512);
    for (auto x : y)
        oversPeak = std::max(oversPeak, std::abs(double(x)));
    check(oversPeak > 1.7 && oversPeak < 1.81, "Boosted float overs lost to clipping/headroom");
}
void eventsAndPartitions() {
    for (auto rate : {44100u, 48000u, 96000u}) {
        auto s = profile(rate);
        PreparedEq mapper(s, s.tracks[0].id, 2048, 10);
        auto p = s;
        p.tracks[0].eq.bands[0].gainDb = 12;
        std::vector<EqEvent> events{mapper.parameterEvent(p, address(s), 97)};
        p.tracks[0].eq.bands[0].frequencyHz = 430;
        p.tracks[0].eq.bands[0].q = 3;
        events.push_back(mapper.parameterEvent(p, address(s, BandParameter::FrequencyHz), 791));
        p.tracks[0].eq.bands[0].gainDb = -6;
        events.push_back(mapper.parameterEvent(p, address(s), 1003));
        events.push_back(mapper.enableEvent(false, 2607));
        events.push_back(mapper.enableEvent(true, 3901));
        auto x = sine(rate, 10000, 997, .4);
        PreparedEq reference(s, s.tracks[0].id, 2048, 10);
        auto expected = render(reference, x, 2048, events);
        for (auto block : {16u, 64u, 127u, 512u, 2048u}) {
            PreparedEq eq(s, s.tracks[0].id, 2048, 10);
            auto y = render(eq, x, block, events);
            auto error = difference(expected, y);
            partitionDifference = std::max(partitionDifference, error);
            check(error <= 1e-7, "Automation partition dependence");
            PreparedEq liveEq(s, s.tracks[0].id, 2048, 10);
            auto live = std::make_unique<EqLiveDriver>(liveEq);
            for (const auto &e : events)
                check(live->submit(e) == SubmitStatus::Accepted, "Live event submit failed");
            std::vector<float> output(x.size());
            for (std::size_t at = 0; at < x.size();) {
                const auto n =
                    static_cast<std::uint32_t>(std::min(std::size_t(block), x.size() - at));
                const float *in = x.data() + at;
                float *out = output.data() + at;
                EqReport r;
                {
                    rt_audit::Guard guard;
                    r = live->process({&in, 1}, {&out, 1}, n);
                }
                check(r.status == ProcessStatus::Ok, "Live driver failed");
                at += n;
            }
            check(difference(expected, output) <= 1e-7 && live->frame() == Frame(x.size()),
                  "Live/offline mismatch");
        }
        check(mapper.smoothingFrames() == static_cast<std::uint32_t>(std::ceil(rate * .01)),
              "Smoothing duration incorrect");
    }
    auto s = profile();
    PreparedEq eq(s, s.tracks[0].id, 2048, 11);
    auto changed = s;
    changed.tracks[0].eq.bands[0].gainDb = 6;
    const auto event = eq.parameterEvent(changed, address(s), 97);
    std::vector<float> x(2048, .25f), y(2048);
    auto r = audited(eq, x.data(), y.data(), 2048, 0, {&event, 1});
    check(r.eventsApplied == 1 && r.status == ProcessStatus::Ok, "Event not applied");
    check(std::equal(y.begin(), y.begin() + 97, x.begin()), "Event applied before timestamp");
    const double first = .25 * (1 + (event.coefficients.values[0] - 1) / eq.smoothingFrames());
    check(std::abs(y[97] - first) < 1e-8 && y[97] != x[97], "Event did not start on timestamp");
    PreparedEq duplicate(s, s.tracks[0].id, 2048, 11), last(s, s.tracks[0].id, 2048, 11);
    auto earlier = changed;
    earlier.tracks[0].eq.bands[0].gainDb = -6;
    const std::vector<EqEvent> both{duplicate.parameterEvent(earlier, address(s), 97), event};
    check(render(duplicate, x, 127, both) == render(last, x, 512, {event}),
          "Same-frame ingress order not respected");
    auto boosted = profile(48000, 6);
    PreparedEq always(boosted, boosted.tracks[0].id, 2048, 15),
        bypass(boosted, boosted.tracks[0].id, 2048, 15);
    auto musical = sine(48000, 8192, 997, .4);
    auto steady = render(always, musical, 127);
    auto switched = render(bypass, musical, 512,
                           {bypass.enableEvent(false, 97), bypass.enableEvent(true, 3901)});
    const auto settled = std::size_t(97 + bypass.smoothingFrames() - 1);
    check(switched[settled - 1] != musical[settled - 1] &&
              std::equal(switched.begin() + static_cast<std::ptrdiff_t>(settled),
                         switched.begin() + 3901,
                         musical.begin() + static_cast<std::ptrdiff_t>(settled)),
          "Bypass did not reach exact dry after smoothing window");
    const auto restored = std::size_t(3901 + bypass.smoothingFrames() - 1);
    check(std::equal(switched.begin() + static_cast<std::ptrdiff_t>(restored), switched.end(),
                     steady.begin() + static_cast<std::ptrdiff_t>(restored)),
          "Bypass/enable reset filter history or exceeded smoothing window");
    auto reordered = s;
    EqBand extra;
    extra.frequencyHz = 300;
    reordered.tracks[0].eq.bands.push_back(extra);
    PreparedEq mapping(reordered, reordered.tracks[0].id, 2048, 12);
    std::reverse(reordered.tracks[0].eq.bands.begin(), reordered.tracks[0].eq.bands.end());
    reordered.tracks[0].name = "Renamed";
    setParameterValue(reordered, address(s), 7);
    check(mapping.parameterEvent(reordered, address(s), 0).band == 0,
          "Mapping depends on band order");
    auto resized = reordered;
    resized.tracks[0].eq.bands.push_back(EqBand{});
    rejects([&] { mapping.parameterEvent(resized, address(s), 0); });
    auto invalid = event;
    invalid.generation = 999;
    std::fill(y.begin(), y.end(), -99);
    check(audited(eq, x.data(), y.data(), 128, 2048, {&invalid, 1}).status ==
                  ProcessStatus::InvalidEvent &&
              y[0] == -99,
          "Invalid batch wrote output");
    invalid = event;
    invalid.coefficients.values[4] = 1.1;
    check(!eq.validEvent(invalid), "Unstable coefficients accepted");
    invalid = event;
    invalid.coefficients.values[0] = std::numeric_limits<double>::quiet_NaN();
    check(!eq.validEvent(invalid), "NaN coefficients accepted");
    auto live = std::make_unique<EqLiveDriver>(eq);
    check(live->submit(event) == SubmitStatus::Accepted, "Submit valid event");
    auto late = event;
    late.frame = 96;
    check(live->submit(late) == SubmitStatus::OutOfOrder, "Out-of-order event accepted");
    check(live->submit(invalid) == SubmitStatus::Invalid, "Invalid queue event accepted");
    rejects([&] { eq.parameterEvent(changed, {s.id, s.id, s.id, BandParameter::GainDb}, 0); });
    rejects([&] { preparePeakingCoefficients(24000, 0, 1, 48000); });
    rejects([&] { PreparedEq bad(s, s.tracks[0].id, 2048, 0); });
}
void overlappingBandRamps() {
    // Gain-only stable processors give an independent closed-form ramp oracle.
    // Bootstrap each band to identity, preserving unity while its matching
    // feedforward/feedback terms settle, then exercise 3 and all64 concurrent ramps.
    struct Reference {
        double origin = 1, target = 1;
        Frame began = -1;
        double at(Frame frame) const {
            if (began < 0 || frame < began)
                return origin;
            const auto steps = std::min<Frame>(48, frame - began + 1);
            return steps == 48 ? target : origin + (target - origin) * double(steps) / 48;
        }
        void retarget(Frame frame, double value) {
            origin = at(frame - 1);
            target = value;
            began = frame;
        }
    };
    for (const auto count : {3u, 64u}) {
        auto s = profile();
        s.tracks[0].eq.bands.resize(count);
        std::vector<EqEvent> bootstrap;
        for (unsigned b = 0; b < count; ++b)
            bootstrap.push_back({0, 77, {}, static_cast<std::uint16_t>(b), EqEventKind::Band});
        std::vector<EqEvent> events;
        const auto gain = [&](Frame frame, unsigned band, double value) {
            events.push_back({frame,
                              77,
                              {{value, 0, 0, 0, 0}},
                              static_cast<std::uint16_t>(band),
                              EqEventKind::Band});
        };
        gain(11, 0, 4);
        gain(17, 1, 2);
        gain(23, 0, 1.25);
        gain(23, 0, 3); // Same-sample supersession must not count a ramp twice.
        gain(23, 1, 2);
        gain(23, 2, 1.5);
        events.push_back({31, 77, {{0, 0, 0, 0, 0}}, 0, EqEventKind::Enable});
        events.push_back({37, 77, {{1, 0, 0, 0, 0}}, 0, EqEventKind::Enable});
        gain(89, 2, .75); // Start again after an interval with no band ramps.
        for (unsigned b = 0; b < count; ++b)
            if (b != 2)
                gain(111, b, b == 0 ? 3 : b == 1 ? 2 : 1);
        gain(123, 0, 2); // Other ramps finish before this retargeted one.
        events.push_back({211, 77, {{0, 0, 0, 0, 0}}, 0, EqEventKind::Enable});
        events.push_back({300, 77, {{1, 0, 0, 0, 0}}, 0, EqEventKind::Enable});
        const std::vector<float> input(512, .25f);
        std::vector<float> expected(input.size());
        std::vector<Reference> reference(count);
        Reference wet;
        std::size_t next = 0;
        for (Frame f = 0; f < Frame(input.size()); ++f) {
            while (next < events.size() && events[next].frame == f) {
                const auto &e = events[next++];
                (e.kind == EqEventKind::Enable ? wet : reference[e.band])
                    .retarget(f, e.coefficients.values[0]);
            }
            double value = .25;
            for (const auto &band : reference)
                value *= band.at(f);
            const auto mix = wet.at(f);
            expected[std::size_t(f)] = float(mix == 1   ? value
                                             : mix == 0 ? .25
                                                        : .25 + mix * (value - .25));
        }
        std::vector<float> whole;
        for (const auto block : {512u, 1u, 7u, 31u, 127u}) {
            PreparedEq eq(s, s.tracks[0].id, 512, 77, 1);
            check(eq.smoothingFrames() == 48, "Gain ramp fixture duration differs");
            check(render(eq, std::vector<float>(64, .25f), 64, bootstrap) ==
                      std::vector<float>(64, .25f),
                  "Identity bootstrap changed unity");
            eq.reset();
            const auto output = render(eq, input, block, events);
            check(difference(expected, output) <= 5e-7,
                  "Overlapping/restarted band or wet ramps disagree with closed-form gain oracle");
            if (whole.empty())
                whole = output;
            else
                check(whole == output, "Concurrent band ramps depend on block partition");
            std::vector<EqEvent> pending;
            for (unsigned b = 0; b < count; ++b)
                pending.push_back({512,
                                   77,
                                   {{b == 0   ? 4.
                                     : b == 1 ? 2.
                                     : b == 2 ? 1.5
                                              : 1.,
                                     0, 0, 0, 0}},
                                   static_cast<std::uint16_t>(b),
                                   EqEventKind::Band});
            pending.push_back({519, 77, {{0, 0, 0, 0, 0}}, 0, EqEventKind::Enable});
            std::array<float, 48> dry{}, out{};
            dry.fill(.25f);
            check(audited(eq, dry.data(), out.data(), 16, 512, pending).status == ProcessStatus::Ok,
                  "Pending ramp preparation failed");
            eq.reset(); // Commit pending targets, clear histories/ramp bookkeeping while stopped.
            check(audited(eq, dry.data(), out.data(), 32, 528).status == ProcessStatus::Ok &&
                      std::all_of(out.begin(), out.begin() + 32, [](float v) { return v == .25f; }),
                  "Reset did not commit dry target");
            const std::array<EqEvent, 2> restarted{
                {{560, 77, {{2, 0, 0, 0, 0}}, 0, EqEventKind::Band},
                 {560, 77, {{1, 0, 0, 0, 0}}, 0, EqEventKind::Enable}}};
            check(audited(eq, dry.data(), out.data(), 48, 560, restarted).status ==
                          ProcessStatus::Ok &&
                      out.front() != .25f && out.back() == 1.5f,
                  "Reset prevented new band/wet ramp from starting or settling on time");
        }
    }
}
void channelsAndFaults() {
    for (auto channels : {1u, 2u, 8u, 32u, 256u}) {
        auto s = profile(48000, 6, 1000, 1, channels);
        PreparedEq eq(s, s.tracks[0].id, 512, 21);
        std::vector<float> data(std::size_t(channels) * 512);
        data[0] = .8f;
        std::vector<const float *> input(channels);
        std::vector<float *> output(channels);
        for (std::uint32_t c = 0; c < channels; ++c) {
            output[c] = data.data() + std::size_t(c) * 512;
            input[c] = output[c];
        }
        EqReport r;
        {
            rt_audit::Guard guard;
            r = eq.process(input, output, 512, 0);
        }
        check(r.status == ProcessStatus::Ok && r.numericFaultSamples == 0,
              "Multichannel in-place processing failed");
        for (std::size_t c = 512; c < data.size(); ++c)
            check(data[c] == 0, "Cross-channel state leakage");
    }
    auto s = profile();
    PreparedEq eq(s, s.tracks[0].id, 64, 22);
    std::vector<float> x(64, .1f), y(64);
    x[0] = std::numeric_limits<float>::quiet_NaN();
    x[1] = std::numeric_limits<float>::infinity();
    auto r = audited(eq, x.data(), y.data(), 64, 0);
    check(r.invalidInputSamples == 2 &&
              std::all_of(y.begin(), y.end(), [](float n) { return std::isfinite(n); }),
          "Invalid input sanitization failed");
    check(audited(eq, x.data(), y.data(), 65, 64).status == ProcessStatus::InvalidBuffer,
          "Oversize quantum accepted");
    check(audited(eq, x.data(), y.data(), 1, std::numeric_limits<Frame>::max()).status ==
              ProcessStatus::TimingError,
          "Clock overflow accepted");
    auto overflow = profile();
    overflow.tracks[0].eq.bands.clear();
    for (int i = 0; i < 64; ++i) {
        EqBand b;
        b.frequencyHz = 1000;
        b.gainDb = 24;
        b.q = .1;
        overflow.tracks[0].eq.bands.push_back(b);
    }
    PreparedEq huge(overflow, overflow.tracks[0].id, 64, 23);
    std::fill(x.begin(), x.end(), std::numeric_limits<float>::max() / 2);
    r = audited(huge, x.data(), y.data(), 64, 0);
    check(r.numericFaultSamples > 0 &&
              std::all_of(y.begin(), y.end(), [](float n) { return std::isfinite(n); }),
          "Float representational overflow not reported");
    auto driver = std::make_unique<EqLiveDriver>(eq);
    auto e = eq.enableEvent(true, 0);
    for (std::size_t n = 0; n < eqEventQueueCapacity; ++n)
        check(driver->submit(e) == SubmitStatus::Accepted, "Queue capacity incorrect");
    check(driver->submit(e) == SubmitStatus::Full, "Queue overwrote unread events");
    const float *in = x.data();
    float *out = y.data();
    {
        rt_audit::Guard guard;
        r = driver->process({&in, 1}, {&out, 1}, 64);
    }
    check(r.status == ProcessStatus::EventBudgetExceeded && driver->stopped(),
          "Event density did not stop driver");
    {
        rt_audit::Guard guard;
        r = driver->process({&in, 1}, {&out, 1}, 64);
    }
    check(r.status == ProcessStatus::Stopped, "Stopped driver advanced");
    PreparedEq lateEq(s, s.tracks[0].id, 64, 24);
    auto late = std::make_unique<EqLiveDriver>(lateEq, 128);
    check(late->submit(lateEq.enableEvent(true, 1)) == SubmitStatus::Accepted,
          "Late event fixture rejected too early");
    {
        rt_audit::Guard guard;
        r = late->process({&in, 1}, {&out, 1}, 64);
    }
    check(r.status == ProcessStatus::TimingError, "Late event applied at wrong sample");
}
struct Packet {
    std::uint32_t sequence, check;
};
struct QueueStress {
    SpscQueue<Packet, 256> queue{std::numeric_limits<std::uint32_t>::max() - 31};
    std::atomic<bool> abort{false};
    std::uint32_t count = 1000000;
};
void yield() {
#ifdef _WIN32
    SwitchToThread();
#else
    std::this_thread::yield();
#endif
}
void produce(QueueStress &s) {
    for (std::uint32_t n = 0; n < s.count && !s.abort.load(std::memory_order_relaxed); ++n) {
        Packet p{n, n ^ 0xabcdef12};
        while (!s.queue.tryPush(p)) {
            if (s.abort.load(std::memory_order_relaxed))
                return;
            yield();
        }
    }
}
#ifdef _WIN32
DWORD WINAPI producerThread(void *context) {
    produce(*static_cast<QueueStress *>(context));
    return 0;
}
#endif
void queueConcurrency() {
    QueueStress s;
#ifdef _WIN32
    auto thread = CreateThread(nullptr, 0, producerThread, &s, 0, nullptr);
    check(thread != nullptr, "Cannot create producer thread");
#else
    std::thread thread(produce, std::ref(s));
#endif
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(30);
    bool correct = true;
    for (std::uint32_t n = 0; n < s.count;) {
        Packet p;
        bool found = false;
        {
            rt_audit::Guard guard;
            found = s.queue.tryPop(p);
        }
        if (found) {
            if (p.sequence != n || p.check != (n ^ 0xabcdef12)) {
                correct = false;
                break;
            }
            ++n;
        } else {
            if (std::chrono::steady_clock::now() > deadline) {
                correct = false;
                break;
            }
            yield();
        }
    }
    s.abort.store(true, std::memory_order_relaxed);
#ifdef _WIN32
    WaitForSingleObject(thread, INFINITE);
    CloseHandle(thread);
#else
    thread.join();
#endif
    check(correct, "SPSC concurrency/counter wrap integrity failed");
    SpscQueue<Packet, 2> small;
    Packet p{1, 2}, out{};
    {
        rt_audit::Guard guard;
        correct = small.tryPush(p) && small.tryPush(p) && !small.tryPush(p) && small.tryPeek(out) &&
                  small.tryPop(out) && small.tryPush(p);
    }
    check(correct, "Queue full/peek/backpressure failed");
}
struct LifetimeProbe {
    int value;
    std::vector<float> payload;
    static inline int destroyed = 0;
    explicit LifetimeProbe(int n) : value(n), payload(32, float(n)) {}
    ~LifetimeProbe() {
        ++destroyed;
    }
};
void objectRetirement() {
    LifetimeProbe::destroyed = 0;
    {
        RtObjectExchange<LifetimeProbe> exchange(std::make_unique<LifetimeProbe>(0));
        for (int value = 1; value <= 8; ++value) {
            auto prepared = std::make_unique<LifetimeProbe>(value);
            check(exchange.publish(prepared) && !prepared, "Cannot publish prepared object");
            bool valid = false;
            {
                rt_audit::Guard guard;
                valid = exchange.beginReplacement() && exchange.previous()->value == value - 1 &&
                        exchange.active().value == value && !exchange.beginReplacement() &&
                        exchange.finishReplacement();
            }
            check(valid, "Replacement borrow/retirement contract failed");
        }
        check(LifetimeProbe::destroyed == 0, "Object destroyed before control retirement");
        auto nine = std::make_unique<LifetimeProbe>(9), ten = std::make_unique<LifetimeProbe>(10),
             eleven = std::make_unique<LifetimeProbe>(11);
        check(exchange.publish(nine) && exchange.publish(ten) && !exchange.publish(eleven) &&
                  eleven,
              "Ready queue backpressure lost caller ownership");
        bool stalled = false;
        {
            rt_audit::Guard guard;
            stalled = !exchange.beginReplacement() && exchange.active().value == 8;
        }
        check(stalled, "Replacement ignored exhausted retirement credits");
        check(exchange.collectRetired() == 8 && LifetimeProbe::destroyed == 8,
              "Control did not retire exact old objects");
        bool valid = false;
        {
            rt_audit::Guard guard;
            valid = exchange.beginReplacement() && exchange.previous()->payload[0] == 8 &&
                    exchange.active().value == 9;
        }
        check(valid && exchange.collectRetired() == 0,
              "Previous object released before transition completion");
        {
            rt_audit::Guard guard;
            valid = exchange.finishReplacement() && exchange.beginReplacement() &&
                    exchange.active().value == 10 && exchange.finishReplacement();
        }
        check(valid && exchange.collectRetired() == 2, "Finish/replacement sequence failed");
    }
    check(LifetimeProbe::destroyed == 12, "Shutdown leaked active, queued or caller-owned objects");
}
} // namespace
int main() {
    try {
        // Confirm C++ audit hooks are active; exclude this deliberate violation afterward.
        {
            rt_audit::Guard guard;
            void *p = ::operator new(16);
            ::operator delete(p);
        }
        check(rt_audit::counts.cppAllocate == 1 && rt_audit::counts.cppFree == 1,
              "Audit hooks not active");
        rt_audit::reset();
        staticResponse();
        eventsAndPartitions();
        overlappingBandRamps();
        channelsAndFaults();
        queueConcurrency();
        objectRetirement();
        assertCleanAudit();
        std::cout << "{\"checks\":" << checks << ",\"gain_error_db\":" << maximumGainError
                  << ",\"partition_error\":" << partitionDifference
                  << ",\"float_overs_peak\":" << oversPeak
                  << ",\"rt_cpp_allocations\":" << rt_audit::counts.cppAllocate
                  << ",\"rt_cpp_frees\":" << rt_audit::counts.cppFree
                  << ",\"rt_c_allocations\":" << rt_audit::counts.cAllocate
                  << ",\"rt_c_frees\":" << rt_audit::counts.cFree
                  << ",\"rt_blocking_locks\":" << rt_audit::counts.blockingLock
                  << ",\"queue_packets\":1000000}\n";
        return 0;
    } catch (const std::exception &e) {
        rt_audit::active = false;
        std::cerr << "FAIL: " << e.what() << '\n';
        return 1;
    }
}
