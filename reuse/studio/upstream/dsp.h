// SPDX-License-Identifier: GPL-3.0-only
#pragma once

#include <array>
#include <cstddef>
#include <span>

namespace soundcurrent {

enum class FilterType { Peaking, LowShelf, HighShelf, HighPass, LowPass };

struct EqBand {
    double frequency;
    double gainDb;
    double q;
    FilterType type = FilterType::Peaking;
};

inline constexpr std::size_t kMaxProcessingBands = 64;
struct FilterCoefficients { double b0, b1, b2, a1, a2; };
FilterCoefficients filterCoefficients(const EqBand &band, int sampleRate);
double filterResponseDb(const EqBand &band, int sampleRate, double frequency);

// Call setProfile and process from the same audio thread. A UI thread should
// pass profile updates to that thread between blocks.
class StereoEqualizer {
public:
    explicit StereoEqualizer(int sampleRate);
    bool setProfile(std::span<const EqBand> bands, double postGainDb,
                    int balancePercent, bool enabled = true, bool automaticHeadroom = true);
    float process(float *interleavedStereo, std::size_t frames);
    void reset();
    double headroomDb() const { return headroomDb_; }

private:
    struct Biquad {
        double b0 = 1.0, b1 = 0.0, b2 = 0.0, a1 = 0.0, a2 = 0.0;
        double z1 = 0.0, z2 = 0.0;
        double process(double input);
        void reset() { z1 = z2 = 0.0; }
    };

    static constexpr std::size_t kMaxBands = kMaxProcessingBands;
    int sampleRate_;
    std::array<std::array<Biquad, kMaxBands>, 2> filters_{};
    std::size_t count_ = 0;
    std::array<double, 2> outputFactors_{1.0, 1.0};
    double headroomDb_ = 0.0;
    bool enabled_ = true;
};

} // namespace soundcurrent
