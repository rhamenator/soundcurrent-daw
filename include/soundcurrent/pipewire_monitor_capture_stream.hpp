// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include "pipewire_filter.hpp"
#include "pipewire_monitor_capture.hpp"

namespace soundcurrent::daw {
struct PipeWireMonitorCaptureStreamOptions {
    std::string nodeName = "sc-daw-native-capture-" + Id::generate().str();
    std::chrono::milliseconds readyTimeout{3000};
    PipeWireMonitorCaptureOptions capture;
    // Optional test instrumentation must be bounded/noexcept/RT safe.
    void *auditContext = nullptr;
    void (*beginCallback)(void *) noexcept = nullptr;
    void (*endCallback)(void *) noexcept = nullptr;
};
// Genuine input-only PipeWire filter. Routes chosen explicitly while inactive.
// The caller owns bank/clock consumers and must retire them after this stream
// joins. stop() destroys/joins the existing filter before publishing capture EOF.
// No Windows Stop/lease receipt is fabricated. Monitoring render/processing and
// cross-platform native-outcome persistence remain separate integration work.
class PipeWireMonitorCaptureStream {
  public:
    PipeWireMonitorCaptureStream(PreparedCaptureFanout &, PreparedMonitorClockHandoff &,
                                PipeWireMonitorCaptureStreamOptions = {});
    ~PipeWireMonitorCaptureStream();
    PipeWireMonitorCaptureStream(const PipeWireMonitorCaptureStream &) = delete;
    PipeWireMonitorCaptureStream &operator=(const PipeWireMonitorCaptureStream &) = delete;
    std::vector<PipeWirePort> ports() const;
    void connectInputs(const std::vector<PipeWirePort> &);
    void activate();
    void requestStop() noexcept;
    void stop() noexcept;
    std::optional<PipeWireCaptureEnd> endAfterJoin() const noexcept;
    std::uint32_t nodeId() const noexcept;
  private:
    struct State;
    std::unique_ptr<State> state_;
};
} // namespace soundcurrent::daw
