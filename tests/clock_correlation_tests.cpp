// SPDX-License-Identifier: GPL-3.0-only
#include <soundcurrent/clock_correlation.hpp>
#include "rt_audit.hpp"
#include <iostream>
#include <limits>
#include <type_traits>
using namespace soundcurrent::daw;
namespace {
using S = ClockCorrelationStatus;
constexpr ClockStreamIdentity id{7,29,3};
unsigned checks = 0;
void require(bool ok, const char *description) {
    ++checks;
    if (!ok) throw std::runtime_error(description);
}
ClockCorrelationConfig config(ClockUnit unit = ClockUnit::CaptureFrames, std::uint64_t frequency = 48000) {
    ClockCorrelationConfig c; c.identity=id; c.unit=unit; c.frequency=frequency; return c;
}
CaptureClockReading capture(std::uint64_t position, std::uint64_t qpc, bool discontinuity=false) {
    return {id,{position},{qpc},ClockReliability::Accurate,discontinuity};
}
RenderClockReading render(std::uint64_t position, std::uint64_t qpc, std::uint64_t frequency) {
    return {id,{position},{qpc},frequency,ClockReliability::Accurate,false};
}
ProjectClockAnchor anchor(std::uint64_t qpc=10000000, Frame frame=0, std::uint32_t rate=48000) {
    return {1,{qpc},frame,rate};
}
void fit(PreparedClockCorrelation &clock) {
    require(clock.observe(capture(0,10000000,true))==S::WaitingSpan,"capture zero origin");
    require(clock.observe(capture(480,10100000))==S::Ready,"capture fit ready");
}
template<class F> void refuses(F fn) {
    try { fn(); } catch(const ProjectError &e) {
        require(e.code()==ErrorCode::InvalidState,"admission error identity"); return;
    }
    require(false,"expected admission refusal");
}
struct Golden {
    std::uint64_t frequency,p0,p1,q0,q1,position,anchorQpc;
    Frame anchorFrame;
    std::uint32_t rate;
    std::uint64_t expectedQpc;
    Frame expectedFloor;
    std::uint32_t expectedNumerator;
    std::uint64_t expectedError;
};
#include "clock_correlation_vectors.inc"
void goldenVectors() {
    for (const auto &v : golden) {
        PreparedClockCorrelation c(config(ClockUnit::RenderTicks,v.frequency));
        require(c.observe(render(v.p0,v.q0,v.frequency))==S::WaitingSpan,"golden initial reading");
        require(c.observe(render(v.p1,v.q1,v.frequency))==S::Ready,"golden ready fit");
        const auto p=c.project(id,RenderTickPosition{v.position},{v.q1},anchor(v.anchorQpc,v.anchorFrame,v.rate));
        require(p.status==S::Ready,"golden projection admitted");
        require(p.estimatedQpc.value==v.expectedQpc,"Fraction oracle QPC");
        require(p.project==FractionalProjectFrame{v.expectedFloor,v.expectedNumerator},"Fraction oracle signed frame");
        require(p.maximumError100ns==v.expectedError,"declared interpolation error");
    }
}
void domainsAndDrift() {
    for (const auto rate : {44100u,48000u,96000u}) {
        PreparedClockCorrelation input(config(ClockUnit::CaptureFrames,rate));
        PreparedClockCorrelation output(config(ClockUnit::RenderTicks,std::uint64_t(rate)*8));
        require(input.observe(capture(1234567,100000000))==S::WaitingSpan,"independent capture origin");
        require(output.observe(render(987654321,100000000,std::uint64_t(rate)*8))==S::WaitingSpan,"independent byte-like render origin");
        require(input.observe(capture(1234567+rate,110000000))==S::Ready,"capture rate fit");
        require(output.observe(render(987654321+std::uint64_t(rate)*8,110000000,std::uint64_t(rate)*8))==S::Ready,"native render frequency fit");
        const auto a=anchor(100000000,-17,rate);
        const auto i=input.project(id,CaptureFramePosition{1234567+rate/2},{110000000},a);
        const auto o=output.project(id,RenderTickPosition{987654321+std::uint64_t(rate)*4},{110000000},a);
        require(i.status==S::Ready && o.status==S::Ready && i.project==o.project,"distinct units/origins same project correspondence");
        require(i.project==FractionalProjectFrame{Frame(rate/2)-17,0},"half-second project frame");
    }
    for (const auto duration : {std::uint64_t(9998000),std::uint64_t(10002000)}) {
        auto c=config(); c.maximumSpan100ns=20000000;
        PreparedClockCorrelation clock(c);
        require(clock.observe(capture(123,50000000))==S::WaitingSpan,"drift seed");
        require(clock.observe(capture(48123,50000000+duration))==S::Ready,"independent 200ppm drift admitted");
        const auto p=clock.project(id,CaptureFramePosition{24123},{50000000+duration},anchor(50000000));
        require(p.status==S::Ready && p.estimatedQpc.value==50000000+duration/2,"drift fit uses observations not nominal frequency");
    }
    auto strict=config(); strict.maximumDriftPpm=0; strict.timestampUncertainty100ns=100;
    PreparedClockCorrelation accumulating(strict);
    require(accumulating.observe(capture(0,10000000))==S::WaitingSpan,"accumulated drift seed");
    require(accumulating.observe(capture(480,10100100))==S::Ready,"small first discrepancy admitted");
    require(accumulating.observe(capture(960,10200200))==S::Ready,"bounded endpoint discrepancy admitted");
    require(accumulating.observe(capture(1440,10300300))==S::ExcessiveDrift,"pairwise jitter cannot accumulate beyond endpoint budget");
    require(!accumulating.snapshot(),"drifting fit invalidated");
    PreparedClockCorrelation bad(config());
    bad.observe(capture(0,10000000));
    require(bad.observe(capture(480,10110000))==S::ExcessiveDrift,"excessive drift refusal");
}
void reliabilityAndFaults() {
    PreparedClockCorrelation clock(config());
    require(clock.project(id,CaptureFramePosition{0},{10000000},anchor()).status==S::NotReady,"no fabricated origin");
    auto missing=capture(10,10000000); missing.reliability=ClockReliability::Missing;
    require(clock.observe(missing)==S::MissingTimestamp && !clock.firstReading(),"missing timestamp cannot seed");
    auto delayed=missing; delayed.reliability=ClockReliability::Delayed;
    require(clock.observe(delayed)==S::Unreliable && !clock.firstReading(),"delayed reading cannot seed");
    require(clock.observe(capture(0,0))==S::MissingTimestamp,"zero QPC refused");
    fit(clock);
    require(clock.firstReading()->discontinuity && clock.firstReading()->position==0,"original startup discontinuity retained");
    require(clock.observe(capture(480,10101000))==S::WaitingMotion,"position plateau not a new anchor");
    require(clock.snapshot()->end.qpc.value==10100000,"plateau cannot change fit");
    require(clock.observe(delayed)==S::Unreliable && clock.snapshot().has_value(),"unreliable observation preserves prior bounded fit");
    require(clock.project(id,CaptureFramePosition{480},{11100001},anchor()).status==S::Stale,"unreliable reading does not freshen age");
    require(clock.observe(capture(960,10200000,true))==S::Discontinuity,"later discontinuity faults");
    require(clock.observe(capture(1440,10300000))==S::Discontinuity && !clock.snapshot(),"first fault sticky");
    require(clock.firstFault()->reading.position==960,"original bad reading retained");
    PreparedClockCorrelation output(config(ClockUnit::RenderTicks));
    require(output.observe(render(0,10000000,48000))==S::StartupZero && !output.firstReading(),"render startup zero is not physical anchor");
    require(output.observe(render(0,11000000,48000))==S::StartupZero,"render startup plateau");
    require(output.observe(render(17,12000000,48000))==S::WaitingSpan,"render first movement seed");
    require(output.observe(render(497,12100000,48000))==S::Ready,"render motion fit");
    require(output.observe(render(500,12101000,96000))==S::FrequencyChanged,"frequency switch requires new owner");
    for (const auto fault : {S::ClockReset,S::IdentityChanged,S::WrongUnit,S::InvalidReading}) {
        PreparedClockCorrelation c(config()); fit(c);
        if (fault==S::ClockReset) require(c.observe(capture(479,10200000))==fault,"backward native clock");
        if (fault==S::IdentityChanged) { auto r=capture(960,10200000); ++r.identity.generation; require(c.observe(r)==fault,"new stream generation"); }
        if (fault==S::WrongUnit) require(c.observe(render(960,10200000,48000))==fault,"mixed units fault");
        if (fault==S::InvalidReading) require(c.observe(capture(960,10100000))==fault,"moving clock frozen QPC");
        require(c.firstFault()->status==fault,"fault status retained");
    }
    PreparedClockCorrelation backwards(config()); fit(backwards);
    require(backwards.observe(capture(960,10099999))==S::ClockReset,"backward QPC");
    PreparedClockCorrelation badEnum(config());
    auto r=capture(0,10000000); r.reliability=static_cast<ClockReliability>(99);
    require(badEnum.observe(r)==S::InvalidReading,"invalid reliability enum");
}
void boundsAndRounding() {
    PreparedClockCorrelation c(config()); fit(c);
    const auto p=c.project(id,CaptureFramePosition{1},{10100000},anchor(10000001));
    require(p.estimatedQpc.value==10000208 && p.project==FractionalProjectFrame{0,9936000},"nearest QPC then exact fractional frame");
    const auto negative=c.project(id,CaptureFramePosition{0},{10100000},anchor(10000001));
    require(negative.project==FractionalProjectFrame{-1,9952000},"negative fractional floor");
    require(c.project(id,CaptureFramePosition{480},{10099999},anchor()).status==S::InvalidReading,"query as-of precedes fit");
    require(c.project(id,CaptureFramePosition{960},{11100000},anchor()).status==S::Ready,"inclusive age and extrapolation bounds");
    require(c.project(id,CaptureFramePosition{961},{10100000},anchor()).status==S::ExtrapolationExceeded,"extrapolation bound refusal");
    require(c.project(id,CaptureFramePosition{480},{11100001},anchor()).status==S::Stale,"age bound refusal");
    auto wrong=id; ++wrong.endpoint;
    require(c.project(wrong,CaptureFramePosition{0},{10100000},anchor()).status==S::IdentityChanged,"query endpoint mismatch");
    require(c.project(id,RenderTickPosition{0},{10100000},anchor()).status==S::WrongUnit,"query unit mismatch");
    for (const auto a : {ProjectClockAnchor{2,{10000000},0,48000},anchor(0),anchor(10000000,0,7999),anchor(10000000,0,384001)})
        require(c.project(id,CaptureFramePosition{0},{10100000},a).status==S::InvalidProjectAnchor,"invalid project anchor");
    require(c.snapshot().has_value() && !c.firstFault(),"bad query does not poison owner");
    require(c.project(id,CaptureFramePosition{0},{10100000},anchor(10000000,INT64_MIN)).project.floor==INT64_MIN,"minimum signed frame exact");
    require(c.project(id,CaptureFramePosition{0},{10100000},anchor(10000000,INT64_MAX)).project.floor==INT64_MAX,"maximum signed frame exact");
    require(c.project(id,CaptureFramePosition{480},{10100000},anchor(10000000,INT64_MAX)).status==S::ArithmeticOverflow,"positive signed overflow refused");
    require(c.project(id,CaptureFramePosition{0},{10100000},anchor(9999999,INT64_MAX)).status==S::ArithmeticOverflow,"fraction above maximum signed frame refused");
    require(c.project(id,CaptureFramePosition{0},{10100000},anchor(10000001,INT64_MIN)).status==S::ArithmeticOverflow,"negative floor overflow refused");
    auto half=config(ClockUnit::RenderTicks,20000000); half.minimumSpan100ns=1;
    PreparedClockCorrelation tie(half); tie.observe(render(100,10000000,20000000));
    require(tie.observe(render(104,10000002,20000000))==S::Ready,"sub-QPC tick fit");
    require(tie.project(id,RenderTickPosition{101},{10000002},anchor()).estimatedQpc.value==10000000,"positive tie earlier");
    require(tie.project(id,RenderTickPosition{99},{10000002},anchor()).estimatedQpc.value==9999999,"negative tie earlier");
    PreparedClockCorrelation gap(config()); fit(gap);
    require(gap.observe(capture(96480,30100000))==S::WaitingSpan && !gap.snapshot(),"long gap reanchors without stale fit");
    require(gap.observe(capture(96960,30200000))==S::Ready,"post-gap fit");
    require(gap.firstReading()->qpc.value==10000000 && gap.snapshot()->begin.qpc.value==30100000,"original reading separate from fresh fit");
    // Product >64 bits and quotient >64 bits must refuse, never wrap.
    auto low=config(ClockUnit::RenderTicks,1); low.minimumSpan100ns=1;
    PreparedClockCorrelation overflow(low); overflow.observe(render(1,1,1));
    require(overflow.observe(render(UINT64_MAX,2,1))==S::ArithmeticOverflow,"wide quotient overflow refused");
    // A 64-bit denominator and wide product exercise division carry handling.
    auto high=config(ClockUnit::RenderTicks,UINT64_MAX);
    PreparedClockCorrelation wide(high); wide.observe(render(1,10000000,UINT64_MAX));
    require(wide.observe(render(UINT64_MAX,20000000,UINT64_MAX))==S::Ready,"full native range fit");
    const auto w=wide.project(id,RenderTickPosition{UINT64_MAX/2},{20000000},anchor());
    require(w.status==S::Ready && w.estimatedQpc.value==15000000,"wide denominator exact mid-point rounding");
    require(w.project==FractionalProjectFrame{24000,0},"wide product project mapping");
    PreparedClockCorrelation upper(config());
    upper.observe(capture(0,UINT64_MAX-100000)); upper.observe(capture(480,UINT64_MAX));
    require(upper.project(id,CaptureFramePosition{481},{UINT64_MAX},anchor()).status==S::ArithmeticOverflow,"estimated QPC addition overflow");
    PreparedClockCorrelation lower(config());
    lower.observe(capture(480,1)); lower.observe(capture(960,100001));
    require(lower.project(id,CaptureFramePosition{479},{100001},anchor()).status==S::ArithmeticOverflow,"estimated QPC subtraction underflow");
}
void admissionAndRealtime() {
    for (unsigned n=0;n<12;++n) {
        auto c=config();
        switch(n) {
        case 0:c.identity.endpoint=0;break; case 1:c.identity.stream=0;break;
        case 2:c.identity.generation=0;break; case 3:c.runGeneration=0;break;
        case 4:c.frequency=7999;break; case 5:c.frequency=384001;break;
        case 6:c.minimumSpan100ns=0;break; case 7:c.maximumSpan100ns=999;break;
        case 8:c.maximumAge100ns=0;break; case 9:c.maximumExtrapolation100ns=10000001;break;
        case 10:c.timestampUncertainty100ns=10000001;break; case 11:c.maximumDriftPpm=100001;break;
        }
        refuses([=]{PreparedClockCorrelation bad(c);});
    }
    static_assert(std::is_trivially_copyable_v<ClockFitSnapshot>);
    PreparedClockCorrelation clock(config()); clock.observe(capture(0,10000000));
    PreparedClockCorrelation fault(config()); fit(fault);
    rt_audit::reset(); bool ok=true; rt_audit::active=true;
    for (std::uint64_t n=1;n<=20000;++n) {
        const auto qpc=10000000+n*100000, frames=n*480;
        ok &= clock.observe(capture(frames,qpc))==S::Ready;
        const auto p=clock.project(id,CaptureFramePosition{frames},{qpc},anchor());
        ok &= p.status==S::Ready && p.project==FractionalProjectFrame{Frame(frames),0};
        ok &= clock.snapshot().has_value() && !clock.firstFault();
    }
    ok &= fault.observe(capture(960,10200000,true))==S::Discontinuity;
    ok &= fault.project(id,CaptureFramePosition{960},{10200000},anchor()).status==S::Discontinuity;
    ok &= fault.observe(capture(1440,10300000))==S::Discontinuity;
    rt_audit::active=false;
    require(ok,"rolling fit long-session RT mapping");
    require(rt_audit::counts.cppAllocate==0 && rt_audit::counts.cppFree==0 &&
            rt_audit::counts.cAllocate==0 && rt_audit::counts.cFree==0 &&
            rt_audit::counts.blockingLock==0,"RT allocation/free/blocking-lock audit");
    std::cout << "20000 rolling RT observations/projections: allocations=0 frees=0 blockingLocks=0\n";
}
}
int main() {
    try {
        goldenVectors(); domainsAndDrift(); reliabilityAndFaults(); boundsAndRounding(); admissionAndRealtime();
        std::cout << checks << " clock correlation checks passed (768 independent Fraction vectors)\n";
        return 0;
    } catch(const std::exception &e) {
        rt_audit::active=false; std::cerr << "Check " << checks << ": " << e.what() << '\n'; return 1;
    }
}
