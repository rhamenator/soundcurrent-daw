// SPDX-License-Identifier: GPL-3.0-only
#include <soundcurrent/reaper_import.hpp>
#include <algorithm>
#include <array>
#include <charconv>
#include <cmath>
#include <optional>

namespace soundcurrent::daw {
namespace {
constexpr auto absent = ReaperStructureNode::noParent;
using FieldId = ImportPropertyId;
using Status = ImportEvidenceStatus;
using Reason = ImportEvidenceReason;
void canceled(std::stop_token stop) {
    if (stop.stop_requested()) throw ProjectError(ErrorCode::Canceled,"Foreign project mapping canceled");
}
void invalid(bool ok, const char *message) {
    if (!ok) throw ProjectError(ErrorCode::InvalidState,message);
}
bool space(char c) { return c==' ' || c=='\t' || c=='\r' || c=='\n'; }
struct Tokens {
    std::array<ForeignByteRange,32> values{};
    std::size_t count = 0;
    bool valid = true;
};
// Original bounded lexer. Three delimiters retain literal byte contents; no
// JSON/C backslash escaping, comment interpretation or permissive atof parsing.
// This deliberately does not accept adjacent quoted tokens without whitespace.
Tokens tokens(const ReaperStructure &s, const ReaperStructureNode &node, std::size_t maximum) {
    Tokens out;
    const auto text = s.source();
    auto p = node.key.begin+node.key.length, end = node.line.begin+node.line.length;
    while (end>p && space(text[end-1])) --end;
    while (p<end) {
        while (p<end && space(text[p])) ++p;
        if (p==end) break;
        if (out.count == maximum)
            throw ResourceLimitError("Foreign mapped line tokens",out.count+1,maximum);
        const auto delimiter = text[p];
        const bool quoted = delimiter=='"' || delimiter=='\'' || delimiter=='`';
        const auto begin = quoted ? ++p : p;
        if (quoted) {
            while (p<end && text[p]!=delimiter) ++p;
            if (p==end) { out.valid=false; break; }
            out.values[out.count++] = {begin,p-begin}; ++p;
            if (p<end && !space(text[p])) { out.valid=false; break; }
        } else {
            while (p<end && !space(text[p])) ++p;
            out.values[out.count++] = {begin,p-begin};
        }
    }
    return out;
}
std::uint64_t bit(FieldId id) { return std::uint64_t{1} << static_cast<unsigned>(id); }
bool takeField(FieldId id) {
    return id==FieldId::TakeName || id==FieldId::ItemGain || id==FieldId::TakeGain || id==FieldId::TakePan ||
           id==FieldId::TakeSourceOffset || id==FieldId::TakeRate || id==FieldId::TakePitch;
}
unsigned evidencePriority(const ImportLineEvidence &e) {
    if (e.status==Status::Unverified) {
        if (e.reason==Reason::InvalidNumber || e.reason==Reason::UnknownShape || e.reason==Reason::InvalidToken) return 4;
        if (e.reason==Reason::DuplicateProperty) return 3;
        if (e.reason==Reason::AmbiguousTake) return 2;
    }
    return e.status==Status::Unsupported ? 1 : 0;
}
bool numberValid(FieldId id, double value) {
    if (!std::isfinite(value)) return false;
    switch (id) {
    case FieldId::ProjectSampleRate: return value>0 && std::floor(value)==value;
    case FieldId::ProjectSampleRateEnabled: return value==0 || value==1;
    case FieldId::TrackChannels: return value>0 && std::floor(value)==value;
    case FieldId::TrackPan: case FieldId::TakePan: return value>=-1 && value<=1;
    case FieldId::TrackGain: case FieldId::ItemGain: case FieldId::TakeGain:
    case FieldId::ItemLength: case FieldId::ItemFadeIn: case FieldId::ItemFadeOut: return value>=0;
    case FieldId::TakeRate: return value>0;
    default: return true; // Position/offset can be negative; do not silently clamp.
    }
}
bool direct(const ReaperStructure &s, std::size_t i, std::string_view parentKey) {
    const auto parent = s.nodes()[i].parent;
    return parent!=absent && s.bytes(s.nodes()[parent].key)==parentKey;
}
std::optional<ImportObjectKind> objectKind(const ReaperStructure &s, std::size_t i) {
    const auto &node = s.nodes()[i];
    if (node.kind!=ReaperLineKind::BlockOpen) return {};
    const auto key=s.bytes(node.key);
    if (i==s.root()) return ImportObjectKind::Project;
    if (key=="TRACK" && node.parent==s.root()) return ImportObjectKind::Track;
    if (key=="ITEM" && direct(s,i,"TRACK") && s.nodes()[node.parent].parent==s.root()) return ImportObjectKind::Item;
    if (key=="SOURCE" && direct(s,i,"ITEM") && direct(s,node.parent,"TRACK") &&
        s.nodes()[s.nodes()[node.parent].parent].parent==s.root()) return ImportObjectKind::Source;
    return {};
}
constexpr std::array projectFields{FieldId::ProjectSampleRate,FieldId::ProjectSampleRateEnabled};
constexpr std::array trackFields{FieldId::TrackIdentity,FieldId::TrackName,FieldId::TrackGain,FieldId::TrackPan,FieldId::TrackChannels};
constexpr std::array itemFields{FieldId::ItemIdentity,FieldId::ItemPosition,FieldId::ItemLength,FieldId::ItemFadeIn,FieldId::ItemFadeOut,
    FieldId::TakeName,FieldId::ItemGain,FieldId::TakeGain,FieldId::TakePan,FieldId::TakeSourceOffset,FieldId::TakeRate,FieldId::TakePitch};
constexpr std::array sourceFields{FieldId::SourceFile};
std::span<const FieldId> required(const ImportObject &object, const ReaperStructure &s) {
    switch (object.kind) {
    case ImportObjectKind::Project: return projectFields;
    case ImportObjectKind::Track: return trackFields;
    case ImportObjectKind::Item: return itemFields;
    case ImportObjectKind::Source: return s.bytes(object.sourceType)=="WAVE" ? std::span<const FieldId>(sourceFields) : std::span<const FieldId>{};
    }
    return {};
}
} // namespace
const ImportProperty *ReaperImportPreview::property(std::size_t object, FieldId id) const noexcept {
    const ImportProperty *found=nullptr;
    for (const auto &p:properties_) if (p.object==object && p.id==id) {
        if (found) return nullptr;
        found=&p;
    }
    return found;
}
ReaperImportPreview inspectReaperImport(std::string_view source, ReaperImportLimits limits,
                                      ResourceLedger resources, std::stop_token stop) {
    canceled(stop);
    invalid(limits.maximumObjects && limits.maximumProperties && limits.maximumTokensPerMappedLine &&
            limits.maximumTokensPerMappedLine<=32,"Invalid trusted foreign mapping limits");
    auto structure=inspectReaperStructure(source,limits.structure,resources,stop);
    const auto nodes=structure.nodes();
    std::size_t objectCount=0;
    for (std::size_t i=0;i<nodes.size();++i) {
        canceled(stop);
        if (objectKind(structure,i) && ++objectCount>limits.maximumObjects)
            throw ResourceLimitError("Foreign import objects",objectCount,limits.maximumObjects);
    }
    // At most three mapped values per data line plus twelve required fields per
    // object. Charge conservative allocation capacity before any vector bank.
    PayloadCharge upper("Foreign import properties",resources.usage().limitBytes);
    upper.add(nodes.size(),3); upper.add(objectCount,12);
    const auto propertyCapacity=std::min(upper.bytes(),limits.maximumProperties);
    PayloadCharge charge("Foreign import representation",resources.usage().limitBytes);
    charge.add(sizeof(ReaperImportPreview));
    charge.add(objectCount,2*sizeof(ImportObject));
    charge.add(propertyCapacity,2*sizeof(ImportProperty));
    charge.add(nodes.size(),2*sizeof(ImportLineEvidence));
    // Scratch node -> object, seen/duplicate property bitsets and source counts.
    charge.add(nodes.size(),2*sizeof(std::size_t));
    charge.add(objectCount,6*sizeof(std::uint64_t));
    ReaperImportPreview result(std::move(structure));
    result.lease_=resources.reserve(charge.bytes());
    result.objects_.reserve(objectCount); result.properties_.reserve(propertyCapacity);
    result.lines_.resize(nodes.size());
    std::size_t retained=0;
    {
        std::vector<std::size_t> context(nodes.size(),absent);
        std::vector<std::uint64_t> seen(objectCount), duplicates(objectCount), sources(objectCount);
        PayloadCharge actual("Foreign import capacity",charge.bytes());
        actual.add(sizeof(ReaperImportPreview)); actual.add(result.objects_.capacity(),sizeof(ImportObject));
        actual.add(result.properties_.capacity(),sizeof(ImportProperty)); actual.add(result.lines_.capacity(),sizeof(ImportLineEvidence));
        retained=actual.bytes();
        actual.add(context.capacity(),sizeof(std::size_t));
        actual.add(seen.capacity()+duplicates.capacity()+sources.capacity(),sizeof(std::uint64_t));
        auto emit=[&](std::size_t owner, std::size_t node, FieldId id, ForeignByteRange raw,
                      ImportValueKind kind, Status status, Reason reason) {
            if (result.properties_.size()==propertyCapacity)
                throw ResourceLimitError("Foreign import properties",result.properties_.size()+1,propertyCapacity);
            ImportProperty p; p.object=owner; p.node=node; p.id=id; p.value=raw;
            p.kind=kind; p.status=status; p.reason=reason;
            if (kind==ImportValueKind::Number) {
                const auto value=result.structure_.bytes(raw);
                const auto parsed=std::from_chars(value.data(),value.data()+value.size(),p.number,std::chars_format::general);
                if (parsed.ec!=std::errc{} || parsed.ptr!=value.data()+value.size() || !numberValid(id,p.number)) {
                    p.kind=ImportValueKind::None; p.number=0; p.status=Status::Unverified; p.reason=Reason::InvalidNumber;
                }
            }
            if (seen[owner]&bit(id)) duplicates[owner]|=bit(id);
            seen[owner]|=bit(id); result.properties_.push_back(p);
        };
        for (std::size_t i=0;i<nodes.size();++i) {
            canceled(stop); const auto &node=nodes[i];
            const auto parent=node.parent==absent ? absent : context[node.parent];
            context[i]=parent;
            const auto kind=objectKind(result.structure_,i);
            if (kind) {
                const auto owner=result.objects_.size();
                ImportObject obj; obj.kind=*kind; obj.node=i; obj.parent=parent;
                if (*kind==ImportObjectKind::Source) {
                    const auto args=tokens(result.structure_,node,limits.maximumTokensPerMappedLine);
                    if (args.valid && args.count==1) obj.sourceType=args.values[0];
                    ++sources[parent];
                    if (result.structure_.bytes(obj.sourceType)!="WAVE")
                        result.lines_[i]={Status::Unsupported,Reason::SourceNotImplemented};
                }
                result.objects_.push_back(obj); context[i]=owner;
            }
            if (node.kind==ReaperLineKind::Blank || node.kind==ReaperLineKind::BlockClose) {
                result.lines_[i]={Status::Preserved,Reason::OriginalValue}; continue;
            }
            const auto owner=context[i];
            if (owner==absent) continue;
            auto &obj=result.objects_[owner];
            const auto key=result.structure_.bytes(node.key);
            if (node.kind==ReaperLineKind::BlockOpen && node.parent==obj.node &&
                ((obj.kind==ImportObjectKind::Project && key=="TEMPOENVEX") ||
                 (obj.kind==ImportObjectKind::Track && key=="FXCHAIN")))
                result.lines_[i]={Status::Unsupported,Reason::ProcessingNotImplemented};
            // Nested opaque/plugin/envelope fields never inherit track semantics.
            if (node.kind!=ReaperLineKind::Data || node.parent!=obj.node) continue;
            if (obj.kind==ImportObjectKind::Item && (key=="TAKE" || key=="TAKESEL")) obj.singleTake=false;
            const auto map=[&](std::size_t expected, std::initializer_list<std::pair<FieldId,std::size_t>> fields,
                               bool bytes=false, bool partial=false, bool processing=false) {
                const auto args=tokens(result.structure_,node,limits.maximumTokensPerMappedLine);
                const bool valid=args.valid && args.count==expected;
                for (const auto &[id,slot]:fields) {
                    const auto raw=valid ? args.values[slot] : node.line;
                    emit(owner,i,id,raw, valid ? (bytes ? ImportValueKind::Bytes : ImportValueKind::Number) : ImportValueKind::None,
                         valid ? (processing ? Status::Unsupported : Status::Preserved) : Status::Unverified,
                         valid ? (processing ? Reason::ProcessingNotImplemented : Reason::OriginalValue) :
                                 (args.valid ? Reason::UnknownShape : Reason::InvalidToken));
                }
                if (valid && !partial && !processing) result.lines_[i]={Status::Preserved,Reason::OriginalValue};
            };
            switch (obj.kind) {
            case ImportObjectKind::Project:
                if (key=="SAMPLERATE") map(3,{{FieldId::ProjectSampleRate,0},{FieldId::ProjectSampleRateEnabled,1}},false,true);
                else if (key=="TEMPO" || key=="PLAYRATE") result.lines_[i]={Status::Unsupported,Reason::ProcessingNotImplemented};
                break;
            case ImportObjectKind::Track:
                if (key=="NAME") map(1,{{FieldId::TrackName,0}},true);
                else if (key=="TRACKID") map(1,{{FieldId::TrackIdentity,0}},true);
                else if (key=="NCHAN") map(1,{{FieldId::TrackChannels,0}});
                else if (key=="VOLPAN") map(5,{{FieldId::TrackGain,0},{FieldId::TrackPan,1}},false,true);
                else if (key=="AUXRECV") result.lines_[i]={Status::Unsupported,Reason::ProcessingNotImplemented};
                break;
            case ImportObjectKind::Item:
                if (key=="IGUID") map(1,{{FieldId::ItemIdentity,0}},true);
                else if (key=="NAME") map(1,{{FieldId::TakeName,0}},true);
                else if (key=="POSITION") map(1,{{FieldId::ItemPosition,0}});
                else if (key=="LENGTH") map(1,{{FieldId::ItemLength,0}});
                else if (key=="FADEIN") map(13,{{FieldId::ItemFadeIn,1}},false,true);
                else if (key=="FADEOUT") map(13,{{FieldId::ItemFadeOut,1}},false,true);
                else if (key=="VOLPAN") map(4,{{FieldId::ItemGain,0},{FieldId::TakePan,1},{FieldId::TakeGain,2}},false,true);
                else if (key=="SOFFS") map(1,{{FieldId::TakeSourceOffset,0}});
                else if (key=="PLAYRATE") map(6,{{FieldId::TakeRate,0},{FieldId::TakePitch,2}},false,true,true);
                break;
            case ImportObjectKind::Source:
                if (result.structure_.bytes(obj.sourceType)!="WAVE") result.lines_[i]={Status::Unsupported,Reason::SourceNotImplemented};
                else if (key=="FILE") map(1,{{FieldId::SourceFile,0}},true);
                break;
            }
        }
        for (std::size_t i=0;i<result.objects_.size();++i) {
            canceled(stop); auto &obj=result.objects_[i];
            if (obj.kind==ImportObjectKind::Item && sources[i]!=1) obj.singleTake=false;
        }
        for (std::size_t i=0;i<nodes.size();++i) {
            canceled(stop);
            if (nodes[i].kind==ReaperLineKind::BlockOpen && result.structure_.bytes(nodes[i].key)=="TAKE" &&
                nodes[i].parent!=absent) {
                const auto owner=context[nodes[i].parent];
                if (owner!=absent && result.objects_[owner].kind==ImportObjectKind::Item &&
                    nodes[i].parent==result.objects_[owner].node) result.objects_[owner].singleTake=false;
            }
        }
        for (auto &p:result.properties_) {
            canceled(stop);
            if (duplicates[p.object]&bit(p.id)) {
                p.status=Status::Unverified; p.reason=Reason::DuplicateProperty;
            } else if (takeField(p.id) && !result.objects_[p.object].singleTake) {
                p.status=Status::Unverified; p.reason=Reason::AmbiguousTake;
            }
            if (p.status!=Status::Preserved && p.node!=absent) {
                const ImportLineEvidence next{p.status,p.reason};
                auto &line=result.lines_[p.node];
                if (evidencePriority(next)>evidencePriority(line)) line=next;
            }
        }
        for (std::size_t i=0;i<result.objects_.size();++i) {
            canceled(stop);
            for (const auto id:required(result.objects_[i],result.structure_)) if (!(seen[i]&bit(id)))
                emit(i,absent,id,{},ImportValueKind::None,Status::Missing,Reason::MissingProperty);
        }
    } // Release scratch banks before returning their credit, off audio.
    result.lease_.resize(retained); canceled(stop);
    return result;
}
} // namespace soundcurrent::daw
