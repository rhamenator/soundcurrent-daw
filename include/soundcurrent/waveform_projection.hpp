// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include "waveform.hpp"
#include "clip_timing.hpp"

namespace soundcurrent::daw {
struct WaveformCoverage {
    // Source sample INDICES in the exact half-open project interval. These are
    // source samples, not an interpolated/resampled output waveform.
    std::uint64_t sourceBegin = 0, sourceEnd = 0;
    std::uint64_t firstFullBin = 0, endFullBin = 0;
    bool needsFinerLeft = false, needsFinerRight = false;
};
// GUI/control-side pure geometry. Uses the same exact clip time map as playback.
// Offsets are relative to clip start, preventing loss from large absolute project
// positions. Only FULL bins inside the requested sample interval are returned;
// a partial crop edge must request finer data, not reuse neighboring extrema.
WaveformCoverage waveformCoverage(const Waveform &, const Clip &, std::uint32_t projectRate,
    std::size_t level, Frame firstClipFrame, Frame endClipFrame);
// Continuous drawing coordinates relative to clip start, for the exact source
// interval represented by this bin (including a short final source bin).
// Floating coordinates are presentation only, never audio/project timing state.
std::pair<long double, long double> waveformBinProjectRange(const Waveform &, const Clip &,
    std::uint32_t projectRate, std::size_t level, std::uint64_t bin);
} // namespace soundcurrent::daw
