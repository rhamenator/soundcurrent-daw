// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include "waveform.hpp"

namespace soundcurrent::daw {
struct WaveformTicket {
    Id operation;
    std::uint64_t projectEpoch = 0, generation = 0;
    WaveformIdentity source;
    std::uint64_t baseFramesPerBin = 256;
    bool operator==(const WaveformTicket &) const = default;
};
inline constexpr std::size_t waveformReplyHeaderBytes = 156;
// Owns exactly one admitted byte allocation. Caller must also charge its receive
// bank before reading a child channel. These are payload, not RSS limits.
class WaveformReply {
  public:
    WaveformReply(WaveformReply &&other) noexcept
        : lease_(std::move(other.lease_)), data_(std::move(other.data_)),
          size_(std::exchange(other.size_, 0)) {}
    std::span<const char> bytes() const noexcept { return {data_.get(), size_}; }
  private:
    friend WaveformReply encodeWaveformReply(const Waveform &, const WaveformTicket &,
        std::uint64_t, ResourceLedger, WaveformLimits, std::stop_token);
    WaveformReply() = default;
    ResourceLease lease_;
    std::unique_ptr<char[]> data_;
    std::size_t size_ = 0;
};
// Exact admitted wire size, computed from trusted expected geometry, never a
// foreign length field. v1 uses little-endian IEEE binary64 extrema, no padding.
std::size_t waveformReplySize(const WaveformTicket &, WaveformLimits = {});
WaveformReply encodeWaveformReply(const Waveform &, const WaveformTicket &,
    std::uint64_t workerPid, ResourceLedger, WaveformLimits = {}, std::stop_token = {});
// Control worker only. Requires exact current ticket and observed PID, validates
// every finite ordered peak and coarse-level reduction before returning a model.
// Does not authenticate a foreign executable or independently re-decode media.
// Parent must supervise the shipped child, require normal exit/empty stderr,
// enforce an end-to-end deadline and recheck current epoch/identity at publication.
Waveform decodeWaveformReply(std::span<const char>, const WaveformTicket &,
    std::uint64_t observedPid, ResourceLedger, WaveformLimits = {}, std::stop_token = {});
} // namespace soundcurrent::daw
