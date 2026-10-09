// SPDX-License-Identifier: GPL-3.0-only
#include <soundcurrent/inspection_bundle.hpp>
#include <nlohmann/json.hpp>
#include <iostream>
using namespace soundcurrent::daw;
std::string hex(std::string_view bytes) {
    static constexpr char digits[]="0123456789abcdef";
    std::string result; result.reserve(bytes.size()*2);
    for (const unsigned char c:bytes) { result+=digits[c>>4]; result+=digits[c&15]; }
    return result;
}
int probe(const std::filesystem::path &path) {
    try {
        ResourceLedger memory(64*1024*1024,"Property protocol fixture");
        nlohmann::json result;
        {
            auto report=loadInspectionBundle(path,memory);
            result={{"propertiesVersion",report.hasProperties() ? 1 : 0},{"pid",report.workerPid()},
                    {"sha256",report.sha256()},{"chargedBytes",report.chargedBytes()},
                    {"objects",nlohmann::json::array()},{"properties",nlohmann::json::array()},
                    {"lines",nlohmann::json::array()}};
            for (const auto &o:report.objects()) result["objects"].push_back({{"node",o.node},{"parent",o.parent},
                {"kind",static_cast<unsigned>(o.kind)},
                {"sourceTypeHex",hex(report.source().substr(o.sourceType.begin,o.sourceType.length))}});
            for (const auto &p:report.properties()) result["properties"].push_back({{"object",p.object},
                {"id",static_cast<unsigned>(p.id)},{"kind",static_cast<unsigned>(p.kind)},
                {"status",static_cast<unsigned>(p.status)},{"reason",static_cast<unsigned>(p.reason)},
                {"number",p.number},{"bytesHex",hex(report.source().substr(p.value.begin,p.value.length))}});
            for (const auto &e:report.lineEvidence()) result["lines"].push_back({static_cast<unsigned>(e.status),static_cast<unsigned>(e.reason)});
            if (memory.usage().reservedBytes!=report.chargedBytes()) throw std::runtime_error("Transient property credit retained");
        }
        if (memory.usage().reservedBytes || memory.usage().owners) throw std::runtime_error("Property report ownership leaked");
        result["retiredBytes"]=0; std::cout<<result.dump()<<'\n'; return 0;
    } catch (const ProjectError &e) { std::cerr<<"refused="<<static_cast<unsigned>(e.code())<<'\n'; return 1; }
    catch (const std::exception &) { std::cerr<<"probe_failed\n"; return 1; }
}
#ifdef _WIN32
int wmain(int argc,wchar_t **argv) { return argc==2 ? probe(std::filesystem::path(argv[1])) : 2; }
#else
int main(int argc,char **argv) { return argc==2 ? probe(std::filesystem::path(argv[1])) : 2; }
#endif
