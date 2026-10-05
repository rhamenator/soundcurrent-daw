// SPDX-License-Identifier: GPL-3.0-only
// Modified peaking subset of SoundCurrent Studio src/dsp.cpp at
// 65151a8fec1aa4b4e6c283e4514d7f3d9166fe2b. See reuse/studio/provenance.json.
#include <soundcurrent/eq.hpp>
#include <algorithm>
#include <cmath>
#include <numbers>

namespace soundcurrent::daw {
BiquadCoefficients preparePeakingCoefficients(double frequency, double gainDb, double q,
                                              std::uint32_t sampleRate) {
    if (sampleRate < 8000 || sampleRate > 384000 || !std::isfinite(frequency) ||
        !std::isfinite(gainDb) || !std::isfinite(q) || frequency < 20 || frequency > 20000 ||
        frequency >= double(sampleRate) / 2 || gainDb < -24 || gainDb > 24 || q < .1 || q > 18)
        throw ProjectError(ErrorCode::InvalidParameter, "Invalid peaking EQ parameters");
    const double omega = 2.0 * std::numbers::pi * frequency / sampleRate;
    const double cosine = std::cos(omega);
    const double alpha = std::sin(omega) / (2.0 * q);
    const double a = std::pow(10.0, gainDb / 40.0);
    const double b0 = 1 + alpha * a, b1 = -2 * cosine, b2 = 1 - alpha * a;
    const double a0 = 1 + alpha / a, a1 = -2 * cosine, a2 = 1 - alpha / a;
    BiquadCoefficients c{{b0 / a0, b1 / a0, b2 / a0, a1 / a0, a2 / a0}};
    const auto &v = c.values;
    if (!std::all_of(v.begin(), v.end(),
                     [](double x) { return std::isfinite(x) && std::abs(x) <= 64; }) ||
        !(1 + v[3] + v[4] > 0 && 1 - v[3] + v[4] > 0 && 1 - v[4] > 0))
        throw ProjectError(ErrorCode::InvalidParameter, "Unstable or unrepresentable coefficients");
    return c;
}
} // namespace soundcurrent::daw
