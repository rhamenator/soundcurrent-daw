// SPDX-License-Identifier: GPL-3.0-only
// Modified 2026-10-05: response-only extraction; see reuse/equipment/provenance.json.
#include "equipment_curve.hpp"
#include <algorithm>
#include <cmath>
#include <complex>
#include <numbers>
namespace soundcurrent::daw::equipment {
namespace {
constexpr double pi = std::numbers::pi;
}
FilterCoefficients filterCoefficients(const EqBand &band, int sampleRate) {
    const double omega = 2.0 * pi * band.frequency / sampleRate;
    const double cosine = std::cos(omega);
    const double alpha = std::sin(omega) / (2.0 * band.q);
    const double a = std::pow(10.0, band.gainDb / 40.0);
    double b0, b1, b2, a0, a1, a2;
    switch (band.type) {
    case FilterType::LowShelf: {
        const double t = 2.0 * std::sqrt(a) * alpha;
        b0 = a * ((a + 1) - (a - 1) * cosine + t);
        b1 = 2 * a * ((a - 1) - (a + 1) * cosine);
        b2 = a * ((a + 1) - (a - 1) * cosine - t);
        a0 = (a + 1) + (a - 1) * cosine + t;
        a1 = -2 * ((a - 1) + (a + 1) * cosine);
        a2 = (a + 1) + (a - 1) * cosine - t;
        break;
    }
    case FilterType::HighShelf: {
        const double t = 2.0 * std::sqrt(a) * alpha;
        b0 = a * ((a + 1) + (a - 1) * cosine + t);
        b1 = -2 * a * ((a - 1) + (a + 1) * cosine);
        b2 = a * ((a + 1) + (a - 1) * cosine - t);
        a0 = (a + 1) - (a - 1) * cosine + t;
        a1 = 2 * ((a - 1) - (a + 1) * cosine);
        a2 = (a + 1) - (a - 1) * cosine - t;
        break;
    }
    default:
        b0 = 1 + alpha * a;
        b1 = -2 * cosine;
        b2 = 1 - alpha * a;
        a0 = 1 + alpha / a;
        a1 = -2 * cosine;
        a2 = 1 - alpha / a;
        break;
    }
    return {b0 / a0, b1 / a0, b2 / a0, a1 / a0, a2 / a0};
}

double filterResponseDb(const EqBand &band, int sampleRate, double frequency) {
    const auto c = filterCoefficients(band, sampleRate);
    const auto z = std::polar(1.0, -2.0 * pi * frequency / sampleRate);
    return 20.0 * std::log10(std::max(1e-15, std::abs((c.b0 + c.b1 * z + c.b2 * z * z) /
                                                      (1.0 + c.a1 * z + c.a2 * z * z))));
}

} // namespace soundcurrent::daw::equipment
