// SPDX-License-Identifier: GPL-3.0-only
#include "dsp.h"

#include <algorithm>
#include <cmath>
#include <complex>
#include <numbers>
#include <stdexcept>

namespace soundcurrent {

namespace {
constexpr double pi = std::numbers::pi;

double responseDb(const std::array<FilterCoefficients, kMaxProcessingBands> &coefficients,
                  std::size_t count, int sampleRate, double frequency) {
    const auto z = std::polar(1.0, -2.0 * pi * frequency / sampleRate);
    double sum = 0.0;
    for (std::size_t i = 0; i < count; ++i) {
        const auto &c = coefficients[i];
        const auto numerator = c.b0 + c.b1 * z + c.b2 * z * z;
        const auto denominator = 1.0 + c.a1 * z + c.a2 * z * z;
        sum += 20.0 * std::log10(std::abs(numerator / denominator));
    }
    return sum;
}
} // namespace

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
    case FilterType::HighPass:
        b0 = (1 + cosine) / 2; b1 = -(1 + cosine); b2 = b0;
        a0 = 1 + alpha; a1 = -2 * cosine; a2 = 1 - alpha;
        break;
    default:
        b0 = 1 + alpha * a; b1 = -2 * cosine; b2 = 1 - alpha * a;
        a0 = 1 + alpha / a; a1 = -2 * cosine; a2 = 1 - alpha / a;
        break;
    }
    return {b0 / a0, b1 / a0, b2 / a0, a1 / a0, a2 / a0};
}

double filterResponseDb(const EqBand &band, int sampleRate, double frequency) {
    const auto c = filterCoefficients(band, sampleRate);
    const auto z = std::polar(1.0, -2.0 * pi * frequency / sampleRate);
    return 20.0 * std::log10(std::max(1e-15, std::abs(
        (c.b0 + c.b1 * z + c.b2 * z * z) / (1.0 + c.a1 * z + c.a2 * z * z))));
}

StereoEqualizer::StereoEqualizer(int sampleRate) : sampleRate_(sampleRate) {
    if (sampleRate < 8000 || sampleRate > 384000)
        throw std::invalid_argument("Unsupported sample rate");
}

double StereoEqualizer::Biquad::process(double input) {
    const double output = b0 * input + z1;
    z1 = b1 * input - a1 * output + z2;
    z2 = b2 * input - a2 * output;
    if (std::abs(z1) < 1e-30) z1 = 0.0;
    if (std::abs(z2) < 1e-30) z2 = 0.0;
    return output;
}

bool StereoEqualizer::setProfile(std::span<const EqBand> bands, double postGainDb,
                                 int balancePercent, bool enabled, bool automaticHeadroom) {
    if (bands.size() > kMaxBands || !std::isfinite(postGainDb) ||
        postGainDb < -12.0 || postGainDb > 12.0 ||
        balancePercent < -100 || balancePercent > 100) return false;

    std::array<FilterCoefficients, kMaxBands> coefficients{};
    for (std::size_t i = 0; i < bands.size(); ++i) {
        const auto &band = bands[i];
        if (!std::isfinite(band.frequency) || !std::isfinite(band.gainDb) ||
            !std::isfinite(band.q) ||
            band.frequency < 20.0 || band.frequency > 20000.0 ||
            band.frequency >= sampleRate_ * 0.45 ||
            band.gainDb < -24.0 || band.gainDb > 24.0 ||
            band.q < 0.1 || band.q > 20.0) return false;
        coefficients[i] = filterCoefficients(band, sampleRate_);
    }

    double peakDb = 0.0;
    for (int i = 0; i <= 512; ++i) {
        const double frequency = std::min(20.0 * std::pow(1000.0, i / 512.0),
                                          sampleRate_ * 0.45);
        peakDb = std::max(peakDb, responseDb(coefficients, bands.size(), sampleRate_, frequency));
    }
    headroomDb_ = automaticHeadroom && peakDb > 0.01 ? -(peakDb + 1.0) : 0.0;
    const double balance = balancePercent / 100.0;
    const double gain = std::pow(10.0, (headroomDb_ + postGainDb) / 20.0);
    outputFactors_ = {gain * std::min(1.0, 1.0 - balance),
                      gain * std::min(1.0, 1.0 + balance)};
    const auto oldCount = count_;
    const bool changedPower = enabled_ != enabled;
    count_ = bands.size();
    enabled_ = enabled;
    for (auto &channel : filters_)
        for (std::size_t i = 0; i < count_; ++i) {
            auto &filter = channel[i];
            const auto &c = coefficients[i];
            filter.b0 = c.b0; filter.b1 = c.b1; filter.b2 = c.b2;
            filter.a1 = c.a1; filter.a2 = c.a2;
            if (i >= oldCount || changedPower) filter.reset();
        }
    return true;
}

float StereoEqualizer::process(float *interleavedStereo, std::size_t frames) {
    if (!interleavedStereo) return 0.0f;
    double peak = 0.0;
    for (std::size_t frame = 0; frame < frames; ++frame)
        for (int channel = 0; channel < 2; ++channel) {
            double value = interleavedStereo[frame * 2 + channel];
            if (!std::isfinite(value)) value = 0.0;
            if (enabled_) {
                for (std::size_t i = 0; i < count_; ++i)
                    value = filters_[channel][i].process(value);
                value *= outputFactors_[channel];
            }
            peak = std::max(peak, std::abs(value));
            interleavedStereo[frame * 2 + channel] =
                static_cast<float>(std::clamp(value, -1.0, 1.0));
        }
    return static_cast<float>(peak);
}

void StereoEqualizer::reset() {
    for (auto &channel : filters_)
        for (auto &filter : channel) filter.reset();
}

} // namespace soundcurrent
