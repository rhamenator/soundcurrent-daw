// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include <soundcurrent/audio_bridge.hpp>
#include <array>
#include <bit>
#include <ostream>
#include <vector>

namespace native_fixture {
// Test-only, prepared off RT, one callback writer per trace. No pointer addresses
// serialized. Retain bounded per-channel buffer identities and exact IEEE bits.
struct PortMarker {
    std::uint32_t frames = 0, bufferIdentity = UINT32_MAX;
    std::array<std::uint32_t, 8> bits{};
    bool seen = false, present = false, sampled = false;
};
struct PortMarkerRow {
    soundcurrent::daw::DeviceBlockClock clock{};
    soundcurrent::daw::Frame before = 0, after = 0;
    std::uint32_t bufferCalls = 0, generatedCalls = 0, bridgeCalls = 0;
    std::uint32_t sdkDequeues = 0, sdkQueues = 0, sdkReturned = 0, sdkQueueFailures = 0;
    bool clockKnown = false;
    std::array<PortMarker, 32> ports{};
};
class PortMarkers {
    std::vector<PortMarkerRow> rows_;
    std::array<std::array<const float *, 8>, 32> identities_{};
    std::array<std::uint32_t, 32> identityCounts_{};
    std::size_t retained_ = 0;
    std::uint64_t calls_ = 0, dropped_ = 0, identityOverflows_ = 0;
    std::uint32_t channels_, expectedCalls_, maximumFrames_;
    bool source_;
    PortMarkerRow *current_ = nullptr;
    std::uint32_t ordinal_ = 0;
    void samples(PortMarker &, const float *, std::uint32_t) noexcept;

  public:
    PortMarkers(std::size_t capacity, std::uint32_t channels, std::uint32_t expectedCalls,
                std::uint32_t maximumFrames, bool source);
    void begin() noexcept;
    void clock(const soundcurrent::daw::DeviceBlockClock &) noexcept;
    void buffer(const float *, std::uint32_t) noexcept;
    void sdkDequeue(bool returned) noexcept;
    void sdkQueue(int result) noexcept;
    void generated(std::span<float *const>, std::uint32_t) noexcept;
    void bridge() noexcept;
    void end(soundcurrent::daw::Frame before, soundcurrent::daw::Frame after) noexcept;
    void write(std::ostream &) const; // Only after callbacks/control joins.
    const PortMarkerRow &row(std::size_t n) const {
        return rows_.at(n);
    }
    std::size_t retained() const noexcept {
        return retained_;
    }
    std::uint64_t calls() const noexcept {
        return calls_;
    }
    std::uint64_t dropped() const noexcept {
        return dropped_;
    }
};
extern thread_local PortMarkers *activePortMarkers;
} // namespace native_fixture
