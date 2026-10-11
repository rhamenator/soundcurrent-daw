// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include "wasapi_render.hpp"
namespace soundcurrent::daw {
// Exact floor(ticks * 10,000,000 / frequency), with checked uint64 result.
// Pure conversion for the independent wait clock, NEVER a replacement SDK reading.
// Zero frequency/overflow return empty; no floating point, allocation or lock.
std::optional<Qpc100ns> convertWasapiQpc(std::uint64_t ticks,std::uint64_t frequency) noexcept;
struct WasapiRenderLifecycleCallbacks {
    void *context = nullptr;
    void (*finished)(void *, const WasapiRenderEnd &) noexcept = nullptr;
    bool (*shouldStop)(void *) noexcept = nullptr;
};
// ONE SDK owner. Only actual SDK results are supplied. No PCM backing retained,
// mapping, extrapolation, native APIs, allocation, lock, I/O or log. Preparation
// and retirement off RT; end callback after SDK Stop/exit, before join. External
// callback context outlives that join. Joined reads do not race the SDK owner.
class PreparedWasapiRenderLifecycle {
  public:
    explicit PreparedWasapiRenderLifecycle(WasapiRenderLifecycleCallbacks c = {},
        std::optional<WasapiWarmupLimits> limits = {}) noexcept : callbacks_(c) { end_.warmupLimits=limits; }
    void prepared(WasapiPreparedRenderFormat) noexcept;
    bool shouldStop() const noexcept;
    void started() noexcept;
    void padding(std::uint32_t actualFrames) noexcept;
    void contentReleased(WasapiRenderLeaseResult) noexcept;
    bool beforeLease(WasapiPreLeaseAction,std::uint32_t,const WasapiRenderClock &,Qpc100ns) noexcept;
    void warmupReleased(WasapiWarmupLeaseResult) noexcept;
    void silenceReleased(WasapiSilenceLeaseResult) noexcept;
    void queueDrained() noexcept;
    void stopped(std::int32_t actualHresult) noexcept;
    void finish(std::optional<WasapiStreamFailure>, bool ownerException = false) noexcept;
    std::optional<WasapiRenderEnd> afterJoin() const noexcept {
        return ended_ ? std::optional(end_) : std::nullopt;
    }
  private:
    WasapiRenderLifecycleCallbacks callbacks_;
    WasapiRenderEnd end_;
    bool prepared_ = false, ended_ = false;
    std::optional<WasapiWarmupLeaseResult> pendingWarmup_;
    void add(std::uint64_t &, std::uint32_t) noexcept;
};
} // namespace soundcurrent::daw
