// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include "session.hpp"
#include <optional>

namespace soundcurrent::daw {
struct CaptureFramePosition { std::uint64_t value = 0; };
struct RenderTickPosition { std::uint64_t value = 0; };
struct Qpc100ns { std::uint64_t value = 0; };
enum class ClockUnit { CaptureFrames, RenderTicks };
enum class ClockReliability { Accurate, Delayed, Missing };
struct ClockStreamIdentity {
    std::uint64_t endpoint = 0, stream = 0, generation = 0;
    bool operator==(const ClockStreamIdentity &) const = default;
};
struct CaptureClockReading {
    ClockStreamIdentity identity;
    CaptureFramePosition position;
    Qpc100ns qpc;
    ClockReliability reliability = ClockReliability::Accurate;
    bool discontinuity = false;
};
struct RenderClockReading {
    ClockStreamIdentity identity;
    RenderTickPosition position;
    Qpc100ns qpc;
    std::uint64_t frequency = 0; // Native tick units/second, not assumed frames.
    ClockReliability reliability = ClockReliability::Accurate;
    bool discontinuity = false;
};
struct ClockCorrelationConfig {
    ClockStreamIdentity identity;
    ClockUnit unit = ClockUnit::CaptureFrames;
    std::uint64_t frequency = 48000, runGeneration = 1;
    std::uint64_t minimumSpan100ns = 100000, maximumSpan100ns = 10000000;
    std::uint64_t maximumAge100ns = 1000000, maximumExtrapolation100ns = 100000;
    std::uint64_t timestampUncertainty100ns = 1000;
    std::uint32_t maximumDriftPpm = 1000;
};
enum class ClockCorrelationStatus {
    Ready, WaitingSpan, WaitingMotion, StartupZero, Unreliable, MissingTimestamp,
    InvalidReading, IdentityChanged, WrongUnit, FrequencyChanged, ClockReset,
    Discontinuity, ExcessiveDrift, ArithmeticOverflow, NotReady, Stale,
    ExtrapolationExceeded, InvalidProjectAnchor
};
// Original readings are retained as POD; no synthesized CaptureTimingOrigin.
struct NativeClockPoint {
    std::uint64_t position = 0;
    Qpc100ns qpc;
    bool discontinuity = false;
};
struct ClockFitSnapshot {
    ClockStreamIdentity identity;
    ClockUnit unit;
    std::uint64_t frequency = 0, runGeneration = 0;
    NativeClockPoint begin, end;
};
struct ProjectClockAnchor {
    std::uint64_t runGeneration = 0;
    Qpc100ns qpc;
    Frame projectFrame = 0;
    std::uint32_t sampleRate = 48000;
    // Caller-supplied project/QPC correspondence, not proof of audible startup.
};
struct FractionalProjectFrame {
    Frame floor = 0;
    std::uint32_t numerator = 0;
    static constexpr std::uint32_t denominator = 10000000;
    bool operator==(const FractionalProjectFrame &) const = default;
};
struct ClockProjection {
    ClockCorrelationStatus status = ClockCorrelationStatus::NotReady;
    FractionalProjectFrame project;
    Qpc100ns estimatedQpc;
    std::uint64_t maximumError100ns = 0;
    // Error covers declared observation uncertainty and integer rounding only.
    // It is NOT measured hardware latency, drift-model or scheduling quality.
};
struct ClockCorrelationFault {
    ClockCorrelationStatus status;
    NativeClockPoint reading;
};
// One owner. Construction/admission off RT. observe/project/snapshot allocate no
// memory, wait on no locks, perform no I/O and have bounded integer work. Publish
// snapshot copies through a bounded queue before using them on another owner.
class PreparedClockCorrelation {
  public:
    explicit PreparedClockCorrelation(ClockCorrelationConfig);
    ClockCorrelationStatus observe(const CaptureClockReading &) noexcept;
    ClockCorrelationStatus observe(const RenderClockReading &) noexcept;
    ClockProjection project(ClockStreamIdentity, CaptureFramePosition, Qpc100ns asOf,
                            ProjectClockAnchor) const noexcept;
    ClockProjection project(ClockStreamIdentity, RenderTickPosition, Qpc100ns asOf,
                            ProjectClockAnchor) const noexcept;
    std::optional<ClockFitSnapshot> snapshot() const noexcept;
    std::optional<NativeClockPoint> firstReading() const noexcept;
    std::optional<ClockCorrelationFault> firstFault() const noexcept { return fault_; }
    // Start a new observation window at the LAST original trustworthy reading.
    // Keeps the initial reading, admitted identity/config and sticky first fault.
    // No extrapolated point or fit is fabricated. Used by overlapping fit owners.
    void restartWindow() noexcept;
  private:
    ClockCorrelationConfig config_;
    NativeClockPoint first_{}, candidate_{}, last_{}, fitBegin_{}, fitEnd_{};
    bool seen_ = false, ready_ = false;
    std::optional<ClockCorrelationFault> fault_;
    ClockCorrelationStatus observe(ClockStreamIdentity, ClockUnit, NativeClockPoint,
                                   std::uint64_t, ClockReliability) noexcept;
    ClockCorrelationStatus fail(ClockCorrelationStatus, NativeClockPoint) noexcept;
    ClockProjection project(ClockStreamIdentity, ClockUnit, std::uint64_t,
                            Qpc100ns, ProjectClockAnchor) const noexcept;
};
} // namespace soundcurrent::daw
