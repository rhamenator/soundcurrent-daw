// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include "monitor_buffer.hpp"
#include "capture_fanout.hpp"
#include "wasapi_capture_lifecycle.hpp"

namespace soundcurrent::daw {
struct MonitorCaptureStopConfig {
    std::uint64_t runGeneration = 1;
    std::uint32_t sourceRate = 48000, channels = 1, maximumPacketFrames = 64,
        monitorSlots = 32, inputCapacityFrames = 4096, targetQueuedSourceFrames = 1024,
        warmupInputFrames = 468;
    std::size_t memoryBudgetBytes = 65536;
    std::optional<ResourceLedger> resources;
};
enum class MonitorCaptureStopKind { Horizon, Cancel, Fault };
struct MonitorCaptureStopRequest {
    std::uint64_t runGeneration = 0;
    MonitorCaptureStopKind kind = MonitorCaptureStopKind::Fault;
    std::optional<std::uint64_t> plannedMonitorEnd;
    std::uint64_t generatedMonitorEnd = 0, acceptedMonitorInputFrames = 0;
};
enum class MonitorCaptureEndStatus { Healthy, NativeFailure, InvalidReceipt, OwnerLost };
struct MonitorCaptureEndReceipt {
    std::uint64_t runGeneration = 0;
    WasapiCaptureEnd original;
    MonitorCaptureEndStatus status = MonitorCaptureEndStatus::OwnerLost;
};
std::uint64_t monitorCaptureStopRequiredInputFrames(const MonitorCaptureStopConfig &);
std::size_t monitorCaptureStopPayloadBytes(const MonitorCaptureStopConfig &);
// Backend-neutral processing-side contract. Native adapters retain their own
// original end receipts; processing reads only an acquired completion status.
// Methods perform bounded fixed-cell work and never activate/stop an SDK.
class MonitorCaptureStopHandoff {
  public:
    virtual ~MonitorCaptureStopHandoff() = default;
    virtual bool request(const MonitorCaptureStopRequest &) noexcept = 0;
    virtual bool requested() const noexcept = 0;
    virtual std::optional<MonitorCaptureEndStatus> endStatus() const noexcept = 0;
    virtual const MonitorCaptureStopConfig &config() const noexcept = 0;
};
// TWO owners: processing publishes ONE immutable stop request; SDK capture
// checks before EACH lease (at most one in-flight packet) and publishes ONE end.
// Fixed cells retain original requests/results. No native APIs or source/project
// mapping here. Construction/retirement off RT, after both owners join.
class PreparedMonitorCaptureStop : public MonitorCaptureStopHandoff {
  public:
    explicit PreparedMonitorCaptureStop(MonitorCaptureStopConfig);
    PreparedMonitorCaptureStop(const PreparedMonitorCaptureStop &) = delete;
    PreparedMonitorCaptureStop &operator=(const PreparedMonitorCaptureStop &) = delete;
    bool request(const MonitorCaptureStopRequest &) noexcept override; // Processing owner.
    bool requested() const noexcept override; // SDK capture owner; check before EACH lease.
    std::optional<MonitorCaptureEndStatus> endStatus() const noexcept override;
    std::optional<MonitorCaptureStopRequest> originalRequest() const noexcept;
    bool complete(const WasapiCaptureEnd &) noexcept; // Capture owner after SDK Stop/exit.
    std::optional<MonitorCaptureEndReceipt> endReceipt() const noexcept;
    void settleAfterJoin() noexcept; // Joined control; missing end = OwnerLost, not fake HRESULT.
    const MonitorCaptureStopConfig &config() const noexcept override { return config_; }
    std::size_t chargedBytes() const noexcept { return bytes_; }
  private:
    ResourceLease lease_;
    const MonitorCaptureStopConfig config_;
    MonitorCaptureStopRequest request_;
    MonitorCaptureEndReceipt end_;
    std::atomic<std::uint32_t> requested_{0}, ended_{0};
    std::size_t bytes_ = 0;
};
} // namespace soundcurrent::daw
