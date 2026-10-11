// SPDX-License-Identifier: GPL-3.0-only
#include <soundcurrent/clock_correlation.hpp>
#include <limits>

namespace soundcurrent::daw {
namespace {
constexpr std::uint64_t qpcRate = 10000000, signBit = std::uint64_t(1) << 63;
struct Division { std::uint64_t whole = 0, remainder = 0; bool valid = false; };
// Exact 64x64 -> 128 product, then 128/64 division. No compiler-specific integer
// extension (MSVC compatible). At most 64 division iterations, including extremes.
Division multiplyDivide(std::uint64_t a, std::uint64_t b, std::uint64_t d) noexcept {
    if (!d) return {};
    constexpr auto mask = std::uint64_t(0xffffffff);
    const auto p0 = (a & mask) * (b & mask);
    const auto p1 = (a >> 32) * (b & mask), p2 = (a & mask) * (b >> 32);
    const auto middle = (p0 >> 32) + (p1 & mask) + (p2 & mask);
    const auto high = (a >> 32) * (b >> 32) + (p1 >> 32) + (p2 >> 32) + (middle >> 32);
    const auto low = a * b; // Defined unsigned low limb, not a signed overflow.
    if (high >= d) return {}; // Quotient would exceed 64 bits.
    std::uint64_t remainder = high, quotient = 0;
    for (unsigned bit = 64; bit--;) {
        const bool carry = (remainder >> 63) != 0;
        const auto shifted = (remainder << 1) | ((low >> bit) & 1);
        const bool subtract = carry || shifted >= d;
        remainder = subtract ? shifted - d : shifted;
        quotient = (quotient << 1) | std::uint64_t(subtract);
    }
    return {quotient,remainder,true};
}
bool add(std::uint64_t a, std::uint64_t b, std::uint64_t &out) noexcept {
    if (a > UINT64_MAX-b) return false;
    out = a+b; return true;
}
std::optional<std::uint64_t> ceiling(Division d) noexcept {
    if (!d.valid || (d.remainder && d.whole == UINT64_MAX)) return {};
    return d.whole + std::uint64_t(d.remainder != 0);
}
// Encode signed frames monotonically without signed overflow, implementation-
// defined unsigned->signed conversion, or negating INT64_MIN.
std::uint64_t bias(Frame f) noexcept {
    return f >= 0 ? signBit + std::uint64_t(f)
                  : std::uint64_t(f-std::numeric_limits<Frame>::min());
}
Frame unbias(std::uint64_t f) noexcept {
    return f >= signBit ? Frame(f-signBit) : std::numeric_limits<Frame>::min()+Frame(f);
}
std::optional<FractionalProjectFrame> toFrame(Qpc100ns qpc, ProjectClockAnchor a) noexcept {
    const bool negative = qpc.value < a.qpc.value;
    const auto delta = negative ? a.qpc.value-qpc.value : qpc.value-a.qpc.value;
    const auto d = multiplyDivide(delta,a.sampleRate,qpcRate);
    if (!d.valid) return {};
    auto magnitude = d.whole;
    const auto remainder = negative && d.remainder ? qpcRate-d.remainder : d.remainder;
    if (negative && d.remainder && !add(magnitude,1,magnitude)) return {};
    auto encoded = bias(a.projectFrame);
    if (negative) { if (encoded < magnitude) return {}; encoded -= magnitude; }
    else if (!add(encoded,magnitude,encoded)) return {};
    if (encoded == UINT64_MAX && remainder) return {}; // Fraction would exceed Frame::max.
    return FractionalProjectFrame{unbias(encoded),std::uint32_t(remainder)};
}
}
PreparedClockCorrelation::PreparedClockCorrelation(ClockCorrelationConfig c) : config_(c) {
    if (!c.identity.endpoint || !c.identity.stream || !c.identity.generation || !c.runGeneration ||
        (c.unit != ClockUnit::CaptureFrames && c.unit != ClockUnit::RenderTicks) || !c.frequency ||
        (c.unit == ClockUnit::CaptureFrames && (c.frequency < 8000 || c.frequency > 384000)) ||
        !c.minimumSpan100ns || c.minimumSpan100ns > c.maximumSpan100ns ||
        c.maximumSpan100ns > qpcRate*60 || !c.maximumAge100ns || c.maximumAge100ns > qpcRate*60 ||
        c.maximumExtrapolation100ns > qpcRate || c.timestampUncertainty100ns > qpcRate ||
        c.maximumDriftPpm > 100000)
        throw ProjectError(ErrorCode::InvalidState,"Invalid clock correlation admission");
}
ClockCorrelationStatus PreparedClockCorrelation::fail(ClockCorrelationStatus s, NativeClockPoint p) noexcept {
    if (!fault_) fault_ = ClockCorrelationFault{s,p};
    ready_ = false; return fault_->status;
}
ClockCorrelationStatus PreparedClockCorrelation::observe(const CaptureClockReading &r) noexcept {
    return observe(r.identity,ClockUnit::CaptureFrames,{r.position.value,r.qpc,r.discontinuity},
                   config_.frequency,r.reliability);
}
ClockCorrelationStatus PreparedClockCorrelation::observe(const RenderClockReading &r) noexcept {
    return observe(r.identity,ClockUnit::RenderTicks,{r.position.value,r.qpc,r.discontinuity},
                   r.frequency,r.reliability);
}
ClockCorrelationStatus PreparedClockCorrelation::observe(ClockStreamIdentity id, ClockUnit unit,
    NativeClockPoint p, std::uint64_t frequency, ClockReliability reliability) noexcept {
    if (fault_) return fault_->status;
    if (id != config_.identity) return fail(ClockCorrelationStatus::IdentityChanged,p);
    if (unit != config_.unit) return fail(ClockCorrelationStatus::WrongUnit,p);
    if (frequency != config_.frequency) return fail(ClockCorrelationStatus::FrequencyChanged,p);
    if (reliability != ClockReliability::Accurate && reliability != ClockReliability::Delayed &&
        reliability != ClockReliability::Missing) return fail(ClockCorrelationStatus::InvalidReading,p);
    if (seen_ && p.discontinuity) return fail(ClockCorrelationStatus::Discontinuity,p);
    if (reliability == ClockReliability::Missing || !p.qpc.value) return ClockCorrelationStatus::MissingTimestamp;
    if (reliability == ClockReliability::Delayed) return ClockCorrelationStatus::Unreliable;
    // Render position0 may precede physical propagation. Do not anchor that
    // startup plateau to project time. Capture packet frame0 has real metadata.
    if (!seen_ && unit == ClockUnit::RenderTicks && !p.position) return ClockCorrelationStatus::StartupZero;
    if (!seen_) { first_=candidate_=last_=p; seen_=true; return ClockCorrelationStatus::WaitingSpan; }
    if (p.position < last_.position || p.qpc.value < last_.qpc.value)
        return fail(ClockCorrelationStatus::ClockReset,p);
    if (p.position == last_.position) return ClockCorrelationStatus::WaitingMotion;
    if (p.qpc.value == last_.qpc.value) return fail(ClockCorrelationStatus::InvalidReading,p);
    const auto actual = p.qpc.value-last_.qpc.value;
    const auto nominal = multiplyDivide(p.position-last_.position,qpcRate,config_.frequency);
    const auto drift = ceiling(multiplyDivide(actual,config_.maximumDriftPpm,1000000));
    std::uint64_t allowance = config_.timestampUncertainty100ns*2;
    if (!nominal.valid || !drift || !add(allowance,*drift,allowance) || !add(allowance,1,allowance))
        return fail(ClockCorrelationStatus::ArithmeticOverflow,p);
    const auto discrepancy = nominal.whole > actual ? nominal.whole-actual : actual-nominal.whole;
    if (discrepancy > allowance) return fail(ClockCorrelationStatus::ExcessiveDrift,p);
    // A long observation gap requires a fresh bounded fit; never extrapolate
    // indefinitely or silently publish the old fit as a new timestamp.
    if (actual > config_.maximumSpan100ns) {
        candidate_=last_=p; ready_=false; return ClockCorrelationStatus::WaitingSpan;
    }
    if (p.qpc.value-candidate_.qpc.value > config_.maximumSpan100ns) candidate_=last_;
    // Pairwise uncertainty must not accumulate into an arbitrarily drifting
    // published fit. Validate its endpoints with the same two-reading budget.
    const auto fitDuration = p.qpc.value-candidate_.qpc.value;
    const auto fitNominal = multiplyDivide(p.position-candidate_.position,qpcRate,config_.frequency);
    const auto fitDrift = ceiling(multiplyDivide(fitDuration,config_.maximumDriftPpm,1000000));
    allowance = config_.timestampUncertainty100ns*2;
    if (!fitNominal.valid || !fitDrift || !add(allowance,*fitDrift,allowance) ||
        !add(allowance,1,allowance)) return fail(ClockCorrelationStatus::ArithmeticOverflow,p);
    const auto fitDiscrepancy = fitNominal.whole > fitDuration ? fitNominal.whole-fitDuration
                                                            : fitDuration-fitNominal.whole;
    if (fitDiscrepancy > allowance) return fail(ClockCorrelationStatus::ExcessiveDrift,p);
    last_=p;
    if (p.qpc.value-candidate_.qpc.value < config_.minimumSpan100ns) return ClockCorrelationStatus::WaitingSpan;
    fitBegin_=candidate_; fitEnd_=p; ready_=true; return ClockCorrelationStatus::Ready;
}
std::optional<ClockFitSnapshot> PreparedClockCorrelation::snapshot() const noexcept {
    if (!ready_ || fault_) return {};
    return ClockFitSnapshot{config_.identity,config_.unit,config_.frequency,config_.runGeneration,fitBegin_,fitEnd_};
}
std::optional<NativeClockPoint> PreparedClockCorrelation::firstReading() const noexcept {
    return seen_ ? std::optional(first_) : std::nullopt;
}
ClockProjection PreparedClockCorrelation::project(ClockStreamIdentity id, CaptureFramePosition p,
    Qpc100ns asOf, ProjectClockAnchor a) const noexcept {
    return project(id,ClockUnit::CaptureFrames,p.value,asOf,a);
}
ClockProjection PreparedClockCorrelation::project(ClockStreamIdentity id, RenderTickPosition p,
    Qpc100ns asOf, ProjectClockAnchor a) const noexcept {
    return project(id,ClockUnit::RenderTicks,p.value,asOf,a);
}
ClockProjection PreparedClockCorrelation::project(ClockStreamIdentity id, ClockUnit unit,
    std::uint64_t position,Qpc100ns asOf,ProjectClockAnchor a) const noexcept {
    auto refused=[](ClockCorrelationStatus s) { ClockProjection r; r.status=s; return r; };
    if (fault_) return refused(fault_->status);
    if (id != config_.identity) return refused(ClockCorrelationStatus::IdentityChanged);
    if (unit != config_.unit) return refused(ClockCorrelationStatus::WrongUnit);
    if (!ready_) return refused(ClockCorrelationStatus::NotReady);
    if (a.runGeneration != config_.runGeneration || !a.qpc.value || a.sampleRate < 8000 || a.sampleRate > 384000)
        return refused(ClockCorrelationStatus::InvalidProjectAnchor);
    if (asOf.value < fitEnd_.qpc.value) return refused(ClockCorrelationStatus::InvalidReading);
    if (asOf.value-fitEnd_.qpc.value > config_.maximumAge100ns) return refused(ClockCorrelationStatus::Stale);
    const auto span = fitEnd_.position-fitBegin_.position;
    const auto duration = fitEnd_.qpc.value-fitBegin_.qpc.value;
    const auto outside = position < fitBegin_.position ? fitBegin_.position-position
                         : position > fitEnd_.position ? position-fitEnd_.position : 0;
    const auto extraTime = ceiling(multiplyDivide(outside,duration,span));
    if (!extraTime) return refused(ClockCorrelationStatus::ArithmeticOverflow);
    if (*extraTime > config_.maximumExtrapolation100ns) return refused(ClockCorrelationStatus::ExtrapolationExceeded);
    const bool negative = position < fitBegin_.position;
    const auto delta = negative ? fitBegin_.position-position : position-fitBegin_.position;
    const auto mapped = multiplyDivide(delta,duration,span);
    if (!mapped.valid) return refused(ClockCorrelationStatus::ArithmeticOverflow);
    // Round QPC to nearest; ties go toward the earlier absolute timestamp.
    std::uint64_t magnitude = mapped.whole;
    const bool round = negative ? mapped.remainder >= span-mapped.remainder
                                : mapped.remainder > span-mapped.remainder;
    if (round && !add(magnitude,1,magnitude)) return refused(ClockCorrelationStatus::ArithmeticOverflow);
    auto estimated = fitBegin_.qpc.value;
    if (negative) { if (estimated < magnitude) return refused(ClockCorrelationStatus::ArithmeticOverflow); estimated-=magnitude; }
    else if (!add(estimated,magnitude,estimated)) return refused(ClockCorrelationStatus::ArithmeticOverflow);
    const auto frame = toFrame({estimated},a);
    if (!frame) return refused(ClockCorrelationStatus::ArithmeticOverflow);
    const auto amplification = ceiling(multiplyDivide(config_.timestampUncertainty100ns,outside,span));
    std::uint64_t error = config_.timestampUncertainty100ns;
    if (!amplification || !add(error,*amplification,error) || !add(error,*amplification,error) ||
        !add(error,1,error)) return refused(ClockCorrelationStatus::ArithmeticOverflow);
    return {ClockCorrelationStatus::Ready,*frame,{estimated},error};
}
} // namespace soundcurrent::daw
