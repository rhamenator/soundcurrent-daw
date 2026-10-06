// SPDX-License-Identifier: GPL-3.0-only
// Modified 2026-10-05: response-only types; see reuse/equipment/provenance.json.
#pragma once
namespace soundcurrent::daw::equipment {
enum class FilterType { Peaking, LowShelf, HighShelf };
struct EqBand {
    double frequency, gainDb, q;
    FilterType type = FilterType::Peaking;
    bool operator==(const EqBand &) const = default;
};
struct FilterCoefficients {
    double b0, b1, b2, a1, a2;
};
FilterCoefficients filterCoefficients(const EqBand &, int sampleRate);
double filterResponseDb(const EqBand &, int sampleRate, double frequency);
} // namespace soundcurrent::daw::equipment
