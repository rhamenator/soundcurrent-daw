// SPDX-License-Identifier: GPL-3.0-only
#include "warp_map.hpp"
#include <algorithm>
#include <limits>
#include <numeric>

namespace soundcurrent::daw::experimental {
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
SourcePosition add(SourcePosition a, SourcePosition b) {
    a = canonical(a); b = canonical(b);
    const auto denominator = commonDenominator(a, b);
    const auto left = a.fraction * (denominator / a.denominator);
    const auto right = b.fraction * (denominator / b.denominator);
    const bool carry = left >= denominator - right;
    const auto remainder = carry ? left - (denominator - right) : left + right;
    require(b.frame <= INT64_MAX - a.frame && (!carry || a.frame + b.frame < INT64_MAX),
            "Warp coordinate sum exceeds exact representation");
    return canonical({a.frame + b.frame + Frame(carry), remainder, denominator});
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
std::uint64_t improper(SourcePosition p) {
    require(std::uint64_t(p.frame) <= (UINT64_MAX - p.fraction) / p.denominator,
            "Warp segment ratio exceeds exact representation");
    return std::uint64_t(p.frame) * p.denominator + p.fraction;
}
struct Ratio { std::uint64_t n, d; };
Ratio ratio(SourcePosition output, SourcePosition input) {
    if (output == input) return {1,1};
    auto on = improper(output), in = improper(input);
    require(on && in, "Warp segment has zero duration");
    auto od = output.denominator, id = input.denominator;
    const auto a = std::gcd(on, in); on /= a; in /= a;
    const auto b = std::gcd(id, od); id /= b; od /= b;
    require(on <= UINT64_MAX / id && in <= UINT64_MAX / od,
            "Warp segment scale exceeds exact representation");
    return {on * id, in * od};
}
SourcePosition evaluate(std::span<const WarpPoint> points, SourcePosition position, bool inverse) {
    position = canonical(position);
    auto from = [inverse](const WarpPoint &p) { return inverse ? p.output : p.source; };
    auto to = [inverse](const WarpPoint &p) { return inverse ? p.source : p.output; };
    require(!sourcePositionLess(position, from(points.front())) &&
            !sourcePositionLess(from(points.back()), position), "Warp lookup is outside its domain");
    if (position == from(points.back())) return to(points.back());
    const auto right = std::upper_bound(points.begin(), points.end(), position,
        [&](SourcePosition p, const WarpPoint &point) { return sourcePositionLess(p, from(point)); });
    const auto &a = *(right - 1), &b = *right;
    if (position == from(a)) return to(a);
    if (a.source == a.output && b.source == b.output) return position;
    const auto r = ratio(subtract(to(b), to(a)), subtract(from(b), from(a)));
    return add(to(a), scaleSourcePosition(subtract(position, from(a)), r.n, r.d));
}
}
WarpMap::WarpMap(WarpRegion region, std::span<const WarpMarker> markers,
                 ResourceLedger ledger, WarpLimits limits) : region_(region) {
    require(markers.size() <= limits.maximumMarkers, "Warp marker count exceeds admission");
    constexpr auto unit = sizeof(WarpPoint) + 36 + sizeof(const WarpPoint *);
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
    require(sourcePositionLess(add(region_.rawOrigin, {region_.inputFrames-1,0,1}),
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
        require(sourcePositionLess(add(region_.rawOrigin, source), {region_.availableSourceFrames,0,1}),
                "Warp marker has no physical source position");
        points_.push_back({marker.id, source, output});
    }
    points_.push_back({{}, {region_.inputFrames,0,1}, {region_.outputFrames,0,1}});
    std::sort(points_.begin(), points_.end(), [](const auto &a, const auto &b) {
        return sourcePositionLess(a.source, b.source);
    });
    std::vector<const WarpPoint *> identities;
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
            (void)ratio(subtract(points_[i].output, points_[i-1].output),
                        subtract(points_[i].source, points_[i-1].source));
    }
}
SourcePosition WarpMap::sourceToOutput(SourcePosition p) const { return evaluate(points_, p, false); }
SourcePosition WarpMap::outputToSource(SourcePosition p) const { return evaluate(points_, p, true); }
SourcePosition WarpMap::rawAt(SourcePosition p) const {
    p = canonical(p);
    require(!sourcePositionLess({region_.inputFrames,0,1}, p), "Raw warp lookup exceeds processing region");
    return add(region_.rawOrigin, p);
}
SourcePosition WarpMap::visibleOutputBegin() const { return sourceToOutput(region_.visibleSourceBegin); }
SourcePosition WarpMap::visibleOutputEnd() const { return sourceToOutput(region_.visibleSourceEnd); }
std::map<std::size_t,std::size_t> WarpMap::vendorInteriorFrames() const {
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
} // namespace soundcurrent::daw::experimental
