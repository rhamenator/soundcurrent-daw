// SPDX-License-Identifier: GPL-3.0-only
#include <soundcurrent/import_inspection_report.hpp>
#include <nlohmann/json.hpp>
#include <algorithm>
#include <array>
#include <charconv>
#include <cmath>
#include <optional>

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
void lexicalNode(std::string_view source,const ReaperStructureNode &node) {
    auto begin=node.line.begin,end=begin+node.line.length;
    if (!begin && source.substr(0,3)=="\xEF\xBB\xBF") begin+=3;
    const auto space=[](char c){return c==' ' || c=='\t' || c=='\r' || c=='\n';};
    while (begin<end && space(source[begin])) ++begin;
    while (end>begin && space(source[end-1])) --end;
    if (begin==end) { check(node.kind==ReaperLineKind::Blank && !node.key.length); return; }
    auto keyBegin=begin,keyEnd=begin;
    if (source[begin]=='<') {
        check(node.kind==ReaperLineKind::BlockOpen); ++keyBegin; keyEnd=keyBegin;
    } else if (source[begin]=='>') {
        check(node.kind==ReaperLineKind::BlockClose && end==begin+1 && node.key==ForeignByteRange{begin,1}); return;
    } else check(node.kind==ReaperLineKind::Data);
    while (keyEnd<end && !space(source[keyEnd])) ++keyEnd;
    check(node.key==ForeignByteRange{keyBegin,keyEnd-keyBegin},"Inspection token range differs from original line");
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
std::size_t inspectionRowsCharge(std::size_t lines,bool properties) {
    PayloadCharge charge("Inspection rows",std::numeric_limits<std::size_t>::max());
    charge.add(sizeof(ImportInspectionReport)+256); charge.add(lines,sizeof(ReaperStructureNode));
    if (properties) {
        PayloadCharge count("Inspection property count",std::numeric_limits<std::size_t>::max()); count.add(lines,15);
        const auto objects=std::min(lines,ReaperImportLimits{}.maximumObjects);
        const auto fields=std::min(count.bytes(),ReaperImportLimits{}.maximumProperties);
        charge.add(objects,2*sizeof(ImportObject)); charge.add(fields,2*sizeof(ImportProperty));
        charge.add(lines,4*sizeof(ImportLineEvidence)+2*sizeof(std::size_t));
        charge.add(objects,6*sizeof(std::uint64_t));
    }
    return charge.bytes();
}
bool inspectionProtocolHasProperties(std::string_view protocol) noexcept {
    return protocol.find("\"sc-import-inspection-v2\"")!=std::string_view::npos;
}
std::size_t inspectionParserCharge(std::size_t bytes) {
    // Conservative payload allowance, not exact allocator/RSS accounting or an
    // OS sandbox. Encoded bytes/depth/tokens/keys remain independently bounded.
    PayloadCharge charge("Inspection decoder",std::numeric_limits<std::size_t>::max());
    charge.add(32768); charge.add(bytes,inspectionDecoderExpansion); return charge.bytes();
}
std::size_t inspectionProtocolCharge(std::size_t bytes) {
    // Admit before string construction. Supported STL implementations can round
    // capacity above length (MSVC notably rounds to a character-allocation block).
    // The owned constructor checks actual capacity and retires unused allowance.
    PayloadCharge charge("Inspection encoded bank",std::numeric_limits<std::size_t>::max());
    charge.add(32); charge.add(bytes,2); return charge.bytes();
}
struct InspectionPropertyDecoder {
    using Field=ImportPropertyId;
    using Status=ImportEvidenceStatus;
    using Reason=ImportEvidenceReason;
    static std::uint64_t bit(unsigned id) { return std::uint64_t{1}<<id; }
    static unsigned priority(ImportLineEvidence e) {
        if (e.status==Status::Unverified) {
            if (e.reason==Reason::InvalidNumber || e.reason==Reason::UnknownShape || e.reason==Reason::InvalidToken) return 4;
            if (e.reason==Reason::DuplicateProperty) return 3;
            if (e.reason==Reason::AmbiguousTake) return 2;
        }
        return e.status==Status::Unsupported ? 1 : 0;
    }
    static std::string_view key(Field id) {
        switch (id) {
        case Field::ProjectSampleRate: case Field::ProjectSampleRateEnabled: return "SAMPLERATE";
        case Field::TrackIdentity: return "TRACKID";
        case Field::TrackName: case Field::TakeName: return "NAME";
        case Field::TrackGain: case Field::TrackPan: case Field::ItemGain: case Field::TakeGain: case Field::TakePan: return "VOLPAN";
        case Field::TrackChannels: return "NCHAN";
        case Field::ItemIdentity: return "IGUID";
        case Field::ItemPosition: return "POSITION";
        case Field::ItemLength: return "LENGTH";
        case Field::ItemFadeIn: return "FADEIN";
        case Field::ItemFadeOut: return "FADEOUT";
        case Field::TakeSourceOffset: return "SOFFS";
        case Field::TakeRate: case Field::TakePitch: return "PLAYRATE";
        case Field::SourceFile: return "FILE";
        }
        return {};
    }
    static bool byteField(Field id) {
        return id==Field::TrackIdentity || id==Field::TrackName || id==Field::ItemIdentity || id==Field::TakeName || id==Field::SourceFile;
    }
    static bool takeField(Field id) {
        return id==Field::TakeName || (id>=Field::ItemGain && id<=Field::TakePitch);
    }
    static std::uint64_t required(const ImportObject &o,std::string_view source) {
        unsigned first=1,last=2;
        if (o.kind==ImportObjectKind::Track) { first=3; last=7; }
        if (o.kind==ImportObjectKind::Item) { first=8; last=19; }
        if (o.kind==ImportObjectKind::Source) {
            if (source.substr(o.sourceType.begin,o.sourceType.length)!="WAVE") return 0;
            first=last=20;
        }
        std::uint64_t bits=0; for (auto i=first;i<=last;++i) bits|=bit(i); return bits;
    }
    static std::uint64_t lineMask(const ImportObject &o,std::string_view source,std::string_view token) {
        const auto allowed=required(o,source); std::uint64_t mask=0;
        for (unsigned i=1;i<=20;++i) if ((allowed&bit(i)) && key(static_cast<Field>(i))==token) mask|=bit(i);
        return mask;
    }
    static bool space(char c) { return c==' ' || c=='\t' || c=='\r' || c=='\n'; }
    static bool quote(char c) { return c=='"' || c=='\'' || c=='`'; }
    static void tokenRange(std::string_view source,const ReaperStructureNode &node,ForeignByteRange value) {
        check(contains(node.line,value) && value.begin>node.key.begin+node.key.length);
        const auto end=value.begin+value.length,lineEnd=node.line.begin+node.line.length;
        const auto before=source[value.begin-1];
        if (quote(before)) {
            check(end<lineEnd && source[end]==before && (end+1==lineEnd || space(source[end+1])));
            check(source.substr(value.begin,value.length).find(before)==std::string_view::npos);
        } else {
            check(space(before) && value.length && (end==lineEnd || space(source[end])));
            for (char c:source.substr(value.begin,value.length)) check(!space(c));
        }
    }
    static ForeignByteRange singleToken(std::string_view source,const ReaperStructureNode &node) {
        auto begin=node.key.begin+node.key.length,end=node.line.begin+node.line.length;
        while (begin<end && space(source[begin])) ++begin;
        while (end>begin && space(source[end-1])) --end;
        if (begin==end) return {};
        if (quote(source[begin])) {
            const auto delimiter=source[begin++];
            if (source[end-1]!=delimiter) return {};
            --end;
            if (source.substr(begin,end-begin).find(delimiter)!=std::string_view::npos) return {};
        } else for (char c:source.substr(begin,end-begin)) if (space(c)) return {};
        return {begin,end-begin};
    }
    // Independent fixed-storage source-token validation, not a call back into
    // the foreign property mapper. A token's position and full line shape must
    // match its field ID; merely containing the reported number is insufficient.
    static void originalField(std::string_view source,const ReaperStructureNode &node,
                              const ImportProperty &p,bool duplicate,bool ambiguous) {
        std::size_t count=1,slot=0;
        switch (p.id) {
        case Field::ProjectSampleRate: count=3; break;
        case Field::ProjectSampleRateEnabled: count=3; slot=1; break;
        case Field::TrackGain: count=5; break;
        case Field::TrackPan: count=5; slot=1; break;
        case Field::ItemFadeIn: case Field::ItemFadeOut: count=13; slot=1; break;
        case Field::ItemGain: count=4; break;
        case Field::TakeGain: count=4; slot=2; break;
        case Field::TakePan: count=4; slot=1; break;
        case Field::TakeRate: count=6; break;
        case Field::TakePitch: count=6; slot=2; break;
        default: break;
        }
        std::array<ForeignByteRange,32> values{};
        std::size_t used=0,begin=node.key.begin+node.key.length,end=node.line.begin+node.line.length;
        while (end>begin && space(source[end-1])) --end;
        bool valid=true;
        while (begin<end) {
            while (begin<end && space(source[begin])) ++begin;
            if (begin==end) break;
            check(used<values.size(),"Unbounded inspection field tokens");
            const auto delimiter=source[begin]; const bool quoted=quote(delimiter);
            const auto start=quoted ? ++begin : begin;
            if (quoted) {
                while (begin<end && source[begin]!=delimiter) ++begin;
                if (begin==end) { valid=false; break; }
                values[used++]={start,begin-start}; ++begin;
                if (begin<end && !space(source[begin])) { valid=false; break; }
            } else {
                while (begin<end && !space(source[begin])) ++begin;
                values[used++]={start,begin-start};
            }
        }
        auto kind=ImportValueKind::None; auto range=node.line;
        auto status=Status::Unverified; auto reason=valid ? Reason::UnknownShape : Reason::InvalidToken;
        if (valid && used==count) {
            range=values[slot]; kind=byteField(p.id) ? ImportValueKind::Bytes : ImportValueKind::Number;
            const bool unsupported=p.id==Field::TakeRate || p.id==Field::TakePitch;
            status=unsupported ? Status::Unsupported : Status::Preserved;
            reason=unsupported ? Reason::ProcessingNotImplemented : Reason::OriginalValue;
            if (kind==ImportValueKind::Number) {
                const auto token=source.substr(range.begin,range.length); double value=0;
                const auto parsed=std::from_chars(token.data(),token.data()+token.size(),value,std::chars_format::general);
                bool number=parsed.ec==std::errc{} && parsed.ptr==token.data()+token.size() && std::isfinite(value);
                switch (p.id) {
                case Field::ProjectSampleRate: case Field::TrackChannels: number=number && value>0 && std::floor(value)==value; break;
                case Field::ProjectSampleRateEnabled: number=number && (value==0 || value==1); break;
                case Field::TrackPan: case Field::TakePan: number=number && value>=-1 && value<=1; break;
                case Field::TrackGain: case Field::ItemGain: case Field::TakeGain:
                case Field::ItemLength: case Field::ItemFadeIn: case Field::ItemFadeOut: number=number && value>=0; break;
                case Field::TakeRate: number=number && value>0; break;
                default: break;
                }
                if (!number) { kind=ImportValueKind::None; status=Status::Unverified; reason=Reason::InvalidNumber; }
                else check(p.number==value,"Inspection number does not match its original field slot");
            }
        }
        if (duplicate) { status=Status::Unverified; reason=Reason::DuplicateProperty; }
        else if (ambiguous) { status=Status::Unverified; reason=Reason::AmbiguousTake; }
        check(p.kind==kind && p.value==range && p.status==status && p.reason==reason,
              "Inspection property differs from original field shape, slot or evidence");
    }
    static void decode(const Json &preview,ImportInspectionReport &out,std::stop_token stop) {
        keys(preview,{"schema","objects","properties","lines"}); check(integer(preview["schema"])==1);
        const auto &objects=preview["objects"], &fields=preview["properties"], &evidence=preview["lines"];
        check(objects.is_array() && !objects.empty() && objects.size()<=out.nodes_.size() &&
              objects.size()<=ReaperImportLimits{}.maximumObjects && fields.is_array() &&
              fields.size()<=ReaperImportLimits{}.maximumProperties && evidence.is_array() && evidence.size()==out.nodes_.size());
        out.objects_.reserve(objects.size()); out.properties_.reserve(fields.size()); out.evidence_.reserve(evidence.size());
        PayloadCharge retained("Inspection retained rows",out.lease_.bytes());
        retained.add(sizeof(ImportInspectionReport)+256); retained.add(out.nodes_.capacity(),sizeof(ReaperStructureNode));
        retained.add(out.objects_.capacity(),sizeof(ImportObject)); retained.add(out.properties_.capacity(),sizeof(ImportProperty));
        retained.add(out.evidence_.capacity(),sizeof(ImportLineEvidence));
        {
            constexpr auto absent=ReaperStructureNode::noParent;
            std::vector<std::size_t> owners(out.nodes_.size(),absent);
            std::vector<ImportLineEvidence> warnings(out.nodes_.size());
            std::vector<std::uint64_t> lineFields(out.nodes_.size()),seen(objects.size()),duplicates(objects.size()),
                present(objects.size()),sources(objects.size()),takes(objects.size());
            PayloadCharge scratch("Inspection row capacity",out.lease_.bytes()); scratch.add(retained.bytes());
            scratch.add(owners.capacity(),sizeof(std::size_t)); scratch.add(lineFields.capacity(),sizeof(std::uint64_t));
            scratch.add(warnings.capacity(),sizeof(ImportLineEvidence));
            for (const auto *bank:{&seen,&duplicates,&present,&sources,&takes}) scratch.add(bank->capacity(),sizeof(std::uint64_t));
            const auto source=out.source();
            const auto text=[&](ForeignByteRange r){return source.substr(r.begin,r.length);};
            std::size_t next=0;
            for (std::size_t n=0;n<out.nodes_.size();++n) {
                canceled(stop); const auto &node=out.nodes_[n];
                const auto parent=node.parent==absent ? absent : owners[node.parent]; owners[n]=parent;
                std::optional<ImportObjectKind> expected;
                if (node.kind==ReaperLineKind::BlockOpen) {
                    if (n==out.root_) expected=ImportObjectKind::Project;
                    else if (parent!=absent && node.parent==out.objects_[parent].node) {
                        const auto kind=out.objects_[parent].kind;
                        const auto tag=text(node.key);
                        if (kind==ImportObjectKind::Project && tag=="TRACK") expected=ImportObjectKind::Track;
                        if (kind==ImportObjectKind::Track && tag=="ITEM") expected=ImportObjectKind::Item;
                        if (kind==ImportObjectKind::Item && tag=="SOURCE") expected=ImportObjectKind::Source;
                    }
                }
                if (expected) {
                    check(next<objects.size()); const auto &row=objects[next];
                    keys(row,{"index","node","parent","kind","sourceType","singleTake"});
                    check(integer(row["index"])==next && integer(row["node"])==n &&
                          integer(row["kind"])==static_cast<unsigned>(*expected) && row["singleTake"].is_boolean());
                    ImportObject object; object.node=n; object.kind=*expected; object.parent=parent;
                    check((row["parent"].is_null() ? absent : integer(row["parent"]))==parent);
                    object.sourceType=range(row["sourceType"],source.size()); object.singleTake=row["singleTake"].get<bool>();
                    if (*expected==ImportObjectKind::Source) {
                        check(object.sourceType==singleToken(source,node)); ++sources[parent];
                    } else check(object.sourceType==ForeignByteRange{});
                    out.objects_.push_back(object); owners[n]=next++;
                }
                const auto owner=owners[n];
                if (owner==absent || node.parent!=out.objects_[owner].node) continue;
                const auto tag=text(node.key);
                if (out.objects_[owner].kind==ImportObjectKind::Item && (tag=="TAKE" || tag=="TAKESEL")) takes[owner]=1;
                if (node.kind==ReaperLineKind::Data) present[owner]|=lineMask(out.objects_[owner],source,tag);
            }
            check(next==objects.size());
            for (std::size_t o=0;o<out.objects_.size();++o) {
                const auto &obj=out.objects_[o];
                check(obj.singleTake==(obj.kind!=ImportObjectKind::Item || (sources[o]==1 && !takes[o])));
            }
            for (const auto &row:fields) {
                canceled(stop); keys(row,{"object","node","id","kind","status","reason","valueRange","number"});
                const auto id=integer(row["id"]),kind=integer(row["kind"]),status=integer(row["status"]),reason=integer(row["reason"]);
                check(id>=1 && id<=20 && kind<=2 && status<=4 && status!=1 && reason<=static_cast<unsigned>(Reason::SourceNotImplemented));
                ImportProperty p; p.id=static_cast<Field>(id); p.kind=static_cast<ImportValueKind>(kind);
                p.status=static_cast<Status>(status); p.reason=static_cast<Reason>(reason); p.object=integer(row["object"]);
                check(p.object<objects.size() && (required(out.objects_[p.object],source)&bit(unsigned(id))));
                p.node=row["node"].is_null() ? absent : integer(row["node"]);
                p.value=range(row["valueRange"],source.size());
                if (p.status==Status::Missing) {
                    check(p.reason==Reason::MissingProperty && p.kind==ImportValueKind::None && p.node==absent &&
                          p.value==ForeignByteRange{} && !(present[p.object]&bit(unsigned(id))));
                } else {
                    check(p.node<out.nodes_.size()); const auto &node=out.nodes_[p.node];
                    check(node.kind==ReaperLineKind::Data && node.parent==out.objects_[p.object].node &&
                          text(node.key)==key(p.id) && !(lineFields[p.node]&bit(unsigned(id))));
                    lineFields[p.node]|=bit(unsigned(id));
                    if (p.kind!=ImportValueKind::None || p.value!=node.line) tokenRange(source,node,p.value);
                    if (p.status==Status::Preserved) check(p.reason==Reason::OriginalValue && p.kind!=ImportValueKind::None &&
                                                          p.id!=Field::TakeRate && p.id!=Field::TakePitch);
                    if (p.status==Status::Unsupported) check(p.reason==Reason::ProcessingNotImplemented &&
                        (p.id==Field::TakeRate || p.id==Field::TakePitch) && p.kind==ImportValueKind::Number);
                    if (p.status==Status::Unverified) check(p.reason!=Reason::OriginalValue && p.reason!=Reason::MissingProperty);
                }
                if (p.kind==ImportValueKind::Number) {
                    check(!byteField(p.id) && row["number"].is_number()); p.number=row["number"].get<double>(); check(std::isfinite(p.number));
                    const auto value=text(p.value); double original=0;
                    const auto parsed=std::from_chars(value.data(),value.data()+value.size(),original,std::chars_format::general);
                    check(parsed.ec==std::errc{} && parsed.ptr==value.data()+value.size() && std::isfinite(original) && original==p.number,
                          "Inspection numeric value differs from original token");
                } else { check(row["number"].is_null()); if (p.kind==ImportValueKind::Bytes) check(byteField(p.id)); }
                if (seen[p.object]&bit(unsigned(id))) duplicates[p.object]|=bit(unsigned(id));
                seen[p.object]|=bit(unsigned(id)); out.properties_.push_back(p);
                if (p.node!=absent && priority({p.status,p.reason})>priority(warnings[p.node])) warnings[p.node]={p.status,p.reason};
            }
            for (std::size_t o=0;o<objects.size();++o) check(seen[o]==required(out.objects_[o],source));
            for (const auto &p:out.properties_) {
                canceled(stop);
                if (p.status!=Status::Missing)
                    originalField(source,out.nodes_[p.node],p,
                        (duplicates[p.object]&bit(static_cast<unsigned>(p.id)))!=0,
                        takeField(p.id) && !out.objects_[p.object].singleTake);
            }
            for (std::size_t n=0;n<out.nodes_.size();++n) {
                canceled(stop); const auto &node=out.nodes_[n]; const auto owner=owners[n];
                const auto expected=owner!=absent && node.kind==ReaperLineKind::Data && node.parent==out.objects_[owner].node ?
                    lineMask(out.objects_[owner],source,text(node.key)) : 0;
                check(lineFields[n]==expected,"Inspection omitted or invented original field occurrence");
                const auto &row=evidence[n]; check(row.is_array() && row.size()==2);
                const auto status=integer(row[0]),reason=integer(row[1]); check(status<=4 && status!=1 && status!=3 && reason<=static_cast<unsigned>(Reason::SourceNotImplemented));
                ImportLineEvidence expectedEvidence;
                const auto tag=text(node.key);
                if (node.kind==ReaperLineKind::Blank || node.kind==ReaperLineKind::BlockClose)
                    expectedEvidence={Status::Preserved,Reason::OriginalValue};
                else if (owner!=absent) {
                    const auto &object=out.objects_[owner];
                    if (object.kind==ImportObjectKind::Source && text(object.sourceType)!="WAVE" &&
                        (n==object.node || (node.kind==ReaperLineKind::Data && node.parent==object.node)))
                        expectedEvidence={Status::Unsupported,Reason::SourceNotImplemented};
                    else if (node.parent==object.node &&
                        ((object.kind==ImportObjectKind::Project &&
                          ((node.kind==ReaperLineKind::Data && (tag=="TEMPO" || tag=="PLAYRATE")) ||
                           (node.kind==ReaperLineKind::BlockOpen && tag=="TEMPOENVEX"))) ||
                         (object.kind==ImportObjectKind::Track &&
                          ((node.kind==ReaperLineKind::BlockOpen && tag=="FXCHAIN") ||
                           (node.kind==ReaperLineKind::Data && tag=="AUXRECV")))))
                        expectedEvidence={Status::Unsupported,Reason::ProcessingNotImplemented};
                    else if (expected && tag!="SAMPLERATE" && tag!="VOLPAN" && tag!="FADEIN" && tag!="FADEOUT" && tag!="PLAYRATE")
                        expectedEvidence={Status::Preserved,Reason::OriginalValue};
                }
                if (priority(warnings[n])>priority(expectedEvidence)) expectedEvidence=warnings[n];
                check(status==static_cast<unsigned>(expectedEvidence.status) && reason==static_cast<unsigned>(expectedEvidence.reason),
                      "Inspection line evidence masks field or opaque-state evidence");
                out.evidence_.push_back({static_cast<Status>(status),static_cast<Reason>(reason)});
            }
        }
        out.lease_.resize(retained.bytes()); out.propertiesVersion_=1;
    }
};
ImportInspectionReport decodeInspectionReport(OwnedInspectionProtocol protocol, ForeignSnapshot &&source,
    std::array<char,64> hash, std::size_t childPid, ResourceLedger resources,
    ResourceLease rowsGrant, ResourceLease parserGrant, std::stop_token stop) {
    canceled(stop);
    const auto encoded=protocol.bytes();
    check(protocol.ownedBy(resources) && source.ownedBy(resources),"Unadmitted encoded/source inspection bank");
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
        const bool properties=root.is_object() && root.contains("protocol") && root["protocol"]=="sc-import-inspection-v2";
        if (properties) keys(root,{"protocol","adapter","sourceFormat","workerPid","source",
                   "nativeCompatibility","semanticStatus","writerVersion","root","nodes","preview","complete"});
        else keys(root,{"protocol","adapter","sourceFormat","workerPid","source",
                   "nativeCompatibility","semanticStatus","writerVersion","root","nodes","complete"});
        check((properties ? root["adapter"]=="rpp-source-properties-v1" :
              root["protocol"]=="sc-import-inspection-v1" && root["adapter"]=="rpp-outline-v1") &&
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
        ImportInspectionReport result(std::move(rowsGrant),std::move(source),std::move(protocol));
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
            lexicalNode(result.source(),node);
        }
        check(position==result.source().size() && rootSeen && rootClosed && !depth &&
              range(writer["headerRange"],result.source().size())==result.nodes_[result.root_].line);
        if (properties) {
            check(result.lease_.bytes()>=inspectionRowsCharge(lineCount,true),"Unadmitted inspection properties");
            InspectionPropertyDecoder::decode(root["preview"],result,stop);
        }
        canceled(stop);
        return result;
    } catch (const Json::exception &) {
        throw ProjectError(ErrorCode::InvalidState,"Malformed inspection protocol");
    }
} // DOM and callback key banks retire before parserGrant returns its credit.
} // namespace soundcurrent::daw
