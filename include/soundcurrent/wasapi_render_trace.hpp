// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include "wasapi_render.hpp"
#include "resource_ledger.hpp"
#include <limits>
#include <span>
#include <vector>

namespace soundcurrent::daw {
struct WasapiRenderLeaseObservation {
    WasapiRenderClock clock{};
    std::uint64_t sequence = 0, sampleOffsetValues = 0;
    // releasedFrames is the SDK ReleaseBuffer argument, not proof of success.
    std::uint32_t requestedFrames = 0, copiedFrames = 0, releasedFrames = 0;
    std::int32_t acquireHresult = 0, releaseHresult = 0;
    WasapiRenderAction action = WasapiRenderAction::Abort;
    bool acquired = false, releaseObserved = false, samplesComplete = false;
};
// Single native writer; control reads ONLY after stop/join seals the bank.
// Not a live meter: no concurrent sample readers or reset/reuse. Allocated and
// admitted on control before native preparation, retired on control after join.
// Only content leases are sampled; native startup/end silence has separate
// counters. Abort never reads possibly unwritten SDK memory. Loss is explicit.
class WasapiRenderTrace {
  public:
    static constexpr std::size_t maximumRows = 2048, maximumSampleValues = 8*1024*1024;
    static constexpr std::size_t invalidToken = std::numeric_limits<std::size_t>::max();
    WasapiRenderTrace(std::uint32_t channels, std::uint32_t maximumFrames,
                      std::uint32_t sampleFrames, std::size_t rows, ResourceLedger);
    WasapiRenderTrace(const WasapiRenderTrace &) = delete;
    WasapiRenderTrace &operator=(const WasapiRenderTrace &) = delete;
    WasapiRenderTrace(WasapiRenderTrace &&) = delete;
    WasapiRenderTrace &operator=(WasapiRenderTrace &&) = delete;
    bool prepare(std::uint32_t channels, std::uint32_t maximumFrames) noexcept;
    std::size_t beforeRelease(const WasapiRenderClock &, std::uint32_t frames,
                             WasapiRenderAction, std::span<const float>) noexcept;
    void released(std::size_t token, std::int32_t hresult, std::uint32_t frames) noexcept;
    void acquireFailed(const WasapiRenderClock &, std::uint32_t frames, std::int32_t hresult) noexcept;
    void sealAfterJoin() noexcept;
    std::span<const WasapiRenderLeaseObservation> observations() const;
    std::span<const float> samples() const;
    // These counters also require quiescence, not cross-thread polling.
    std::uint64_t lostRows() const noexcept { return lostRows_; }
    std::uint64_t lostSampleFrames() const noexcept { return lostSampleFrames_; }
    bool malformed() const noexcept { return malformed_; }
    std::size_t chargedBytes() const noexcept { return lease_.bytes(); }
  private:
    std::size_t append(const WasapiRenderClock &, std::uint32_t) noexcept;
    void requireSealed() const;
    ResourceLease lease_;
    std::uint32_t channels_, maximumFrames_;
    std::vector<WasapiRenderLeaseObservation> rows_;
    std::vector<float> samples_;
    std::size_t usedRows_ = 0, usedValues_ = 0, pending_ = invalidToken;
    std::uint64_t next_ = 0, lostRows_ = 0, lostSampleFrames_ = 0;
    bool prepared_ = false, sealed_ = false, malformed_ = false;
};
} // namespace soundcurrent::daw
