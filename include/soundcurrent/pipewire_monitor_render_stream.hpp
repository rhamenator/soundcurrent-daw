// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include "pipewire_filter.hpp"
#include "pipewire_monitor_render.hpp"
namespace soundcurrent::daw {
struct PipeWireMonitorRenderStreamOptions {
    std::string nodeName = "sc-daw-native-render-"+Id::generate().str();
    std::chrono::milliseconds readyTimeout{3000};
    PipeWireMonitorRenderOptions render;
    void *auditContext = nullptr;
    void (*beginCallback)(void *) noexcept = nullptr;
    void (*endCallback)(void *) noexcept = nullptr;
};
// Genuine output-only stream, explicit inactive route. The producer/bank/clock
// consumer are external owners. Completed monitor content means native queue
// acceptance, not speaker playback/drain. Join before reading end/retiring storage.
class PipeWireMonitorRenderStream {
  public:
    PipeWireMonitorRenderStream(PreparedMonitorOutputBank &,PreparedMonitorClockHandoff &,
                               PipeWireMonitorRenderStreamOptions = {});
    ~PipeWireMonitorRenderStream();
    PipeWireMonitorRenderStream(const PipeWireMonitorRenderStream &) = delete;
    PipeWireMonitorRenderStream &operator=(const PipeWireMonitorRenderStream &) = delete;
    std::vector<PipeWirePort> ports() const;
    void connectOutputs(const std::vector<PipeWirePort> &);
    void activate();
    void requestStop() noexcept;
    void stop() noexcept;
    bool contentComplete() const noexcept;
    std::optional<PipeWireRenderFault> firstFault() const noexcept;
    std::optional<AudioBridgeStatus> firstUnavailable() const noexcept;
    std::optional<PipeWireRenderEnd> endAfterJoin() const noexcept;
    std::uint32_t nodeId() const noexcept;
  private:
    struct State;
    std::unique_ptr<State> state_;
};
} // namespace soundcurrent::daw
