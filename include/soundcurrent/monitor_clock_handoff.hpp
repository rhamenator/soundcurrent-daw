// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include "clock_correlation.hpp"
#include "resource_ledger.hpp"
#include "spsc_queue.hpp"

namespace soundcurrent::daw {
enum class MonitorClockDomain { WindowsQpc100ns, PosixMonotonic100ns };
struct MonitorClockHandoffConfig {
    ClockStreamIdentity captureIdentity{1,1,1}, renderIdentity{2,1,1};
    std::uint64_t renderFrequency = 48000, runGeneration = 1, initialRecordSequence = 0;
    std::uint32_t slots = 64, initialQueueSequence = 0;
    std::size_t memoryBudgetBytes = 65536;
    std::optional<ResourceLedger> resources;
    MonitorClockDomain timestampDomain = MonitorClockDomain::WindowsQpc100ns;
};
enum class MonitorClockPhase { Running, Stopped, Cancelled, Fault };
enum class MonitorClockCode { Accepted, Ready, Empty, Drained, Stopped, Cancelled, Fault };
enum class MonitorClockCause { Identity, Frequency, Reliability, QueueFull, SequenceOverflow,
                               ProducerFailed, ConsumerLost, PeerLost };
template<class Reading> struct MonitorClockRecord {
    std::uint64_t runGeneration = 0, sequence = 0; // Handoff sequence, not a native position.
    Reading original;
};
template<class Reading> struct MonitorClockFault {
    MonitorClockCause cause;
    std::optional<Reading> attempted; // Missing on loss/settlement without a real reading.
};
struct MonitorClockRead { MonitorClockCode code; MonitorClockPhase phase; };
struct MonitorClockCounts { std::uint64_t accepted = 0, delivered = 0, discarded = 0; };
std::size_t monitorClockHandoffPayloadBytes(const MonitorClockHandoffConfig &);
// THREE owners: capture clock producer, render clock producer, processing consumer.
// Each original source uses a separate bounded SPSC route/fault cell. No SDK calls,
// conversion, extrapolation, fit or cross-source time order is manufactured here.
// Construction/retirement off RT; all owners join before control settlement/destruction.
class PreparedMonitorClockHandoff {
  public:
    static constexpr std::uint32_t capacity = 64;
    explicit PreparedMonitorClockHandoff(MonitorClockHandoffConfig);
    PreparedMonitorClockHandoff(const PreparedMonitorClockHandoff &) = delete;
    PreparedMonitorClockHandoff &operator=(const PreparedMonitorClockHandoff &) = delete;
    MonitorClockCode publish(const CaptureClockReading &) noexcept; // Capture owner.
    MonitorClockCode publish(const RenderClockReading &) noexcept;  // Render owner.
    // Distinct backend boundary: floor(CLOCK_MONOTONIC nanoseconds / 100).
    // Both lanes must use the declared POSIX domain. No cross-domain inference.
    MonitorClockCode publishMonotonic(const CaptureClockReading &) noexcept;
    MonitorClockCode publishMonotonic(const RenderClockReading &) noexcept;
    MonitorClockRead read(MonitorClockRecord<CaptureClockReading> &) noexcept; // Processing owner.
    MonitorClockRead read(MonitorClockRecord<RenderClockReading> &) noexcept;
    void finishCapture() noexcept;
    void finishRender() noexcept;
    void captureFailed() noexcept;
    void renderFailed() noexcept;
    void consumerFailed() noexcept; // Processing owner; visible even after producers finish.
    void requestStop() noexcept;    // Control; drain accepted prefix, in-flight publication may finish.
    void requestCancel() noexcept;  // Control; refuse new reads, discard only after join.
    void settleAfterJoin() noexcept;
    MonitorClockPhase phase() const noexcept;
    bool captureDone() const noexcept;
    bool renderDone() const noexcept;
    std::optional<MonitorClockFault<CaptureClockReading>> captureFault() const noexcept;
    std::optional<MonitorClockFault<RenderClockReading>> renderFault() const noexcept;
    MonitorClockCounts captureCounts() const noexcept;
    MonitorClockCounts renderCounts() const noexcept;
    const MonitorClockHandoffConfig &config() const noexcept { return config_; }
    std::size_t chargedBytes() const noexcept { return bytes_; }
  private:
    template<class Reading> struct Lane {
        SpscQueue<MonitorClockRecord<Reading>,capacity> queue;
        MonitorClockFault<Reading> fault{};
        std::uint64_t next = 0;
        std::atomic<std::uint32_t> faultReady{0}, done{0};
        std::atomic<std::uint64_t> accepted{0}, delivered{0}, discarded{0};
        Lane(std::uint32_t q, std::uint64_t sequence) : queue(q), next(sequence) {}
    };
    ResourceLease lease_; // Retires after queues, after joined owners.
    const MonitorClockHandoffConfig config_;
    Lane<CaptureClockReading> capture_;
    Lane<RenderClockReading> render_;
    std::atomic<std::uint32_t> requested_{0}, consumerLost_{0};
    std::size_t bytes_ = 0;
    template<class Reading> MonitorClockCode fail(Lane<Reading> &, MonitorClockCause,
                                                std::optional<Reading> = {}) noexcept;
    template<class Reading> MonitorClockCode publish(Lane<Reading> &, const Reading &) noexcept;
    template<class Reading> MonitorClockRead read(Lane<Reading> &, MonitorClockRecord<Reading> &) noexcept;
    template<class Reading> void finish(Lane<Reading> &) noexcept;
    template<class Reading> void settle(Lane<Reading> &) noexcept;
};
} // namespace soundcurrent::daw
