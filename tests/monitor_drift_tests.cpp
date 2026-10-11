// SPDX-License-Identifier: GPL-3.0-only
#include <soundcurrent/monitor_drift.hpp>
#include "rt_audit.hpp"
#include <algorithm>
#include <cmath>
#include <iostream>
#include <limits>
#include <vector>
using namespace soundcurrent::daw;
namespace {
std::uint64_t checks = 0, grids = 0;
void check(bool ok, const char *why) { ++checks; if (!ok) throw std::runtime_error(why); }
template<class F> auto rt(F f) { rt_audit::Guard guard; return f(); }
template<class F> void refuses(F f) {
    try { f(); } catch (const ProjectError &) { ++checks; return; }
    throw std::runtime_error("Invalid drift admission accepted");
}
constexpr std::uint64_t qbase = (std::uint64_t(1) << 63) + 555555;
constexpr std::uint64_t cbase = (std::uint64_t(1) << 54) + 77, rbase = (std::uint64_t(1) << 55) + 99;
CaptureClockReading capture(const MonitorDriftConfig &c, std::uint64_t delta, std::uint64_t time) {
    return {c.captureIdentity, {cbase + delta}, {qbase + time}};
}
RenderClockReading render(const MonitorDriftConfig &c, std::uint64_t delta, std::uint64_t time) {
    return {c.renderIdentity, {rbase + delta}, {qbase + time}, c.renderFrequency};
}
void seed(PreparedMonitorDrift &d, const MonitorDriftConfig &c, std::uint64_t ac = 96000,
          std::uint64_t bc = 96000, std::uint64_t duration = 20000000) {
    check(rt([&] { return d.observe(capture(c, 0, 0)); }) == ClockCorrelationStatus::WaitingSpan, "Capture startup");
    check(rt([&] { return d.observe(render(c, 0, 0)); }) == ClockCorrelationStatus::WaitingSpan, "Render startup");
    check(rt([&] { return d.observe(capture(c, ac, duration)); }) == ClockCorrelationStatus::Ready, "Capture fit");
    check(rt([&] { return d.observe(render(c, bc, duration)); }) == ClockCorrelationStatus::Ready, "Render fit");
}
MonitorControlPoint point(const MonitorDriftConfig &c, std::uint64_t time = 20000000) {
    return {c.runGeneration, c.outputFrameBase, {qbase + time}, c.targetQueuedSourceFrames};
}
void admissionAndTransactions() {
    MonitorDriftConfig c; ResourceLedger ledger(PreparedMonitorDrift::payloadBytes); c.resources = ledger;
    const auto before = ledger.usage();
    {
        PreparedMonitorDrift d(c); const auto admitted = ledger.usage();
        check(admitted.reservedBytes == PreparedMonitorDrift::payloadBytes && admitted.owners == 1, "Drift credit");
        refuses([&] { PreparedMonitorDrift second(c); }); check(ledger.usage() == admitted, "Failed drift credit mutated");
        check(rt([&] { return d.propose(point(c)); }).code == MonitorDriftCode::WaitingClock, "Unseeded controller ran");
        seed(d, c);
        auto p = point(c); p.queuedSourceFrames += 100;
        const auto r = rt([&] { return d.propose(p); });
        check(r.code == MonitorDriftCode::Ready && r.event.outputFrame == c.outputFrameBase &&
                  r.event.ratio < 1 && r.feedbackPpm < 0, "High source occupancy sign");
        check(d.nextOutputFrame() == c.outputFrameBase && d.lastScheduledRatio() == 1, "Proposal committed prematurely");
        const auto retry = rt([&] { return d.propose(p); });
        check(retry.event.ratio == r.event.ratio && retry.filteredOccupancyErrorFrames == r.filteredOccupancyErrorFrames,
              "Queue-refusal retry integrated feedback twice");
        auto different = p; ++different.asOf.value;
        check(rt([&] { return d.propose(different); }).code == MonitorDriftCode::Pending, "Pending point replaced");
        auto wrong = r.event; wrong.ratio += .00001;
        check(rt([&] { return d.commit(wrong); }) == MonitorDriftCode::InvalidCommit &&
                  d.nextOutputFrame() == c.outputFrameBase, "Wrong commit changed controller");
        check(rt([&] { return d.commit(r.event); }) == MonitorDriftCode::Committed && d.nextOutputFrame() == 64,
              "Accepted event did not commit once");
        check(rt([&] { return d.commit(r.event); }) == MonitorDriftCode::InvalidCommit, "Duplicate event commit");
        p.outputFrame = 64; p.queuedSourceFrames = c.inputCapacityFrames + 1;
        check(rt([&] { return d.propose(p); }).code == MonitorDriftCode::InvalidPoint, "Bad occupancy admitted");
        p.queuedSourceFrames = c.targetQueuedSourceFrames; p.runGeneration = 42;
        check(rt([&] { return d.propose(p); }).code == MonitorDriftCode::InvalidPoint, "Stale generation admitted");
        check(ledger.usage() == admitted, "Prepared drift credit changed");
    }
    check(ledger.usage().reservedBytes == before.reservedBytes && !ledger.usage().owners, "Drift retirement credit");
    for (unsigned n = 0; n < 14; ++n) {
        MonitorDriftConfig bad;
        switch (n) {
        case 0: bad.sourceRate = 0; break; case 1: bad.renderFrequency = 0; break;
        case 2: bad.captureIdentity.generation = 0; break; case 3: bad.renderIdentity.stream = 0; break;
        case 4: bad.runGeneration = 0; break; case 5: bad.outputFrameBase = 17; break;
        case 6: bad.minimumSpan100ns = 2000; break; case 7: bad.maximumStepPpm = 4001; break;
        case 8: bad.targetQueuedSourceFrames = bad.inputCapacityFrames; break;
        case 9: bad.maximumConfidencePpm = std::numeric_limits<double>::quiet_NaN(); break;
        case 10: bad.occupancyGainPpmPerFrame = -1; break; case 11: bad.smoothingSeconds = 0; break;
        case 12: bad.maximumFeedbackPpm = 2001; break; case 13: bad.memoryBudgetBytes = 4095; break;
        }
        refuses([&] { PreparedMonitorDrift d(bad); });
    }
}
void unitsAndFaults() {
    MonitorDriftConfig c; c.renderFrequency = 10000000; c.outputFrameBase = std::uint64_t(1) << 54;
    PreparedMonitorDrift d(c); seed(d, c, 96024, 20000400);
    const auto r = rt([&] { return d.propose(point(c)); });
    // Independent exact fraction: (20000400*48000)/(10000000*96024).
    const auto expected = 960019200000.0 / 960240000000.0;
    check(r.code == MonitorDriftCode::Ready && std::abs(r.clockRatio - expected) < 4e-15 &&
              r.event.outputFrame == c.outputFrameBase && r.slewLimited,
          "Native tick frequency/origin subtraction/ratio direction");
    check(r.maximumClockErrorPpm > 199 && r.maximumClockErrorPpm < 201, "Endpoint uncertainty propagation");
    check(rt([&] { return d.commit(r.event); }) == MonitorDriftCode::Committed, "Large ordinal commit");
    auto p = point(c); p.outputFrame += 64; --p.asOf.value;
    check(rt([&] { return d.propose(p); }).code == MonitorDriftCode::Fault &&
              d.firstFault()->code == MonitorDriftFaultCode::BackwardControlTime, "Backward control timestamp");
    const auto first = d.firstFault(); rt([&] { d.rejectEvent(); }); rt([&] { d.cancel(); });
    check(d.firstFault() == first, "First drift fault replaced");
    for (unsigned n = 0; n < 7; ++n) {
        c = {}; PreparedMonitorDrift f(c); seed(f, c); p = point(c);
        switch (n) {
        case 0: p.asOf.value += c.maximumAge100ns + 1; break;
        case 1: { auto x = capture(c, 96048, 20010000); ++x.identity.endpoint; rt([&] { f.observe(x); }); break; }
        case 2: { auto x = render(c, 96048, 20010000); ++x.frequency; rt([&] { f.observe(x); }); break; }
        case 3: { auto x = capture(c, 96048, 20010000); x.discontinuity = true; rt([&] { f.observe(x); }); break; }
        case 4: { auto x = render(c, 1, 20010000); rt([&] { f.observe(x); }); break; }
        case 5: rt([&] { f.rejectEvent(); }); break;
        case 6: rt([&] { f.cancel(); }); break;
        }
        const auto proposal = rt([&] { return f.propose(p); });
        check(proposal.code == (n == 6 ? MonitorDriftCode::Cancelled : MonitorDriftCode::Fault), "Terminal drift path");
        if (n == 0) check(f.firstFault()->code == MonitorDriftFaultCode::StaleCapture, "Stale capture classification");
    }
    c = {}; c.maximumConfidencePpm = 100;
    PreparedMonitorDrift uncertain(c); seed(uncertain, c);
    check(rt([&] { return uncertain.propose(point(c)); }).code == MonitorDriftCode::WaitingConfidence &&
              uncertain.nextOutputFrame() == 0 && !uncertain.firstFault(), "Uncertain fit silently clamped");
    c = {}; c.outputFrameBase = UINT64_MAX - 63;
    PreparedMonitorDrift overflow(c); seed(overflow, c);
    check(rt([&] { return overflow.propose(point(c)); }).code == MonitorDriftCode::Fault &&
              overflow.firstFault()->code == MonitorDriftFaultCode::CounterOverflow, "Grid overflow");
    c = {}; PreparedMonitorDrift relative(c); seed(relative, c, 96144, 95856);
    check(rt([&] { return relative.propose(point(c)); }).code == MonitorDriftCode::Fault &&
              relative.firstFault()->code == MonitorDriftFaultCode::RatioOutOfRange,
          "Opposite endpoint drift beyond monitor range silently clamped");
    c = {}; PreparedMonitorDrift delayed(c); seed(delayed, c);
    auto x = capture(c, 96048, 20010000); x.reliability = ClockReliability::Delayed;
    check(rt([&] { return delayed.observe(x); }) == ClockCorrelationStatus::Unreliable, "Delayed reading fitted");
    auto y = render(c, 96048, 20010000); y.reliability = ClockReliability::Missing;
    check(rt([&] { return delayed.observe(y); }) == ClockCorrelationStatus::MissingTimestamp, "Missing timestamp fitted");
    p = point(c, 20010000);
    check(rt([&] { return delayed.propose(p); }).code == MonitorDriftCode::Ready, "Fresh prior fit not retained");
}
void goldenRatiosAndWindows() {
    struct Vector {
        unsigned source, output; std::uint64_t frequency, ac, bc, at, bt;
        double ratio, uncertainty;
    };
    const Vector vectors[] = {
#include "monitor_drift_vectors.inc"
    };
    for (const auto &v : vectors) {
        MonitorDriftConfig c; c.sourceRate = v.source; c.outputRate = v.output; c.renderFrequency = v.frequency;
        c.maximumConfidencePpm = 1000; c.outputFrameBase = std::uint64_t(1) << 54;
        PreparedMonitorDrift d(c);
        rt([&] { d.observe(capture(c, 0, 0)); d.observe(render(c, 0, 0));
                 d.observe(capture(c, v.ac, v.at)); d.observe(render(c, v.bc, v.bt)); });
        const auto r = rt([&] { return d.propose(point(c, std::max(v.at, v.bt))); });
        check(r.code == MonitorDriftCode::Ready && std::abs(r.clockRatio - v.ratio) <= v.ratio * 4e-15,
              "Fraction ratio oracle across admitted rates/native units");
        check(std::abs(r.maximumClockErrorPpm - v.uncertainty) < 1e-7, "Fraction uncertainty oracle");
    }
    MonitorDriftConfig c; c.maximumConfidencePpm = 125;
    PreparedMonitorDrift confidence(c); seed(confidence, c);
    check(rt([&] { return confidence.propose(point(c)); }).code == MonitorDriftCode::WaitingConfidence, "Short uncertain fit ran");
    rt([&] { confidence.observe(capture(c, 192000, 40000000)); confidence.observe(render(c, 192000, 40000000)); });
    check(rt([&] { return confidence.propose(point(c, 40000000)); }).code == MonitorDriftCode::Ready,
          "Longer trustworthy fit did not gain declared confidence");
    c = {}; PreparedMonitorDrift staleRender(c); seed(staleRender, c);
    rt([&] { staleRender.observe(capture(c, 105600, 22000000)); });
    check(rt([&] { return staleRender.propose(point(c, 22000000)); }).code == MonitorDriftCode::Fault &&
              staleRender.firstFault()->code == MonitorDriftFaultCode::StaleRender, "Independent render fit age ignored");
    c = {}; PreparedMonitorDrift gap(c); seed(gap, c);
    auto r = rt([&] { return gap.propose(point(c)); }); rt([&] { gap.commit(r.event); });
    rt([&] { gap.observe(capture(c, 672000, 140000000)); gap.observe(render(c, 672000, 140000000)); });
    auto p = point(c, 140000000); p.outputFrame = 64;
    check(rt([&] { return gap.propose(p); }).code == MonitorDriftCode::Fault, "Active clock loss restarted monitor silently");
    ClockCorrelationConfig f; f.identity = c.captureIdentity; f.minimumSpan100ns = 100000;
    PreparedClockCorrelation clock(f);
    rt([&] { clock.restartWindow(); clock.observe(capture(c, 0, 0)); clock.observe(capture(c, 480, 100000)); });
    const auto first = clock.firstReading();
    check(clock.snapshot().has_value(), "Initial window fit unavailable");
    rt([&] { clock.restartWindow(); });
    check(!clock.snapshot() && clock.firstReading()->position == first->position, "Window restart fabricated initial reading");
    check(rt([&] { return clock.observe(capture(c, 960, 200000)); }) == ClockCorrelationStatus::Ready,
          "Window did not reseed from last original reading");
    auto x = capture(c, 1440, 300000); x.discontinuity = true;
    rt([&] { clock.observe(x); clock.restartWindow(); });
    check(clock.firstFault()->status == ClockCorrelationStatus::Discontinuity && !clock.snapshot(), "Window cleared sticky fault");
}
void eventBackpressureAndSaturation() {
    MonitorDriftConfig c; PreparedMonitorDrift d(c); seed(d, c);
    PreparedMonitorBuffer buffer({});
    auto p = point(c); p.queuedSourceFrames += 100;
    for (unsigned n = 0; n < PreparedMonitorBuffer::eventCapacity; ++n) {
        p.outputFrame = n * 64; const auto r = rt([&] { return d.propose(p); });
        check(r.code == MonitorDriftCode::Ready && rt([&] { return buffer.enqueue(r.event); }) == MonitorBufferCode::Accepted,
              "Prepared event queue fill");
        check(rt([&] { return d.commit(r.event); }) == MonitorDriftCode::Committed, "Queue fill commit");
    }
    p.outputFrame += 64; const auto r = rt([&] { return d.propose(p); });
    const auto frame = d.nextOutputFrame(); const auto ratio = d.lastScheduledRatio();
    check(rt([&] { return buffer.enqueue(r.event); }) == MonitorBufferCode::EventFull, "Event pressure not exercised");
    const auto retry = rt([&] { return d.propose(p); });
    check(retry.event.ratio == r.event.ratio && retry.filteredOccupancyErrorFrames == r.filteredOccupancyErrorFrames &&
              d.nextOutputFrame() == frame && d.lastScheduledRatio() == ratio, "Event pressure advanced controller");
    rt([&] { d.rejectEvent(); buffer.cancel(); });
    check(d.firstFault()->code == MonitorDriftFaultCode::EventRejected, "Rejected native event not explicit");
    c = {}; PreparedMonitorDrift saturated(c); seed(saturated, c, 95813, 96000);
    bool feedback = false, ratioLimit = false, slew = false;
    for (unsigned n = 0; n < 3000; ++n) {
        p = point(c); p.outputFrame = n * 64; p.queuedSourceFrames = 0;
        const auto r2 = rt([&] { return saturated.propose(p); });
        check(r2.code == MonitorDriftCode::Ready && r2.event.ratio <= 1.002, "Saturated ratio exceeded admission");
        feedback |= r2.feedbackSaturated; ratioLimit |= r2.ratioSaturated; slew |= r2.slewLimited;
        check(rt([&] { return saturated.commit(r2.event); }) == MonitorDriftCode::Committed, "Saturation commit");
    }
    check(feedback && ratioLimit && slew && saturated.lastScheduledRatio() == 1.002,
          "Feedback/ratio/slew saturation not exposed");
}
void plant(double capturePpm, double renderPpm, double initialError) {
    MonitorDriftConfig c; c.renderFrequency = 10000000; c.maximumAge100ns = 1000000;
    PreparedMonitorDrift d(c);
    const auto capRate = c.sourceRate * (1 + capturePpm * 1e-6);
    const auto outRate = c.outputRate * (1 + renderPpm * 1e-6);
    const auto tickRate = double(c.renderFrequency) * (1 + renderPpm * 1e-6);
    seed(d, c, std::uint64_t(std::llround(capRate * 2)), std::uint64_t(std::llround(tickRate * 2)));
    double occupancy = c.targetQueuedSourceFrames + initialError, worst = 0;
    constexpr unsigned count = 500000;
    for (unsigned n = 0; n < count; ++n) {
        const auto time = std::uint64_t(std::llround((2 + double(n) * 64 / outRate) * 10000000));
        if (n && n % 32 == 0) {
            const auto t = double(time) / 10000000;
            check(rt([&] { return d.observe(capture(c, std::uint64_t(std::llround(t * capRate)), time)); }) ==
                      ClockCorrelationStatus::Ready, "Plant capture fit lost");
            check(rt([&] { return d.observe(render(c, std::uint64_t(std::llround(t * tickRate)), time)); }) ==
                      ClockCorrelationStatus::Ready, "Plant render fit lost");
        }
        MonitorControlPoint p{c.runGeneration, std::uint64_t(n) * 64, {qbase + time},
                              std::uint32_t(std::llround(occupancy))};
        const auto r = rt([&] { return d.propose(p); });
        check(r.code == MonitorDriftCode::Ready && !r.ratioSaturated, "Plant controller unavailable");
        check(std::abs(r.event.ratio - d.lastScheduledRatio()) <= 1.00001e-6, "Plant slew exceeded bound");
        check(rt([&] { return d.commit(r.event); }) == MonitorDriftCode::Committed, "Plant event commit");
        // Analytic fluid reservoir oracle, not a native scheduler or kernel.
        occupancy += capRate * (64 / outRate) - 64 / r.event.ratio;
        worst = std::max(worst, std::abs(occupancy - c.targetQueuedSourceFrames));
        check(std::isfinite(occupancy) && occupancy > 0 && occupancy < c.inputCapacityFrames,
              "Plant reservoir crossed admitted bounds");
        ++grids;
    }
    check(std::abs(occupancy - c.targetQueuedSourceFrames) < 3 && worst < std::abs(initialError) + 3,
          "Default proportional/filtered/slewed plant failed to settle");
    std::cout << "plant capturePpm=" << capturePpm << " renderPpm=" << renderPpm
              << " initialError=" << initialError << " finalError=" << occupancy - c.targetQueuedSourceFrames
              << " worstError=" << worst << " grids=" << count << '\n';
}
void composedBuffer() {
    MonitorBufferConfig b; b.channels = 2; b.maximumInputFrames = 257; b.inputCapacityFrames = 2048;
    b.outputCapacityFrames = 129; b.retainedSourceFrames = 128;
    PreparedMonitorBuffer buffer(b);
    MonitorDriftConfig c; c.inputCapacityFrames = b.inputCapacityFrames; c.targetQueuedSourceFrames = 1024;
    PreparedMonitorDrift d(c); seed(d, c);
    std::vector<float> input(257 * b.channels), output(7 * b.channels);
    for (std::size_t n = 0; n < input.size(); ++n) input[n] = float(2 * std::sin(.1 * double(n)));
    unsigned got = 0; constexpr unsigned wanted = 8192;
    std::uint64_t serviceCalls = 0, iterations = 0;
    while (got < wanted) {
        auto s = buffer.statistics();
        if (s.queuedInputFrames < c.targetQueuedSourceFrames)
            check(rt([&] { return buffer.push(input); }) == MonitorBufferCode::Accepted, "Composed input copy");
        s = buffer.statistics();
        if (s.nextOutputFrame < wanted && !s.pendingGridFrames && s.nextOutputFrame == d.nextOutputFrame()) {
            const auto time = 20000000 + s.nextOutputFrame * 10000000 / c.outputRate;
            if (s.nextOutputFrame) {
                check(rt([&] { return d.observe(capture(c, 96000 + s.nextOutputFrame, time)); }) ==
                          ClockCorrelationStatus::Ready, "Composed capture clock");
                check(rt([&] { return d.observe(render(c, 96000 + s.nextOutputFrame, time)); }) ==
                          ClockCorrelationStatus::Ready, "Composed render clock");
            }
            const auto r = rt([&] { return d.propose({c.runGeneration, s.nextOutputFrame, {qbase + time}, s.queuedInputFrames}); });
            check(r.code == MonitorDriftCode::Ready, "Composed ratio proposal");
            check(rt([&] { return buffer.enqueue(r.event); }) == MonitorBufferCode::Accepted,
                  "Controller emitted an unadmitted buffer ratio/grid");
            check(rt([&] { return d.commit(r.event); }) == MonitorDriftCode::Committed, "Composed commit");
        }
        if (s.nextOutputFrame < wanted) {
            check(rt([&] { return buffer.service(); }) != MonitorBufferCode::Fault, "Composed service fault"); ++serviceCalls;
        }
        const auto read = rt([&] { return buffer.pull(output); }); got += read.frames;
        for (unsigned n = 0; n < read.frames * b.channels; ++n) check(std::isfinite(output[n]), "Composed nonfinite output");
        check(got <= wanted && ++iterations < 20000, "Composed bounded progress");
    }
    check(rt([&] { return buffer.finishAt(wanted); }) == MonitorBufferCode::Accepted, "Composed exact Stop");
    check(rt([&] { return buffer.service(); }) == MonitorBufferCode::Done && !buffer.firstFault(), "Composed terminal Stop");
    rt([&] { d.cancel(); });
    check(buffer.statistics().deliveredOutputFrames == wanted && d.nextOutputFrame() == wanted, "Composed grid extent");
    std::cout << "stereo buffer composedFrames=" << got << " serviceCalls=" << serviceCalls << '\n';
}
} // namespace
int main() {
    try {
        rt_audit::reset(); admissionAndTransactions(); unitsAndFaults(); goldenRatiosAndWindows();
        eventBackpressureAndSaturation();
        plant(150, -50, 200); plant(-150, 50, -200); composedBuffer();
        check(!rt_audit::counts.cppAllocate && !rt_audit::counts.cppFree, "Prepared C++ allocation/free");
#ifdef SC_WRAP_LIBC
        check(!rt_audit::counts.cAllocate && !rt_audit::counts.cFree && !rt_audit::counts.blockingLock,
              "Prepared libc/pthread operations");
        std::cout << "libcInterposition=enabled cAllocations=0 cFrees=0 blockingLocks=0\n";
#else
        std::cout << "libcInterposition=disabled (C allocation/locks not intercepted)\n";
#endif
        std::cout << checks << " monitor drift checks passed grids=" << grids << " cppAllocations=0 cppFrees=0\n";
        return 0;
    } catch (const std::exception &e) {
        rt_audit::active = false; std::cerr << "Check " << checks << ": " << e.what() << '\n'; return 1;
    }
}
