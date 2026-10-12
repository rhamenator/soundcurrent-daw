// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include "monitor_capture_stop.hpp"
#include "pipewire_monitor_capture.hpp"
namespace soundcurrent::daw {
struct PipeWireCaptureEndReceipt {
    std::uint64_t generation = 0;
    std::optional<PipeWireCaptureEnd> original;
    MonitorCaptureEndStatus status = MonitorCaptureEndStatus::OwnerLost;
};
// ONE processing request producer, joined native capture end producer and
// processing/control readers. An immutable end retains actual PipeWire facts;
// missing owner is explicit, never a fabricated WASAPI Start/Stop/HRESULT.
class PreparedPipeWireCaptureStop final : public MonitorCaptureStopHandoff {
  public:
    explicit PreparedPipeWireCaptureStop(MonitorCaptureStopConfig);
    bool request(const MonitorCaptureStopRequest &) noexcept override;
    bool requested() const noexcept override;
    std::optional<MonitorCaptureEndStatus> endStatus() const noexcept override;
    const MonitorCaptureStopConfig &config() const noexcept override { return config_; }
    std::optional<MonitorCaptureStopRequest> originalRequest() const noexcept;
    bool complete(const PipeWireCaptureEnd &) noexcept; // AFTER native callbacks joined.
    std::optional<PipeWireCaptureEndReceipt> endReceipt() const noexcept;
    void settleAfterJoin() noexcept; // AFTER both owners joined; no fake original end.
  private:
    ResourceLease lease_;
    const MonitorCaptureStopConfig config_;
    MonitorCaptureStopRequest request_;
    PipeWireCaptureEndReceipt end_;
    std::atomic<std::uint32_t> requested_{0},ended_{0};
};
} // namespace soundcurrent::daw
