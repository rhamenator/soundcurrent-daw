// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include "wasapi_capture.hpp"
#include "monitor_capture_stop.hpp"
#include "monitor_clock_handoff.hpp"

namespace soundcurrent::daw {
struct WasapiMonitorCaptureOptions {
    std::size_t memoryBudgetBytes = 65536;
    std::optional<ResourceLedger> resources;
};
// Prepared AFTER successful SDK format/capacity admission, BEFORE activation.
// ONE capture owner; external fanout/clocks/Stop outlive all callback and consumer
// joins. No channel selection/remapping, clock fit or project anchor is invented.
class PreparedWasapiMonitorCapture {
  public:
    PreparedWasapiMonitorCapture(WasapiPreparedCaptureFormat, PreparedCaptureFanout &,
        PreparedMonitorClockHandoff &, PreparedMonitorCaptureStop &, WasapiMonitorCaptureOptions = {});
    PreparedWasapiMonitorCapture(const PreparedWasapiMonitorCapture &) = delete;
    PreparedWasapiMonitorCapture &operator=(const PreparedWasapiMonitorCapture &) = delete;
    void packet(const WasapiPacket &) noexcept;
    void unavailable(std::int32_t) noexcept;
    bool shouldStop() const noexcept;
    void finished(const WasapiCaptureEnd &) noexcept;
    std::optional<std::int32_t> firstUnavailable() const noexcept;
    bool bindable() const noexcept; // Serialized control before capture ownership transfer.
    void settleAfterJoin() noexcept; // All owners joined; missing end stays OwnerLost.
    const WasapiPreparedCaptureFormat &format() const noexcept { return format_; }
    std::size_t chargedBytes() const noexcept { return sizeof(*this); }
  private:
    ResourceLease lease_;
    const WasapiPreparedCaptureFormat format_;
    PreparedCaptureFanout &fanout_;
    PreparedMonitorClockHandoff &clocks_;
    PreparedMonitorCaptureStop &stop_;
    std::int32_t unavailable_ = 0;
    std::atomic<std::uint32_t> unavailableReady_{0};
    bool finished_ = false;
};
enum class WasapiCaptureBindingPhase : std::uint32_t { Unbound, Bound, Ending, Ended };
enum class WasapiCaptureBindingCause { UnboundPacket, PacketAfterEnd };
struct WasapiCaptureBindingFault {
    WasapiCaptureBindingCause cause;
    WasapiDeliveredPacket original; // Metadata only; callback backing never retained.
};
// Prepare before creating the SDK stream. ONE serialized control owner binds a
// fully prepared context once; ONE SDK owner calls the callbacks. The Bound/End
// CAS determines who won even when the prepared SDK owner exits during binding.
// End before bind remains original evidence and refuses bind/activation. No reset.
class PreparedWasapiCaptureBinding {
  public:
    explicit PreparedWasapiCaptureBinding(WasapiMonitorCaptureOptions = {});
    PreparedWasapiCaptureBinding(const PreparedWasapiCaptureBinding &) = delete;
    PreparedWasapiCaptureBinding &operator=(const PreparedWasapiCaptureBinding &) = delete;
    bool bind(PreparedWasapiMonitorCapture &, WasapiPreparedCaptureFormat actual); // Control, off RT.
    WasapiCaptureCallbacks callbacks() noexcept; // Table has stable context/address.
    WasapiCaptureBindingPhase phase() const noexcept;
    std::optional<WasapiCaptureEnd> originalEnd() const noexcept;
    std::optional<std::int32_t> firstUnavailable() const noexcept;
    std::optional<WasapiCaptureBindingFault> firstFault() const noexcept;
    std::size_t chargedBytes() const noexcept { return sizeof(*this); }
  private:
    ResourceLease lease_;
    PreparedWasapiMonitorCapture *context_ = nullptr; // Published by successful Bound CAS.
    std::atomic<std::uint32_t> phase_{0}, unavailableReady_{0}, faultReady_{0};
    WasapiCaptureEnd end_;
    std::int32_t unavailable_ = 0;
    WasapiCaptureBindingFault fault_{};
    static void packet(void *, const WasapiPacket &) noexcept;
    static void unavailable(void *, std::int32_t) noexcept;
    static bool shouldStop(void *) noexcept;
    static void finished(void *, const WasapiCaptureEnd &) noexcept;
};
// Windows SDK wrapper: native preparation first, then external context bind,
// then activation. Does not enable the desktop duplex factory or own DSP/disk.
class WasapiMonitorCaptureStream {
  public:
    explicit WasapiMonitorCaptureStream(WasapiCaptureOptions, WasapiMonitorCaptureOptions = {});
    ~WasapiMonitorCaptureStream();
    WasapiMonitorCaptureStream(const WasapiMonitorCaptureStream &) = delete;
    WasapiMonitorCaptureStream &operator=(const WasapiMonitorCaptureStream &) = delete;
    WasapiPreparedCaptureFormat preparedFormat() const noexcept;
    void bind(PreparedWasapiMonitorCapture &);
    void activate();
    void requestStop() noexcept;
    void stop() noexcept;
    std::optional<WasapiCaptureEnd> endAfterJoin() const noexcept;
    std::optional<WasapiCaptureBindingFault> bindingFault() const noexcept;
  private:
    struct State;
    std::unique_ptr<State> state_;
};
} // namespace soundcurrent::daw
