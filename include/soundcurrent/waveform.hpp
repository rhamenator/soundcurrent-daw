// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include "wave_validation.hpp"
#include <utility>

namespace soundcurrent::daw {
struct WaveformIdentity {
    std::array<char, 64> sha256{};
    std::uint64_t frames = 0;
    std::uint32_t rate = 0, channels = 0;
    bool operator==(const WaveformIdentity &) const = default;
};
struct WaveformPeak {
    double minimum = 0, maximum = 0;
    bool operator==(const WaveformPeak &) const = default;
};
struct WaveformLevel {
    std::uint64_t framesPerBin = 0, bins = 0;
    std::size_t sampleOffset = 0;
};
struct WaveformLimits {
    // Trusted worker policy. No adaptive loss of requested resolution on refusal.
    std::uint64_t baseFramesPerBin = 256;
    std::size_t maximumBaseBins = 1024 * 1024;
    std::size_t maximumPayloadBytes = 64 * 1024 * 1024;
    // Header preflight and full validation each use this policy; their I/O
    // counters are separate. The caller's deadline covers the whole operation.
    WaveValidationLimits validation;
};
// Immutable SOURCE sample extrema, not an image of resampled/processed output.
// A bin covers [first,end); partial edge bins are never padded with silence.
// Clipping/zooming inside a bin requires finer data: callers must not claim that
// the bin's extrema describe only a subrange. Channel order remains source order.
// Construction, movement and destruction are serialized worker/control work,
// never audio-callback work. The lease includes retained owned payload, not RSS.
class Waveform {
  public:
    Waveform(Waveform &&) noexcept;
    Waveform &operator=(Waveform &&) = delete;
    Waveform(const Waveform &) = delete;
    Waveform &operator=(const Waveform &) = delete;
    const WaveformIdentity &source() const noexcept { return source_; }
    std::span<const WaveformLevel> levels() const noexcept { return {levels_.data(), levelCount_}; }
    const WaveformPeak &peak(std::size_t level, std::uint64_t bin, std::uint32_t channel) const;
    std::pair<std::uint64_t, std::uint64_t> sourceRange(std::size_t level, std::uint64_t bin) const;
    std::size_t chargedBytes() const noexcept { return reservation_.bytes(); }
  private:
    friend Waveform buildApprovedWaveform(ApprovedMediaFile &, const WaveformIdentity &,
        WaveformLimits, std::stop_token, const std::function<void()> &);
    Waveform() = default;
    ResourceLease reservation_;
    WaveformIdentity source_;
    std::array<WaveformLevel, 64> levels_{};
    std::size_t levelCount_ = 0;
    std::unique_ptr<WaveformPeak[]> peaks_;
};
// Full pinned-file decode, finiteness/digest/expected-format check precedes any
// returned result. Exceptions/cancellation discard all partial peaks and credit.
// Cooperative cancellation surrounds reads and reduction blocks; OS/decoder work
// is not hard bounded. GUI callers must use an isolated child with a deadline,
// and reject results whose project epoch/media identity no longer match.
Waveform buildApprovedWaveform(ApprovedMediaFile &, const WaveformIdentity &,
    WaveformLimits = {}, std::stop_token = {}, const std::function<void()> &beforeRead = {});
} // namespace soundcurrent::daw
