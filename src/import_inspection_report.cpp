// SPDX-License-Identifier: GPL-3.0-only
#include <soundcurrent/import_inspection_report.hpp>
#include <nlohmann/json.hpp>
#include <algorithm>
#include <array>

namespace soundcurrent::daw {
namespace {
using Json = nlohmann::json;
void check(bool value, const char *message = "Invalid inspection protocol") {
    if (!value) throw ProjectError(ErrorCode::InvalidState,message);
}
void canceled(std::stop_token stop) {
    if (stop.stop_requested()) throw ProjectError(ErrorCode::Canceled,"Inspection report canceled");
}
void keys(const Json &value, std::initializer_list<std::string_view> expected) {
    check(value.is_object() && value.size()==expected.size());
    for (auto key : expected) check(value.contains(std::string(key)));
}
std::size_t integer(const Json &value) {
    check(value.is_number_unsigned());
    const auto number = value.get<std::uint64_t>();
    check(number<=std::numeric_limits<std::size_t>::max());
    return static_cast<std::size_t>(number);
}
ForeignByteRange range(const Json &value, std::size_t size) {
    check(value.is_array() && value.size()==2);
    ForeignByteRange result{integer(value[0]),integer(value[1])};
    check(result.begin<=size && result.length<=size-result.begin);
    return result;
}
bool contains(ForeignByteRange outer, ForeignByteRange inner) {
    return inner.begin>=outer.begin && inner.begin-outer.begin<=outer.length &&
           inner.length<=outer.length-(inner.begin-outer.begin);
}
void preflight(std::string_view bytes, std::stop_token stop) {
    unsigned depth=0, length=0; bool quoted=false;
    for (std::size_t i=0;i<bytes.size();++i) {
        if (!(i%4096)) canceled(stop);
        const auto c=static_cast<unsigned char>(bytes[i]);
        check(c<128 && (c>=32 || c=='\n' || c=='\r' || c=='\t'));
        if (c=='"') { quoted=!quoted; length=0; }
        else if (quoted) {
            // Producer v1 uses literal ASCII keys/constants/hash only. It never
            // echoes foreign text or emits escapes. Bound tokens before DOM.
            check(c!='\\' && c>=32 && ++length<=64);
        } else if (c=='{' || c=='[') check(++depth<=8);
        else if (c=='}' || c==']') { check(depth>0); --depth; }
    }
    check(!quoted && depth==0);
}
}
std::size_t foreignSnapshotLines(std::string_view source, std::stop_token stop) {
    std::size_t result=0;
    for (std::size_t i=0;i<source.size();++i) {
        if (!(i%65536)) canceled(stop);
        if (source[i]=='\n') ++result;
    }
    return result+(!source.empty() && source.back()!='\n');
}
std::size_t inspectionRowsCharge(std::size_t lines) {
    PayloadCharge charge("Inspection rows",std::numeric_limits<std::size_t>::max());
    charge.add(sizeof(ImportInspectionReport)+256); charge.add(lines,sizeof(ReaperStructureNode));
    return charge.bytes();
}
std::size_t inspectionParserCharge(std::size_t bytes) {
    // Conservative payload allowance, not exact allocator/RSS accounting or an
    // OS sandbox. Encoded bytes/depth/tokens/keys remain independently bounded.
    PayloadCharge charge("Inspection decoder",std::numeric_limits<std::size_t>::max());
    charge.add(32768); charge.add(bytes,inspectionDecoderExpansion); return charge.bytes();
}
ImportInspectionReport decodeInspectionReport(std::string_view encoded, ForeignSnapshot &&source,
    std::array<char,64> hash, std::size_t childPid, ResourceLedger resources,
    ResourceLease rowsGrant, ResourceLease parserGrant, std::stop_token stop) {
    canceled(stop);
    const auto lineCount=foreignSnapshotLines(source.bytes(),stop);
    check(childPid && lineCount && lineCount<=ReaperStructureLimits{}.maximumLines);
    check(resources.owns(rowsGrant) && resources.owns(parserGrant) &&
          rowsGrant.bytes()>=inspectionRowsCharge(lineCount) &&
          parserGrant.bytes()>=inspectionParserCharge(encoded.size()),"Unadmitted inspection decode");
    preflight(encoded,stop);
    // Duplicate keys must not be normalized into a plausible last value.
    struct ObjectKeys { std::array<std::string,16> names; std::size_t count=0; };
    std::array<ObjectKeys,8> objects{}; std::size_t objectDepth=0;
    const auto callback=[&](int,Json::parse_event_t event,Json &value) {
        canceled(stop);
        if (event==Json::parse_event_t::object_start) {
            check(objectDepth<objects.size()); objects[objectDepth++].count=0;
        } else if (event==Json::parse_event_t::object_end) {
            check(objectDepth>0); --objectDepth;
        } else if (event==Json::parse_event_t::key) {
            check(objectDepth>0 && value.is_string());
            auto &frame=objects[objectDepth-1]; const auto &key=value.get_ref<const std::string &>();
            check(frame.count<frame.names.size());
            for (std::size_t i=0;i<frame.count;++i) check(frame.names[i]!=key,"Duplicate inspection key");
            frame.names[frame.count++]=key;
        }
        return true;
    };
    try {
        const auto root=Json::parse(encoded,callback);
        keys(root,{"protocol","adapter","sourceFormat","workerPid","source",
                   "nativeCompatibility","semanticStatus","writerVersion","root","nodes","complete"});
        check(root["protocol"]=="sc-import-inspection-v1" && root["adapter"]=="rpp-outline-v1" &&
              root["sourceFormat"]=="reaper-rpp" && root["nativeCompatibility"]=="unqualified" &&
              root["semanticStatus"]=="unverified" && root["complete"]==true &&
              integer(root["workerPid"])==childPid);
        const auto &provenance=root["source"];
        keys(provenance,{"bytes","sha256","storage","consistency"});
        check(integer(provenance["bytes"])==source.bytes().size() &&
              provenance["sha256"].is_string() &&
              std::string_view(provenance["sha256"].get_ref<const std::string &>())==
                  std::string_view(hash.data(),hash.size()) &&
              provenance["storage"]=="worker-memory-snapshot" &&
              provenance["consistency"]=="size-and-mtime-checked-not-atomic");
        const auto &writer=root["writerVersion"]; keys(writer,{"status","headerRange"});
        check(writer["status"]=="unverified");
        const auto &rows=root["nodes"]; check(rows.is_array() && rows.size()==lineCount);
        ImportInspectionReport result(std::move(rowsGrant),std::move(source));
        result.sha_=hash; result.pid_=childPid; result.root_=integer(root["root"]);
        check(result.root_<lineCount);
        result.nodes_.reserve(lineCount);
        std::array<std::size_t,128> parents{}; std::size_t depth=0, position=0;
        bool rootSeen=false, rootClosed=false;
        for (std::size_t i=0;i<lineCount;++i) {
            canceled(stop); const auto &row=rows[i];
            keys(row,{"index","kind","parent","lineRange","keyRange","extentRange","status","originalBytesRetained"});
            check(integer(row["index"])==i && row["status"]=="unverified" && row["originalBytesRetained"]==true);
            ReaperStructureNode node;
            node.line=range(row["lineRange"],result.source().size());
            node.key=range(row["keyRange"],result.source().size());
            node.extent=range(row["extentRange"],result.source().size());
            node.parent=row["parent"].is_null() ? ReaperStructureNode::noParent : integer(row["parent"]);
            check(node.parent==(depth ? parents[depth-1] : ReaperStructureNode::noParent));
            check(node.line.begin==position && node.line.length && node.line.length<=65537);
            const auto remaining=result.source().substr(position);
            const auto newline=remaining.substr(0,std::min<std::size_t>(remaining.size(),65537)).find('\n');
            const auto expectedLength=newline==std::string_view::npos ? remaining.size() : newline+1;
            check(node.line.length==expectedLength,"Inspection line inventory differs from source");
            position+=node.line.length;
            if (depth) check(contains(result.nodes_[parents[depth-1]].extent,node.line));
            if (row["kind"]=="block-open") {
                node.kind=ReaperLineKind::BlockOpen;
                check(node.key.length && contains(node.line,node.key) && contains(node.extent,node.line) &&
                      node.extent.begin==node.line.begin && depth<parents.size());
                if (!depth) {
                    check(!rootSeen && !rootClosed && i==result.root_ &&
                          result.source().substr(node.key.begin,node.key.length)=="REAPER_PROJECT");
                    rootSeen=true;
                }
                parents[depth++]=i;
            } else if (row["kind"]=="block-close") {
                node.kind=ReaperLineKind::BlockClose;
                check(depth && node.extent==node.line && node.key.length==1 && contains(node.line,node.key));
                const auto opened=result.nodes_[parents[--depth]].extent;
                check(opened.begin+opened.length==position);
                if (!depth) rootClosed=true;
            } else if (row["kind"]=="blank") {
                node.kind=ReaperLineKind::Blank; check(node.extent==node.line && !node.key.length);
            } else if (row["kind"]=="data") {
                node.kind=ReaperLineKind::Data;
                check(depth && node.extent==node.line && node.key.length && contains(node.line,node.key));
            } else check(false);
            result.nodes_.push_back(node);
        }
        check(position==result.source().size() && rootSeen && rootClosed && !depth &&
              range(writer["headerRange"],result.source().size())==result.nodes_[result.root_].line);
        canceled(stop);
        return result;
    } catch (const Json::exception &) {
        throw ProjectError(ErrorCode::InvalidState,"Malformed inspection protocol");
    }
} // DOM and callback key banks retire before parserGrant returns its credit.
} // namespace soundcurrent::daw
