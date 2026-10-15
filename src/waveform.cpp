// SPDX-License-Identifier: GPL-3.0-only
#include <soundcurrent/waveform.hpp>
#include <algorithm>
#include <limits>
#include <utility>

namespace soundcurrent::daw {
namespace {
void require(bool ok, const char *message, ErrorCode code = ErrorCode::InvalidParameter) {
    if (!ok)
        throw ProjectError(code, message);
}
std::uint64_t binsFor(std::uint64_t frames, std::uint64_t width) {
    return frames / width + (frames % width != 0);
}
void canceled(std::stop_token stop) {
    require(!stop.stop_requested(), "Waveform preparation canceled", ErrorCode::Canceled);
}
} // namespace
Waveform::Waveform(Waveform &&other) noexcept
    : reservation_(std::move(other.reservation_)), source_(std::exchange(other.source_, {})),
      levels_(other.levels_), levelCount_(std::exchange(other.levelCount_, 0)),
      peaks_(std::move(other.peaks_)) {}

std::pair<std::uint64_t, std::uint64_t> Waveform::sourceRange(std::size_t level,
                                                           std::uint64_t bin) const {
    require(level < levelCount_ && bin < levels_[level].bins, "Invalid waveform bin");
    const auto width = levels_[level].framesPerBin;
    const auto first = bin * width; // Admitted bin index implies first < frames.
    return {first, first + std::min(width, source_.frames - first)};
}
const WaveformPeak &Waveform::peak(std::size_t level, std::uint64_t bin,
                                  std::uint32_t channel) const {
    require(level < levelCount_ && bin < levels_[level].bins && channel < source_.channels,
            "Invalid waveform peak coordinate");
    return peaks_[levels_[level].sampleOffset + std::size_t(bin) * source_.channels + channel];
}

Waveform buildApprovedWaveform(ApprovedMediaFile &file, const WaveformIdentity &expected,
                               WaveformLimits limits, std::stop_token stop,
                               const std::function<void()> &beforeRead) {
    canceled(stop);
    require(limits.baseFramesPerBin && limits.maximumBaseBins && limits.maximumPayloadBytes,
            "Invalid waveform policy");
    require(expected.rate && expected.channels &&
                std::all_of(expected.sha256.begin(), expected.sha256.end(), [](char c) {
                    return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f');
                }), "Invalid expected waveform identity");
    const auto poll = [&] { canceled(stop); if (beforeRead) beforeRead(); };
    const auto extent = inspectApprovedWaveData(file, limits.validation,
                                                WaveExtentPolicy::ExactContainer, poll);
    const auto &format = extent.format;
    require(format.frames == expected.frames && format.rate == expected.rate &&
                format.channels == expected.channels,
            "Waveform source format differs from expected media", ErrorCode::MediaMismatch);
    Waveform result;
    result.source_ = expected;
    auto width = limits.baseFramesPerBin;
    auto bins = binsFor(expected.frames, width);
    require(bins <= limits.maximumBaseBins, "Waveform base-bin limit exceeded",
            ErrorCode::ResourceLimit);
    PayloadCharge charge("Waveform pyramid", limits.maximumPayloadBytes);
    charge.add(sizeof(Waveform));
    std::size_t samples = 0;
    while (bins) {
        require(result.levelCount_ < result.levels_.size(), "Waveform level limit exceeded",
                ErrorCode::ResourceLimit);
        require(bins <= std::numeric_limits<std::size_t>::max() / expected.channels,
                "Waveform bin size overflow", ErrorCode::ResourceLimit);
        const auto count = std::size_t(bins) * expected.channels;
        charge.add(count, sizeof(WaveformPeak));
        result.levels_[result.levelCount_++] = {width, bins, samples};
        samples += count; // charge.add already checked the stronger byte bound.
        if (bins == 1)
            break;
        bins = bins / 2 + bins % 2;
        width = width >= expected.frames - width ? expected.frames : width * 2;
    }
    result.reservation_ = file.resourceLedger().reserve(charge.bytes());
    result.peaks_ = std::make_unique<WaveformPeak[]>(samples);
    std::uint64_t seen = 0;
    const auto checked = validateApprovedWave(file, limits.validation, stop,
        [&](std::uint64_t first, std::span<const double> block) {
            canceled(stop);
            require(first == seen && block.size() % expected.channels == 0,
                    "Waveform decoder coordinates changed", ErrorCode::MediaMismatch);
            const auto frames = block.size() / expected.channels;
            require(frames <= expected.frames - seen, "Waveform decoded extent changed",
                    ErrorCode::MediaMismatch);
            for (std::size_t f = 0; f < frames; ++f) {
                const auto at = first + f;
                const auto bin = std::size_t(at / limits.baseFramesPerBin);
                for (std::uint32_t c = 0; c < expected.channels; ++c) {
                    auto &p = result.peaks_[bin * expected.channels + c];
                    const auto value = block[f * expected.channels + c];
                    if (at % limits.baseFramesPerBin == 0)
                        p = {value, value};
                    else {
                        p.minimum = std::min(p.minimum, value);
                        p.maximum = std::max(p.maximum, value);
                    }
                }
            }
            seen += frames;
        }, beforeRead);
    require(seen == expected.frames && checked.sourceSha256 == expected.sha256 &&
                checked.frames == expected.frames && checked.rate == expected.rate &&
                checked.channels == expected.channels,
            "Waveform source bytes differ from expected media", ErrorCode::MediaMismatch);
    for (std::size_t level = 1; level < result.levelCount_; ++level) {
        const auto &previous = result.levels_[level - 1];
        const auto &current = result.levels_[level];
        for (std::uint64_t bin = 0; bin < current.bins; ++bin) {
            if (bin % 256 == 0)
                canceled(stop);
            for (std::uint32_t c = 0; c < expected.channels; ++c) {
                auto p = result.peak(level - 1, bin * 2, c);
                if (bin * 2 + 1 < previous.bins) {
                    const auto &right = result.peak(level - 1, bin * 2 + 1, c);
                    p.minimum = std::min(p.minimum, right.minimum);
                    p.maximum = std::max(p.maximum, right.maximum);
                }
                result.peaks_[current.sampleOffset + std::size_t(bin) * expected.channels + c] = p;
            }
        }
    }
    poll();
    file.verifyUnchanged();
    canceled(stop);
    return result;
}
} // namespace soundcurrent::daw
