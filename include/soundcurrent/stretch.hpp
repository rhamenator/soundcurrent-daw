// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include "session.hpp"
#include <string_view>
namespace soundcurrent::daw {
inline constexpr std::string_view stretchProcessorId = "soundcurrent.stretch-rubberband4-r3-positioned-v2";
inline constexpr std::string_view unityStretchProcessorId = "soundcurrent.stretch-positioned-copy-v1";
std::string_view stretchProcessorFor(const StretchSettings &);
void validateStretchProcessor(std::string_view, const StretchSettings &);
void validateStretchSettings(const StretchSettings &);
StretchSettings canonicalStretchSettings(StretchSettings);
Frame stretchOutputFrames(Frame inputFrames, const StretchSettings &);
// Control-side exact arithmetic; overflowing/unrepresentable intermediates refuse.
// Never use floating timing or silently round a fractional source anchor.
SourcePosition scaleSourcePosition(SourcePosition, std::uint64_t numerator,
                                   std::uint64_t denominator);
Frame scaleStretchFrame(Frame, std::uint64_t numerator, std::uint64_t denominator,
                        bool ceiling);
void validateClipStretch(const ClipStretchAnchor &, const Asset &source, const Asset &rendered);
struct ClipStretchPlan {
    Id trackId;
    Clip expectedClip;
    Asset source;
    ClipStretchAnchor anchor; // renderKey filled only by verified artifact adoption.
};
ClipStretchPlan prepareClipStretch(const Session &, const Id &track, const Id &clip,
                                   StretchSettings);
// Used on proposed state by transactional applySessionEdits. The caller must
// independently verify the owned completion marker and audio before submission.
void adoptClipStretch(Session &, const ApplyClipStretch &);
} // namespace soundcurrent::daw
