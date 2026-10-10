// SPDX-License-Identifier: GPL-3.0-only
// Bounded JSON-lines oracle adapter. No media, rendering, UI or audio endpoints.
#include "warp_map.hpp"
#include <nlohmann/json.hpp>
#include <cstdio>
#include <iostream>
#include <limits>

using namespace soundcurrent::daw;
using namespace soundcurrent::daw::experimental;
using Json = nlohmann::json;
namespace {
std::uint64_t integer(const Json &v, std::uint64_t maximum = UINT64_MAX) {
    if (!v.is_number_integer() || (v.is_number_integer() && !v.is_number_unsigned() && v.get<std::int64_t>() < 0))
        throw std::runtime_error("Expected nonnegative exact integer");
    const auto n = v.get<std::uint64_t>();
    if (n > maximum) throw std::runtime_error("Integer exceeds typed field");
    return n;
}
SourcePosition position(const Json &v) {
    if (!v.is_array() || v.size() != 3) throw std::runtime_error("Expected exact coordinate triple");
    return {Frame(integer(v[0], INT64_MAX)), integer(v[1]), integer(v[2])};
}
Json encoded(SourcePosition p) { return Json::array({p.frame,p.fraction,p.denominator}); }
Json request(const Json &j) {
    ResourceLedger ledger(j.contains("ledgerBytes") ? std::size_t(integer(j.at("ledgerBytes"), SIZE_MAX)) : 1024*1024,
                          "Experimental warp oracle");
    try {
        WarpRegion region{position(j.at("rawOrigin")), std::uint32_t(integer(j.at("physicalRate"), UINT32_MAX)),
            Frame(integer(j.at("availableSourceFrames"), INT64_MAX)), Frame(integer(j.at("inputFrames"), INT64_MAX)),
            Frame(integer(j.at("outputFrames"), INT64_MAX)), position(j.at("visibleBegin")), position(j.at("visibleEnd"))};
        std::vector<WarpMarker> markers;
        if (!j.at("markers").is_array() || j.at("markers").size() > 128) throw std::runtime_error("Probe marker envelope");
        for (const auto &v : j.at("markers")) markers.push_back({Id(v.at("id").get<std::string>()), position(v.at("source")), position(v.at("output"))});
        WarpLimits limits;
        if (j.contains("maximumMarkers")) limits.maximumMarkers = std::size_t(integer(j.at("maximumMarkers"), 4096));
        if (j.contains("maximumPayloadBytes")) limits.maximumPayloadBytes = std::size_t(integer(j.at("maximumPayloadBytes"), 1024*1024));
        Json result;
        {
            WarpMap map(region, markers, ledger, limits);
            result = {{"accepted",true}, {"geometry",warpGeometryId}, {"points",Json::array()}, {"forward",Json::array()}, {"inverse",Json::array()}, {"raw",Json::array()},
                      {"visibleBegin",encoded(map.visibleOutputBegin())}, {"visibleEnd",encoded(map.visibleOutputEnd())}, {"chargedBytes",map.chargedBytes()}};
            for (const auto &p : map.points()) result["points"].push_back({{"id",p.id ? Json(p.id->str()) : Json(nullptr)}, {"source",encoded(p.source)}, {"output",encoded(p.output)}});
            for (const auto &v : j.at("forward")) {const auto p=position(v);result["forward"].push_back(encoded(map.sourceToOutput(p)));result["raw"].push_back(encoded(map.rawAt(p)));}
            for (const auto &v : j.at("inverse")) result["inverse"].push_back(encoded(map.outputToSource(position(v))));
            try {
                result["vendor"] = Json::array();
                for (const auto &[a,b] : map.vendorInteriorFrames()) result["vendor"].push_back(Json::array({a,b}));
                result["vendorAccepted"] = true;
            } catch (const std::exception &e) {result["vendorAccepted"]=false;result["vendorRefusal"]=e.what();result["vendor"]=nullptr;}
        }
        result["afterReleaseBytes"] = ledger.usage().reservedBytes;
        return result;
    } catch (const std::exception &e) {
        return {{"accepted",false}, {"error",e.what()}, {"afterRefusalBytes",ledger.usage().reservedBytes}};
    }
}
}
int main() {
    try {
        for (unsigned line = 0; line < 4096; ++line) {
            std::string text;
            // Bound before allocation, rather than checking an unbounded getline.
            while (true) {
                const auto c = std::cin.get();
                if (c == EOF || c == '\n') break;
                if (text.size() == 16384) throw std::runtime_error("Probe input exceeds bank");
                text.push_back(char(c));
            }
            if (text.empty() && std::cin.eof()) return 0;
            Json output;
            try {output=request(Json::parse(text));}
            catch(const std::exception &e) {output={{"accepted",false},{"error",e.what()},{"afterRefusalBytes",0}};}
            std::cout << output.dump() << '\n';
            if (std::cin.eof()) return 0;
        }
        if (std::cin.peek() != EOF) throw std::runtime_error("Probe request count exceeds bank");
        return 0;
    } catch (const std::exception &e) {std::cerr<<e.what()<<'\n';return 1;}
}
