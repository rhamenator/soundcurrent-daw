// SPDX-License-Identifier: GPL-3.0-only
#include <soundcurrent/comp_protected_span.hpp>
#include <soundcurrent/comp_regeneration_source.hpp>
#include <algorithm>
#include <cmath>
#include <limits>
namespace soundcurrent::daw {
namespace {
void require(bool value,const char *message,ErrorCode code=ErrorCode::InvalidState) {
    if(!value)throw ProjectError(code,message);
}
Frame acousticFrame(SourcePosition p) {
    require(p.frame>=0 && !p.fraction,"Protected comp requires integer acoustic positions",ErrorCode::UnsupportedSchema);
    return p.frame;
}
}
const ProtectedWarpPlan &CompProtectedSpanPlan::geometry() const {
    require(bool(geometry_),"Protected comp plan was moved from");return *geometry_;
}
CompProtectedSpanPlan prepareProtectedCompSpan(const CompHandleEnvelope &envelope,
    ResourceLedger ledger,const std::function<void()> &poll) {
    require(bool(poll),"Missing protected comp preparation poll");poll();const auto &e=envelope.data();
    require(!e.expected.compHandles,"Legacy protected handle windows require an adapter",ErrorCode::UnsupportedSchema);
    const auto &original=e.regeneration?e.regeneration->data().originalNominalClip:e.expected;
    require(original.stretch && original.stretch->warp,"Protected comp requires original protected anchors");
    if(e.regeneration){const auto &history=e.regeneration->data();
        require(history.bakedPitchScale==SourcePosition{1,0,1} && history.backendClockScale==SourcePosition{1,0,1},
            "Protected history requires unchanged pitch and sample clocks",ErrorCode::UnsupportedSchema);
        require(history.raw==e.raw && history.current==e.base && history.originalNominal.has_value(),
            "Protected history lost original raw/nominal identity");
    }
    require(e.raw.sampleRate==e.base.sampleRate,"Protected comp requires an unchanged sample clock",ErrorCode::UnsupportedSchema);
    const auto &anchor=*original.stretch;
    require(!anchor.context && !anchor.settings.pitchMilliCents && anchor.settings.formantPreserved,
        "Unsupported protected comp processing settings",ErrorCode::UnsupportedSchema);
    require(e.map.size()>=2 && e.map.size()<=16388 && e.map.front().output==SourcePosition{} &&
        e.map.back().output==SourcePosition{e.outputFrames,0,1},"Invalid protected comp map endpoints");
    const auto first=e.map.front().raw,span=subtractSourcePosition(e.map.back().raw,first);
    const auto origin=acousticFrame(first),count=acousticFrame(span);
    require(count>0 && count<=1000000000 && e.outputFrames>0 && e.outputFrames<=1000000000 &&
        origin<=e.raw.frames && count<=e.raw.frames-origin,"Protected comp exceeds actual raw extent");
    PayloadCharge payload("Owned protected comp plan",SIZE_MAX);
    payload.add(sizeof(CompProtectedSpanPlan)+4096);payload.add(e.raw.relativePath.size()+e.raw.sha256.size());
    payload.add(e.map.size(),sizeof(CompHandlePoint));
    for(const auto &file:e.raw.nativeEvidence){poll();payload.add(sizeof(ProjectEvidenceFile)+256);payload.add(file.relativePath.size()+file.sha256.size());}
    auto lease=ledger.reserve(payload.bytes());
    PayloadCharge scratch("Rebased protected comp anchors",SIZE_MAX);
    scratch.add(anchor.warp->markers.size(),sizeof(WarpAnchor)+256);auto anchorCredit=ledger.reserve(scratch.bytes());
    std::vector<WarpAnchor> markers;markers.reserve(anchor.warp->markers.size());
    for(const auto &m:anchor.warp->markers){poll();
        const auto raw=addSourcePosition(anchor.sourceOrigin,m.source);
        auto output=addSourcePosition(m.output,{e.prefixFrames,0,1});
        if(e.regeneration){
            // The current artifact may include several expanded generations.
            // Locate the ORIGINAL semantic anchor on its exact raw map, never
            // reinterpret a current artifact frame as a nominal output frame.
            require(!sourcePositionLess(raw,e.map.front().raw) && sourcePositionLess(raw,e.map.back().raw),
                "Protected history anchor lies outside the current span",ErrorCode::UnsupportedSchema);
            const auto right=std::upper_bound(e.map.begin(),e.map.end(),raw,[](auto x,const auto &k){return sourcePositionLess(x,k.raw);});
            require(right!=e.map.begin() && right!=e.map.end(),"Protected history anchor has no exact interval");
            const auto &a=*(right-1),&b=*right;
            const auto ratio=sourceIntervalRatio(subtractSourcePosition(b.output,a.output),subtractSourcePosition(b.raw,a.raw));
            output=addSourcePosition(a.output,scaleSourcePosition(subtractSourcePosition(raw,a.raw),ratio.numerator,ratio.denominator));
        }
        markers.push_back({m.id,subtractSourcePosition(raw,first),output});
    }
    auto geometry=std::make_unique<ProtectedWarpPlan>(ClipWarpRegion{first,e.raw.sampleRate,e.raw.frames,
        count,e.outputFrames,{}, {count,0,1}},markers,anchor.warp->protection,ledger);
    poll();validateProtectedWarpRender(*geometry,e.raw.sampleRate,e.raw.layout.channels);
    // Equality at the union of piecewise knots proves identical interpolation;
    // comparing only the semantic anchors would omit protected boundaries.
    for(const auto &point:e.map){poll();require(addSourcePosition(first,geometry->map().outputToSource(point.output))==point.raw,
        "Protected comp differs at an original map knot",ErrorCode::MediaMismatch);}
    for(const auto &point:geometry->map().points()){
        poll();const auto out=point.output;
        auto right=std::upper_bound(e.map.begin(),e.map.end(),out,[](auto x,const auto &k){return sourcePositionLess(x,k.output);});
        SourcePosition raw;
        if(right==e.map.end())raw=e.map.back().raw;
        else {require(right!=e.map.begin(),"Protected comp knot precedes original map");const auto &a=*(right-1),&b=*right;
            const auto ratio=sourceIntervalRatio(subtractSourcePosition(b.raw,a.raw),subtractSourcePosition(b.output,a.output));
            raw=addSourcePosition(a.raw,scaleSourcePosition(subtractSourcePosition(out,a.output),ratio.numerator,ratio.denominator));}
        require(raw==addSourcePosition(first,point.source),"Protected comp changes an original map segment",ErrorCode::MediaMismatch);
    }
    poll();return {std::move(lease),e.raw,e.map,std::move(geometry)};
}
CompProtectedSpanReport renderProtectedCompSpan(const CompProtectedSpanPlan &plan,
    const WarpRead &read,const WarpWrite &write,ResourceLedger ledger,const std::function<void()> &poll) {
    require(bool(read) && bool(write) && bool(poll),"Missing protected comp callbacks");poll();
    const auto &geometry=plan.geometry();const auto &source=plan.source();const auto region=geometry.map().region();
    const auto origin=acousticFrame(region.rawOrigin);const auto channels=source.layout.channels;
    require(channels>0 && channels<=256 && !source.relativePath.empty() && !source.sha256.empty(),"Invalid protected comp source declaration");
    CompProtectedSpanReport report;
    const auto checkedRead=[&](Frame at,std::span<float> samples){
        poll();require(at>=0 && samples.size()%channels==0 && samples.size()/channels<=512 &&
            at<=region.inputFrames-Frame(samples.size()/channels),"Protected comp read exceeds planned source",ErrorCode::MediaMismatch);
        std::fill(samples.begin(),samples.end(),std::numeric_limits<float>::quiet_NaN());
        read(origin+at,samples);poll();
        for(float value:samples)require(std::isfinite(value),"Incomplete/nonfinite protected comp source",ErrorCode::MediaMismatch);
    };
    const auto checkedWrite=[&](std::span<const float> samples){
        poll();require(samples.size()%channels==0 && samples.size()/channels<=512 &&
            Frame(samples.size()/channels)<=region.outputFrames-report.writtenFrames,
            "Protected comp output exceeds exact duration",ErrorCode::MediaMismatch);
        for(float value:samples){require(std::isfinite(value),"Nonfinite protected comp output",ErrorCode::MediaMismatch);
            report.floatPeak=std::max(report.floatPeak,std::abs(double(value)));}
        write(samples);report.writtenFrames+=Frame(samples.size()/channels);poll();
    };
    const auto rendered=renderProtectedWarp(geometry,source.sampleRate,channels,checkedRead,checkedWrite,ledger,poll);
    require(rendered.writtenFrames==report.writtenFrames && report.writtenFrames==region.outputFrames,
        "Protected comp did not drain exact duration",ErrorCode::MediaMismatch);
    report.processedGaps=rendered.processedGaps;return report;
}
} // namespace soundcurrent::daw
