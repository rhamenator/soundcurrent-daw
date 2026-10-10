// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include "session.hpp"
#include "warp.hpp"
#include <string_view>
namespace soundcurrent::daw {
inline constexpr std::string_view protectedWarpProcessorId="soundcurrent.stretch-transient-protected-rubberband4-r3-v1";
inline constexpr std::string_view stretchProcessorId = "soundcurrent.stretch-rubberband4-r3-positioned-v2";
inline constexpr std::string_view unityStretchProcessorId = "soundcurrent.stretch-positioned-copy-v1";
inline constexpr std::string_view regionStretchProcessorId = "soundcurrent.stretch-rubberband4-r3-region-v1";
inline constexpr std::string_view regionCopyProcessorId = "soundcurrent.stretch-positioned-copy-region-v1";
inline constexpr std::string_view stretchRegionMapId = "soundcurrent.stretch-region-nominal-v1";
std::string_view stretchProcessorFor(const StretchSettings &,std::optional<StretchContext> = {},const std::optional<WarpSettings> & = {});
void validateStretchProcessor(std::string_view, const StretchSettings &,std::optional<StretchContext> = {},const std::optional<WarpSettings> & = {});
void validateStretchSettings(const StretchSettings &);
StretchSettings canonicalStretchSettings(StretchSettings);
Frame stretchOutputFrames(Frame inputFrames, const StretchSettings &);
// Control-side exact arithmetic; overflowing/unrepresentable intermediates refuse.
// Never use floating timing or silently round a fractional source anchor.
SourcePosition scaleSourcePosition(SourcePosition, std::uint64_t numerator,
                                   std::uint64_t denominator);
Frame scaleStretchFrame(Frame, std::uint64_t numerator, std::uint64_t denominator,
                        bool ceiling);
struct StretchGeometry {
    SourcePosition inputOrigin;
    Frame inputFrames=0, outputFrames=0;
    SourcePosition visibleBegin, visibleEnd;
    std::uint64_t mapNumerator=1, mapDenominator=1;
};
// Off-RT exact geometry. The visible crop uses the nominal ratio for new
// regions; legacy artifacts retain their original rounded-output map.
StretchGeometry stretchGeometry(const ClipStretchAnchor &,Frame availableSourceFrames);
bool sourcePositionLess(SourcePosition,SourcePosition);
SourcePosition subtractSourcePosition(SourcePosition,SourcePosition);
SourcePosition stretchSourceToOutput(const ClipStretchAnchor &,SourcePosition);
SourcePosition stretchOutputToSource(const ClipStretchAnchor &,SourcePosition);
void validateClipStretch(const ClipStretchAnchor &, const Asset &source, const Asset &rendered);
void validateStretchClipWindow(const Clip &,const Asset &source,const Asset &rendered,std::uint32_t projectRate);
struct ClipStretchPlan {
    Id trackId;
    Clip expectedClip;
    Asset source;
    ClipStretchAnchor anchor; // renderKey filled only by verified artifact adoption.
};
ClipStretchPlan prepareClipStretch(const Session &, const Id &track, const Id &clip,
                                   StretchSettings,std::optional<StretchContext> = {},const std::optional<WarpSettings> & = {});
// Used on proposed state by transactional applySessionEdits. The caller must
// independently verify the owned completion marker and audio before submission.
void adoptClipStretch(Session &, const ApplyClipStretch &);
} // namespace soundcurrent::daw
