// SPDX-License-Identifier: GPL-3.0-only
#include <soundcurrent/warp.hpp>
#include <soundcurrent/stretch.hpp>
#include <algorithm>
#include <limits>
#include <numeric>

namespace soundcurrent::daw {
namespace {
void require(bool value, const char *message) {
    if (!value) throw ProjectError(ErrorCode::InvalidParameter, message);
}
SourcePosition canonical(SourcePosition p) {
    require(p.frame >= 0 && p.denominator && p.fraction < p.denominator,
            "Invalid warp coordinate");
    const auto divisor = std::gcd(p.fraction, p.denominator);
    p.fraction /= divisor; p.denominator /= divisor;
    return p;
}
std::uint64_t commonDenominator(SourcePosition a, SourcePosition b) {
    const auto reduced = a.denominator / std::gcd(a.denominator, b.denominator);
    require(reduced <= UINT64_MAX / b.denominator,
            "Warp denominator exceeds exact representation");
    return reduced * b.denominator;
}
SourcePosition subtract(SourcePosition a, SourcePosition b) {
    a = canonical(a); b = canonical(b);
    require(!sourcePositionLess(a, b), "Warp coordinate difference is negative");
    const auto denominator = commonDenominator(a, b);
    const auto left = a.fraction * (denominator / a.denominator);
    const auto right = b.fraction * (denominator / b.denominator);
    const bool borrow = left < right;
    require(!borrow || a.frame > b.frame, "Warp subtraction borrow underflow");
    return canonical({a.frame - b.frame - Frame(borrow),
                      borrow ? denominator - (right - left) : left - right, denominator});
}
SourcePosition evaluate(std::span<const ClipWarpPoint> points, SourcePosition position, bool inverse) {
    position = canonical(position);
    auto from = [inverse](const ClipWarpPoint &p) { return inverse ? p.output : p.source; };
    auto to = [inverse](const ClipWarpPoint &p) { return inverse ? p.source : p.output; };
    require(!sourcePositionLess(position, from(points.front())) &&
            !sourcePositionLess(from(points.back()), position), "Warp lookup is outside its domain");
    if (position == from(points.back())) return to(points.back());
    const auto right = std::upper_bound(points.begin(), points.end(), position,
        [&](SourcePosition p, const ClipWarpPoint &point) { return sourcePositionLess(p, from(point)); });
    const auto &a = *(right - 1), &b = *right;
    if (position == from(a)) return to(a);
    if (a.source == a.output && b.source == b.output) return position;
    const auto r = sourceIntervalRatio(subtract(to(b), to(a)), subtract(from(b), from(a)));
    return addSourcePosition(to(a), scaleSourcePosition(subtract(position, from(a)), r.numerator, r.denominator));
}
}
ClipWarpMap::ClipWarpMap(ClipWarpRegion region, std::span<const WarpAnchor> markers,
                 ResourceLedger ledger, ClipWarpLimits limits) : region_(region) {
    require(markers.size() <= limits.maximumMarkers, "Warp marker count exceeds admission");
    constexpr auto unit = sizeof(ClipWarpPoint) + 36 + sizeof(const ClipWarpPoint *);
    require(markers.size() <= (SIZE_MAX - 1024) / unit - 2, "Warp payload arithmetic overflow");
    const auto charge = 1024 + (markers.size() + 2) * unit;
    if (charge > limits.maximumPayloadBytes)
        throw ResourceLimitError("Experimental warp map", charge, limits.maximumPayloadBytes);
    lease_ = ledger.reserve(charge);
    region_.rawOrigin = canonical(region_.rawOrigin);
    region_.visibleSourceBegin = canonical(region_.visibleSourceBegin);
    region_.visibleSourceEnd = canonical(region_.visibleSourceEnd);
    require(region_.physicalRate >= 8000 && region_.physicalRate <= 384000 &&
            region_.availableSourceFrames > 0 && region_.inputFrames > 0 && region_.outputFrames > 0,
            "Invalid warp region/rate/duration");
    // Count is a prepared source grid. A fractional final sample may precede
    // the asset endpoint although the exclusive grid extent passes it.
    require(sourcePositionLess(addSourcePosition(region_.rawOrigin, {region_.inputFrames-1,0,1}),
                               {region_.availableSourceFrames,0,1}), "Warp processing grid exceeds source asset");
    require(sourcePositionLess(region_.visibleSourceBegin, region_.visibleSourceEnd) &&
            !sourcePositionLess({region_.inputFrames,0,1}, region_.visibleSourceEnd),
            "Warp visible source interval exceeds processing region");
    points_.reserve(markers.size() + 2);
    points_.push_back({{}, {0,0,1}, {0,0,1}});
    for (const auto &marker : markers) {
        const auto source = canonical(marker.source), output = canonical(marker.output);
        require(sourcePositionLess({0,0,1}, source) && sourcePositionLess(source, {region_.inputFrames,0,1}) &&
                sourcePositionLess({0,0,1}, output) && sourcePositionLess(output, {region_.outputFrames,0,1}),
                "Warp markers must be strictly inside both domains");
        require(sourcePositionLess(addSourcePosition(region_.rawOrigin, source), {region_.availableSourceFrames,0,1}),
                "Warp marker has no physical source position");
        points_.push_back({marker.id, source, output});
    }
    points_.push_back({{}, {region_.inputFrames,0,1}, {region_.outputFrames,0,1}});
    std::sort(points_.begin(), points_.end(), [](const auto &a, const auto &b) {
        return sourcePositionLess(a.source, b.source);
    });
    std::vector<const ClipWarpPoint *> identities;
    identities.reserve(markers.size());
    for (const auto &point : points_) if (point.id) identities.push_back(&point);
    std::sort(identities.begin(), identities.end(), [](auto a, auto b) { return a->id->str() < b->id->str(); });
    for (std::size_t i = 1; i < identities.size(); ++i)
        require(*identities[i-1]->id != *identities[i]->id, "Duplicate warp marker identity");
    for (std::size_t i = 1; i < points_.size(); ++i) {
        require(sourcePositionLess(points_[i-1].source, points_[i].source) &&
                sourcePositionLess(points_[i-1].output, points_[i].output),
                "Warp map has duplicate or reversed coordinates");
        if (points_[i].source != points_[i].output || points_[i-1].source != points_[i-1].output)
            (void)sourceIntervalRatio(subtract(points_[i].output, points_[i-1].output),
                        subtract(points_[i].source, points_[i-1].source));
    }
}
SourcePosition ClipWarpMap::sourceToOutput(SourcePosition p) const { return evaluate(points_, p, false); }
SourcePosition ClipWarpMap::outputToSource(SourcePosition p) const { return evaluate(points_, p, true); }
SourcePosition ClipWarpMap::rawAt(SourcePosition p) const {
    p = canonical(p);
    require(!sourcePositionLess({region_.inputFrames,0,1}, p), "Raw warp lookup exceeds processing region");
    return addSourcePosition(region_.rawOrigin, p);
}
SourcePosition ClipWarpMap::visibleOutputBegin() const { return sourceToOutput(region_.visibleSourceBegin); }
SourcePosition ClipWarpMap::visibleOutputEnd() const { return sourceToOutput(region_.visibleSourceEnd); }
std::map<std::size_t,std::size_t> ClipWarpMap::vendorInteriorFrames() const {
    std::map<std::size_t,std::size_t> result;
    for (const auto &point : points_) if (point.id) {
        require(!point.source.fraction && !point.output.fraction,
                "Integer-only vendor map cannot represent a fractional marker");
        require(std::uint64_t(point.source.frame) <= SIZE_MAX && std::uint64_t(point.output.frame) <= SIZE_MAX,
                "Vendor marker exceeds platform frame representation");
        result.emplace(std::size_t(point.source.frame), std::size_t(point.output.frame));
    }
    return result;
}
} // namespace soundcurrent::daw
