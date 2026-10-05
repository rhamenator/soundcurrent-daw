// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include "audio_bridge.hpp"
#include <chrono>
#include <memory>
#include <string>
#include <vector>

namespace soundcurrent::daw {
struct PipeWirePort {
    std::uint32_t nodeId = 0, portId = 0;
    std::uint64_t nodeSerial = 0;
    std::string nodeName, portName, mediaClass;
    bool input = false;
};
struct PipeWireFilterOptions {
    std::string nodeName;
    std::uint32_t inputs = 1, outputs = 1;
    // Native allocation bound; oversized quanta are never sent to callbacks.
    std::uint32_t maximumNativeFrames = 65536;
    bool alwaysProcess = false; // Test generators only; normal track is false.
};
struct PipeWireCallbacks {
    void *context = nullptr;
    void (*process)(void *, const DeviceBlockClock &, std::span<const float *const>,
                    std::span<float *const>, std::uint32_t) noexcept = nullptr;
    // Control-loop notification. Consumers may latch a bridge fault atomically.
    void (*unavailable)(void *, AudioBridgeStatus) noexcept = nullptr;
    // Optional RT-safe instrumentation; no allocating/logging hooks allowed.
    void (*beginCallback)(void *) noexcept = nullptr;
    void (*endCallback)(void *) noexcept = nullptr;
};
// Linux-specific implementation behind a framework-free public descriptor.
// No autoconnection, default-device mutation or driver/rate/quantum forcing.
class PipeWireFilter {
  public:
    PipeWireFilter(PipeWireFilterOptions, PipeWireCallbacks);
    ~PipeWireFilter();
    PipeWireFilter(const PipeWireFilter &) = delete;
    PipeWireFilter &operator=(const PipeWireFilter &) = delete;
    bool waitReady(std::chrono::milliseconds timeout);
    std::vector<PipeWirePort> ports() const; // Control-side copies under loop lock.
    // Before activation: exact channel count; no implicit channel truncation.
    void connectInputs(const std::vector<PipeWirePort> &);
    void connectOutputs(const std::vector<PipeWirePort> &);
    void activate();
    // Synchronously disconnect/destroy context/data loops before returning.
    void stop() noexcept;
    std::uint32_t nodeId() const noexcept;
    std::string diagnostic() const;
    bool memoryLocked() const noexcept;

  private:
    struct State;
    std::unique_ptr<State> state_;
};
} // namespace soundcurrent::daw
