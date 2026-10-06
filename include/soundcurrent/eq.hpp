// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include "session.hpp"
#include "spsc_queue.hpp"
#include <array>
#include <memory>
#include <span>

namespace soundcurrent::daw {
inline constexpr std::size_t maxEqBands = 64;
inline constexpr std::size_t maxEqTimedEventsPerBlock = 1024;
inline constexpr std::size_t eqImmediateQueueCapacity = 128;
inline constexpr std::size_t maxEqEventsPerBlock =
    maxEqTimedEventsPerBlock + eqImmediateQueueCapacity;
inline constexpr std::size_t eqEventQueueCapacity = 4096;
struct BiquadCoefficients {
    // b0, b1, b2, a1, a2; normalized a0 = 1.
    std::array<double, 5> values{1, 0, 0, 0, 0};
    bool operator==(const BiquadCoefficients &) const = default;
};
// Control/preparation side: mathematical transforms and diagnostic exceptions.
BiquadCoefficients preparePeakingCoefficients(double frequencyHz, double gainDb, double q,
                                              std::uint32_t sampleRate);
enum class EqEventKind : std::uint8_t { Band, Enable };
struct EqEvent {
    Frame frame = 0;
    std::uint64_t generation = 0;
    BiquadCoefficients coefficients;
    std::uint16_t band = 0;
    EqEventKind kind = EqEventKind::Band;
};
static_assert(sizeof(EqEvent) <= 64);
static_assert(std::is_trivially_copyable_v<EqEvent>);
enum class ProcessStatus {
    Ok,
    InvalidBuffer,
    InvalidEvent,
    EventBudgetExceeded,
    TimingError,
    Stopped
};
struct EqReport {
    ProcessStatus status = ProcessStatus::Ok;
    std::uint32_t eventsApplied = 0;
    std::uint64_t invalidInputSamples = 0;
    std::uint64_t numericFaultSamples = 0;
    double peak = 0;
};
enum class TailKind { UntilSilent };
struct EqMetadata {
    std::string_view typeId = "sc.eq";
    std::uint32_t processorVersion = 1;
    std::uint32_t latencyFrames = 0;
    TailKind tail = TailKind::UntilSilent;
    bool supportsInPlace = true;
    bool supportsOffline = true;
};

class PreparedEq {
  public:
    // All construction/allocation/validation occurs before callback activation.
    PreparedEq(const Session &, const Id &trackId, std::uint32_t maxFrames,
               std::uint64_t generation, double smoothingMs = 10);
    PreparedEq(const PreparedEq &) = delete;
    PreparedEq &operator=(const PreparedEq &) = delete;
    PreparedEq(PreparedEq &&) = delete;
    PreparedEq &operator=(PreparedEq &&) = delete;
    ~PreparedEq() = default; // Stop the callback before destroying this object.

    EqEvent parameterEvent(const Session &updated, const ParameterAddress &, Frame at) const;
    EqEvent enableEvent(bool enabled, Frame at) const;
    bool validEvent(const EqEvent &) const noexcept; // Reads immutable mapping only.
    EqReport process(std::span<const float *const> input, std::span<float *const> output,
                     std::uint32_t frames, Frame start,
                     std::span<const EqEvent> events = {}) noexcept;
    // Initial state/reset is controlled while stopped; never concurrently process.
    void reset() noexcept;
    std::uint32_t sampleRate() const noexcept {
        return sampleRate_;
    }
    std::uint32_t channels() const noexcept {
        return channels_;
    }
    std::uint32_t maxFrames() const noexcept {
        return maxFrames_;
    }
    std::uint32_t smoothingFrames() const noexcept {
        return smoothingFrames_;
    }
    std::uint64_t generation() const noexcept {
        return generation_;
    }
    static constexpr EqMetadata metadata() noexcept {
        return {};
    }

  private:
    struct State {
        double z1 = 0, z2 = 0;
    };
    struct Ramp {
        BiquadCoefficients current, target, step;
        std::uint32_t remaining = 0;
    };
    void apply(const EqEvent &) noexcept;
    void advance() noexcept;
    void resetChannel(std::uint32_t channel) noexcept;
    Id trackId_, processorId_;
    ChannelLayout layout_;
    std::uint32_t sampleRate_, channels_, maxFrames_, smoothingFrames_;
    std::uint64_t generation_;
    std::vector<Id> bandIds_;
    std::vector<Ramp> bands_;
    std::vector<State> states_; // band-major, channels stride; touched in preparation.
    double wet_ = 1, wetTarget_ = 1, wetStep_ = 0;
    std::uint32_t wetRemaining_ = 0;
    std::uint32_t activeBandRamps_ = 0; // Audio owner; 0..maxEqBands.
};

enum class SubmitStatus { Accepted, Full, OutOfOrder, Invalid };
struct ImmediateAcknowledgement {
    std::uint64_t generation = 0, revision = 0;
    Frame frame = 0;                 // First sample of the block where smoothing started.
    std::uint32_t eventsApplied = 0; // Prefix ending at revision, in this block.
};
static_assert(std::atomic<std::uint64_t>::is_always_lock_free);
// One session-owner producer, one audio-owner consumer. Queue never overwrites.
class EqLiveDriver {
  public:
    explicit EqLiveDriver(PreparedEq &eq, Frame start = 0);
    SubmitStatus submit(const EqEvent &) noexcept;
    // Control edits: frame is ignored; generation/coefficients remain validated.
    // Positive revisions strictly increase on accepted submissions only.
    SubmitStatus submitImmediate(const EqEvent &, std::uint64_t revision) noexcept;
    // One control consumer; lossy diagnostics never backpressure audio.
    bool acknowledgement(ImmediateAcknowledgement &) noexcept;
    std::uint64_t droppedAcknowledgements() const noexcept;
    EqReport process(std::span<const float *const> input, std::span<float *const> output,
                     std::uint32_t frames) noexcept;
    Frame frame() const noexcept {
        return frame_;
    } // Audio owner only.
    bool stopped() const noexcept {
        return stopped_;
    } // Audio owner only.
  private:
    PreparedEq &eq_;
    SpscQueue<EqEvent, eqEventQueueCapacity> queue_;
    struct ImmediateUpdate {
        EqEvent event;
        std::uint64_t revision = 0;
    };
    SpscQueue<ImmediateUpdate, eqImmediateQueueCapacity> immediate_;
    SpscQueue<ImmediateAcknowledgement, 64> acknowledgements_;
    std::atomic<std::uint64_t> droppedAcknowledgements_{0};
    std::uint64_t lastImmediateRevision_ = 0; // Producer owns.
    std::array<EqEvent, maxEqEventsPerBlock> ready_{};
    Frame lastSubmitted_ = 0; // Producer owns; equal timestamps retain ingress order.
    bool submitted_ = false;
    Frame frame_; // Consumer owns.
    bool stopped_ = false;
};
} // namespace soundcurrent::daw
