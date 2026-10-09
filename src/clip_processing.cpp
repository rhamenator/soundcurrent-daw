// SPDX-License-Identifier: GPL-3.0-only
#include <soundcurrent/clip_processing.hpp>
#include <cmath>
#include <numbers>

namespace soundcurrent::daw {
namespace {
double fade(const ClipFade &f, Frame frame, bool out) noexcept {
    if (f.startFrame == f.endFrame) return 1;
    double phase;
    if (frame < f.startFrame) phase = 0;
    else if (frame >= f.endFrame) phase = 1;
    else {
        // Validation makes the signed duration subtraction representable.
        const auto duration = f.endFrame - f.startFrame;
        if (duration == 1) phase = .5;
        else phase = static_cast<double>(frame - f.startFrame) /
                     static_cast<double>(duration - 1);
    }
    if (out) phase = 1 - phase;
    if (phase <= 0) return 0;
    if (phase >= 1) return 1;
    if (f.shape != 1) phase = std::pow(phase, f.shape);
    switch (f.curve) {
    case ClipFadeCurve::Linear: return phase;
    case ClipFadeCurve::EqualPower: return std::sin(phase * std::numbers::pi / 2);
    case ClipFadeCurve::Smoothstep: return phase * phase * (3 - 2 * phase);
    }
    return 0; // Unreachable after control-side preparation validation.
}
}
PreparedClipProcessing::PreparedClipProcessing(ClipProcessing settings)
    :settings_(std::move(settings)) {
    validateClipProcessing(settings_);
    gain_ = settings_.muted ? 0 : std::pow(10.,settings_.gainDb / 20.) *
                                  (settings_.polarityInverted ? -1 : 1);
    unity_ = gain_ == 1 && settings_.fadeIn.startFrame == settings_.fadeIn.endFrame &&
             settings_.fadeOut.startFrame == settings_.fadeOut.endFrame;
}
double PreparedClipProcessing::gainAt(Frame frame) const noexcept {
    if (frame < 0 || gain_ == 0) return 0;
    if (unity_) return 1;
    return gain_ * fade(settings_.fadeIn,frame,false) * fade(settings_.fadeOut,frame,true);
}
} // namespace soundcurrent::daw
