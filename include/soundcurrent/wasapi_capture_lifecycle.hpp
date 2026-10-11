// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include "wasapi_input.hpp"

namespace soundcurrent::daw {
struct WasapiStreamFailure {
    std::int32_t hresult = 0;
    std::uint32_t operation = 0; // 1 wait/start, 2 acquire, 3 release, 4 stop.
    bool operator==(const WasapiStreamFailure &) const = default;
};
struct WasapiDeliveredPacket {
    std::uint32_t frames = 0, flags = 0;
    std::uint64_t devicePosition = 0, qpc100ns = 0;
    bool operator==(const WasapiDeliveredPacket &) const = default;
};
struct WasapiCaptureEnd {
    bool started = false, ownerException = false, counterOverflow = false;
    std::optional<std::int32_t> stopHresult; // Absent if client->Stop was not called.
    std::optional<WasapiStreamFailure> failure;
    std::optional<WasapiDeliveredPacket> lastDelivered;
    std::uint64_t deliveredPackets = 0, deliveredFrames = 0;
    // Delivered means native packet callback invoked, not raw acceptance, device
    // backlog drain, a project end or joined SDK retirement.
};
struct WasapiCaptureLifecycleCallbacks {
    void *context = nullptr;
    bool (*shouldStop)(void *) noexcept = nullptr;
    void (*finished)(void *, const WasapiCaptureEnd &) noexcept = nullptr;
};
// ONE SDK owner. Portable bounded bookkeeping used by the actual native stream.
// Only actual Start/Stop/packet results are supplied; no clock/domain inference.
// Prepared and retired off RT. End callback runs once after SDK Stop/owner exit,
// before native thread join; its context must outlive that join.
class PreparedWasapiCaptureLifecycle {
  public:
    explicit PreparedWasapiCaptureLifecycle(WasapiCaptureLifecycleCallbacks c) noexcept : callbacks_(c) {}
    bool shouldStop() const noexcept;
    void started() noexcept;
    void delivered(const WasapiPacket &) noexcept;
    void stopped(std::int32_t actualHresult) noexcept;
    void finish(std::optional<WasapiStreamFailure>, bool ownerException = false) noexcept;
    WasapiCaptureEnd afterJoin() const noexcept { return end_; } // Joined control only.
  private:
    WasapiCaptureLifecycleCallbacks callbacks_;
    WasapiCaptureEnd end_;
    bool ended_ = false;
};
} // namespace soundcurrent::daw
