// SPDX-License-Identifier: GPL-3.0-only
#include <soundcurrent/waveform_projection.hpp>
#include <algorithm>

namespace soundcurrent::daw {
namespace {
std::uint64_t sampleCeiling(SourcePosition p, std::uint64_t frames) {
    // SourceFrameMap guarantees nonnegative representable positions. Clamp
    // before adding the fractional carry, including a project interval in a tail.
    const auto whole = std::uint64_t(p.frame);
    return whole >= frames ? frames : whole + (p.fraction != 0);
}
}
WaveformCoverage waveformCoverage(const Waveform &wave, const Clip &clip, std::uint32_t rate,
    std::size_t level, Frame first, Frame end) {
    if (level >= wave.levels().size() || first < 0 || first > end || end > clip.lengthFrames)
        throw ProjectError(ErrorCode::InvalidParameter, "Invalid waveform viewport interval");
    const auto map = clipSourceMap(clip, wave.source().rate, rate);
    const auto begin = sampleCeiling(map.at(first), wave.source().frames);
    const auto finish = sampleCeiling(map.at(end), wave.source().frames);
    WaveformCoverage result{begin, finish};
    if (begin == finish) return result;
    const auto &layout = wave.levels()[level]; const auto width = layout.framesPerBin;
    result.firstFullBin = std::min(layout.bins, begin / width + (begin % width != 0));
    result.endFullBin = finish == wave.source().frames ? layout.bins : finish / width;
    result.endFullBin = std::max(result.firstFullBin, result.endFullBin);
    result.needsFinerLeft = begin % width != 0;
    result.needsFinerRight = finish != wave.source().frames && finish % width != 0;
    return result;
}
std::pair<long double, long double> waveformBinProjectRange(const Waveform &wave, const Clip &clip,
    std::uint32_t rate, std::size_t level, std::uint64_t bin) {
    const auto [first, end] = wave.sourceRange(level, bin);
    const auto map = clipSourceMap(clip, wave.source().rate, rate); const auto origin = map.at(0);
    const auto offset = static_cast<long double>(origin.frame) +
        static_cast<long double>(origin.fraction) / origin.denominator;
    const auto scale = static_cast<long double>(map.denominator()) / map.numerator();
    return {(static_cast<long double>(first) - offset) * scale,
            (static_cast<long double>(end) - offset) * scale};
}
} // namespace soundcurrent::daw
