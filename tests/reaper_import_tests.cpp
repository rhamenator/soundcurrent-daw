// SPDX-License-Identifier: GPL-3.0-only
#include <soundcurrent/reaper_import.hpp>
#include <nlohmann/json.hpp>
#include <atomic>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <optional>
#include <thread>
#include <type_traits>

using namespace soundcurrent::daw;
namespace {
using FieldId=ImportPropertyId;
using Status=ImportEvidenceStatus;
using Reason=ImportEvidenceReason;
unsigned checks=0;
void check(bool ok,const char *message) {
    ++checks; if (!ok) throw std::runtime_error(message);
}
std::string read(const std::filesystem::path &path) {
    std::ifstream file(path,std::ios::binary);
    if (!file) throw std::runtime_error("Cannot read frozen native corpus");
    return {std::istreambuf_iterator<char>(file),std::istreambuf_iterator<char>()};
}
const ImportProperty &field(const ReaperImportPreview &p,std::size_t object,FieldId id) {
    const auto *value=p.property(object,id); check(value!=nullptr,"Native field missing or duplicated"); return *value;
}
void scalar(const ReaperImportPreview &p,std::size_t object,FieldId id,double expected,Status status=Status::Preserved) {
    const auto &v=field(p,object,id);
    check(v.kind==ImportValueKind::Number && v.number==expected && v.status==status,
          "Decoded foreign scalar disagrees with actual native API readback");
}
void text(const ReaperImportPreview &p,std::size_t object,FieldId id,std::string_view expected) {
    const auto &v=field(p,object,id);
    check(v.kind==ImportValueKind::Bytes && v.status==Status::Preserved && p.structure().bytes(v.value)==expected,
          "Foreign byte token differs from native writer readback");
}
void empty(const ResourceLedger &resources) {
    check(resources.usage().reservedBytes==0 && resources.usage().owners==0,"IR leaked resource admission");
}
template<class F> void refusal(F &&fn,ErrorCode code) {
    bool refused=false;
    try { fn(); } catch (const ProjectError &error) { refused=true; check(error.code()==code,"Incorrect mapping refusal"); }
    check(refused,"Mapping ignored admission/cancellation limit");
}
void nativeCorpus(const std::filesystem::path &root) {
    ResourceLedger resources(16*1024*1024,"Native property corpus");
    for (const auto *name: {"empty","unicode-mono","offset-fades","stereo-gain-pan","rate-pitch","opaque-state-routing","midi-tempo"}) {
        const auto source=read(root/(std::string(name)+".rpp"));
        const auto witness=nlohmann::json::parse(read(root/(std::string(name)+".observed.json")));
        check(witness.at("beforeSave")==witness.at("afterReopen"),"Native save/reopen witness changed");
        const auto &observed=witness.at("afterReopen");
        {
            auto preview=inspectReaperImport(source,{},resources);
            check(preview.structure().source()==source,"IR did not retain exact original bytes");
            check(preview.lines().size()==preview.structure().nodes().size(),"Missing opaque line evidence");
            scalar(preview,0,FieldId::ProjectSampleRate,observed.at("projectSampleRate").get<double>());
            scalar(preview,0,FieldId::ProjectSampleRateEnabled,1);
            std::size_t track=0,item=0,sourceCount=0;
            std::size_t parentTrack=0,parentItem=0;
            for (std::size_t o=1;o<preview.objects().size();++o) {
                const auto &object=preview.objects()[o];
                check(object.parent<o && object.node<preview.structure().nodes().size(),"IR hierarchy escaped its owner");
                if (object.kind==ImportObjectKind::Track) {
                    parentTrack=track++; parentItem=0;
                    const auto &t=observed.at("tracks").at(parentTrack);
                    text(preview,o,FieldId::TrackName,t.at("name").get<std::string>());
                    text(preview,o,FieldId::TrackIdentity,t.at("guid").get<std::string>());
                    scalar(preview,o,FieldId::TrackGain,t.at("gain").get<double>());
                    scalar(preview,o,FieldId::TrackPan,t.at("pan").get<double>());
                    scalar(preview,o,FieldId::TrackChannels,t.at("channels").get<double>());
                } else if (object.kind==ImportObjectKind::Item) {
                    const auto &v=observed.at("tracks").at(parentTrack).at("items").at(parentItem++); ++item;
                    check(object.singleTake,"Native single take was marked ambiguous");
                    scalar(preview,o,FieldId::ItemPosition,v.at("position").get<double>());
                    scalar(preview,o,FieldId::ItemLength,v.at("length").get<double>());
                    scalar(preview,o,FieldId::ItemFadeIn,v.at("fadeIn").get<double>());
                    scalar(preview,o,FieldId::ItemFadeOut,v.at("fadeOut").get<double>());
                    scalar(preview,o,FieldId::ItemGain,v.at("gain").get<double>());
                    scalar(preview,o,FieldId::TakeGain,v.at("takeGain").get<double>());
                    scalar(preview,o,FieldId::TakePan,v.at("takePan").get<double>());
                    text(preview,o,FieldId::TakeName,v.at("name").get<std::string>());
                    scalar(preview,o,FieldId::TakeRate,v.at("playRate").get<double>(),Status::Unsupported);
                    scalar(preview,o,FieldId::TakePitch,v.at("pitch").get<double>(),Status::Unsupported);
                    if (!v.at("midi").get<bool>()) scalar(preview,o,FieldId::TakeSourceOffset,v.at("sourceOffset").get<double>());
                    else check(field(preview,o,FieldId::TakeSourceOffset).status==Status::Unverified,"MIDI-specific offset flags silently mapped");
                } else if (object.kind==ImportObjectKind::Source) {
                    ++sourceCount;
                    const auto &v=observed.at("tracks").at(parentTrack).at("items").at(parentItem-1);
                    if (!v.at("midi").get<bool>()) {
                        check(preview.structure().bytes(object.sourceType)=="WAVE","Audio source kind lost");
                        const auto nativePath=v.at("sourceFile").get<std::string>();
                        text(preview,o,FieldId::SourceFile,"media/"+nativePath.substr(nativePath.find_last_of('/')+1));
                    } else {
                        check(preview.structure().bytes(object.sourceType)=="MIDI" && !preview.property(o,FieldId::SourceFile),
                              "Embedded MIDI source fabricated a media path");
                    }
                }
            }
            std::size_t nativeItems=0;
            for (const auto &t:observed.at("tracks")) nativeItems+=t.at("items").size();
            check(track==observed.at("tracks").size() && item==nativeItems && sourceCount==nativeItems,
                  "IR objects disagree with native object counts");
            for (const auto &p:preview.properties()) {
                check(p.status!=Status::Converted && p.status!=Status::Missing,"Corpus falsely converted or lost required fields");
                check(p.object<preview.objects().size() && p.node<preview.structure().nodes().size(),"Property has foreign owner/range");
                check(p.value.begin<=source.size() && p.value.length<=source.size()-p.value.begin,"Property escaped exact source bytes");
            }
            const auto usage=resources.usage();
            check(usage.owners==2 && usage.reservedBytes==preview.chargedBytes(),"IR/source owners do not match retained charge");
            auto moved=std::move(preview);
            check(resources.usage()==usage && moved.structure().source()==source,"IR move changed original bytes/admission");
            check(read(root/(std::string(name)+".rpp"))==source,"Original project was modified");
        }
        empty(resources);
    }
}
const std::string single=
    "<REAPER_PROJECT 0.1 \"untrusted-version\"\nSAMPLERATE 48000 1 0\n"
    "<TRACK {opening-is-not-a-destination-uuid}\nTRACKID {raw-foreign-identity}\nNAME `Voix 'été'`\n"
    "NCHAN 2\nVOLPAN 0.5 0.25 -1 -1 1\n<ITEM\nPOSITION 1.25\nLENGTH 2\n"
    "NAME 'Prise \"quoted\"'\nVOLPAN 0.75 -0.25 0.625 -1\nSOFFS -0.125\n"
    "PLAYRATE 1 1 0 -1 0 0.0025\n<SOURCE WAVE\nFILE `../unapproved/C:\\literal\\name.wav`\n>\n>\n"
    "<FXCHAIN\nNAME \"not-a-track-name\"\n<TRACK\nNAME \"not-a-real-track\"\n>\n"
    "SCRIPT `do_not_execute()`\n>\n>\n>\n";
std::string replace(std::string value,std::string_view from,std::string_view to) {
    const auto p=value.find(from); if (p==std::string::npos) throw std::runtime_error("Test mutation missing");
    value.replace(p,from.size(),to); return value;
}
void opaqueAndRefusals() {
    ResourceLedger resources(16*1024*1024);
    {
        auto p=inspectReaperImport(single,{},resources);
        check(p.objects().size()==4,"Opaque nested TRACK interpreted as a real track");
        text(p,1,FieldId::TrackName,"Voix 'été'"); text(p,2,FieldId::TakeName,"Prise \"quoted\"");
        text(p,3,FieldId::SourceFile,"../unapproved/C:\\literal\\name.wav");
        scalar(p,2,FieldId::TakeSourceOffset,-0.125);
        scalar(p,2,FieldId::ItemGain,0.75); scalar(p,2,FieldId::TakeGain,0.625);
        const auto &missing=field(p,2,FieldId::ItemFadeIn);
        check(missing.kind==ImportValueKind::None && missing.status==Status::Missing && missing.node==ReaperStructureNode::noParent,
              "Missing field silently defaulted to a destination value");
        std::size_t opaque=0;
        for (std::size_t i=0;i<p.lines().size();++i) if (p.structure().bytes(p.structure().nodes()[i].key)=="SCRIPT") {
            ++opaque; check(p.lines()[i].status==Status::Unverified,"Opaque code gained semantic qualification");
        }
        check(opaque==1 && p.structure().source()==single,"Opaque extension bytes lost");
    }
    empty(resources);
    {
        auto p=inspectReaperImport(std::string("\xEF\xBB\xBF \r\n")+single,{},resources);
        check(p.structure().root()!=0 && p.objects()[0].node==p.structure().root(),"Leading BOM/blank changed project object identity");
        scalar(p,0,FieldId::ProjectSampleRate,48000);
        text(p,1,FieldId::TrackName,"Voix 'été'");
    }
    empty(resources);
    for (const std::string badNumber:{"nan","inf","1e99999","1junk","1,25","0x20","\"bad\""}) {
        auto p=inspectReaperImport(replace(single,"POSITION 1.25","POSITION "+badNumber),{},resources);
        const auto &v=field(p,2,FieldId::ItemPosition);
        check(v.kind==ImportValueKind::None && v.status==Status::Unverified && v.reason==Reason::InvalidNumber,
              "Invalid foreign number parsed permissively or defaulted");
        check(p.lines()[v.node].status==Status::Unverified,"Malformed property line marked preserved");
    }
    empty(resources);
    for (const auto badChannels:{"0","-2","2.5"}) {
        auto p=inspectReaperImport(replace(single,"NCHAN 2",std::string("NCHAN ")+badChannels),{},resources);
        check(field(p,1,FieldId::TrackChannels).reason==Reason::InvalidNumber,"Invalid layout count mapped");
    }
    empty(resources);
    for (const auto badName:{"NAME \"unterminated", "NAME \"closed\"adjacent", "NAME one two"}) {
        auto p=inspectReaperImport(replace(single,"NAME `Voix 'été'`",badName),{},resources);
        check(field(p,1,FieldId::TrackName).status==Status::Unverified,"Unqualified lexical shape mapped");
    }
    empty(resources);
    {
        auto p=inspectReaperImport(replace(single,"NCHAN 2","NCHAN 2\nNCHAN 4"),{},resources);
        check(!p.property(1,FieldId::TrackChannels),"Duplicate scalar silently selected first/last value");
        unsigned duplicate=0;
        for (const auto &v:p.properties()) if (v.id==FieldId::TrackChannels) {
            ++duplicate; check(v.status==Status::Unverified && v.reason==Reason::DuplicateProperty,"Duplicate property qualified");
        }
        check(duplicate==2,"Duplicate source occurrences discarded");
    }
    empty(resources);
    for (const auto extra:{"<SOURCE WAVE\nFILE second.wav\n>\n", "TAKE\n", "<TAKE\nNAME second\n>\n"}) {
        auto p=inspectReaperImport(replace(single,"<SOURCE WAVE\n",std::string(extra)+"<SOURCE WAVE\n"),{},resources);
        check(!p.objects()[2].singleTake && field(p,2,FieldId::TakeName).reason==Reason::AmbiguousTake,
              "Multiple/nested take state silently selected a take");
        scalar(p,2,FieldId::ItemPosition,1.25);
    }
    empty(resources);
    for (const auto sourceType:{"MIDI","SECTION","UNRECOGNIZED"}) {
        auto p=inspectReaperImport(replace(single,"SOURCE WAVE",std::string("SOURCE ")+sourceType),{},resources);
        check(!p.property(3,FieldId::SourceFile),"Unknown/embedded source treated as filesystem media");
        const auto &obj=p.objects()[3];
        check(p.structure().bytes(obj.sourceType)==sourceType,"Unknown source kind lost");
        check(p.lines()[obj.node+1].status==Status::Unsupported,"Unknown source field lacks unsupported evidence");
    }
    empty(resources);
    {
        auto limited=ReaperImportLimits{}; limited.maximumObjects=3;
        refusal([&] { inspectReaperImport(single,limited,resources); },ErrorCode::ResourceLimit); empty(resources);
        limited={}; limited.maximumProperties=3;
        refusal([&] { inspectReaperImport(single,limited,resources); },ErrorCode::ResourceLimit); empty(resources);
        limited={}; limited.maximumTokensPerMappedLine=3;
        refusal([&] { inspectReaperImport(single,limited,resources); },ErrorCode::ResourceLimit); empty(resources);
        limited={}; limited.maximumTokensPerMappedLine=33;
        refusal([&] { inspectReaperImport(single,limited,resources); },ErrorCode::InvalidState); empty(resources);
        limited={}; limited.maximumObjects=4; limited.maximumProperties=20;
        {
            auto p=inspectReaperImport(single,limited,resources);
            check(p.objects().size()==4 && p.properties().size()==20,"Exact object/property bounds refused");
        }
        empty(resources);
        std::stop_source stop; stop.request_stop();
        refusal([&] { inspectReaperImport(single,{},resources,stop.get_token()); },ErrorCode::Canceled); empty(resources);
    }
    const auto peak=resources.usage().peakBytes;
    ResourceLedger tooSmall(peak/4);
    refusal([&] { inspectReaperImport(single,{},tooSmall); },ErrorCode::ResourceLimit); empty(tooSmall);
    std::optional<ReaperImportPreview> retained;
    {
        ResourceLedger facade;
        retained.emplace(inspectReaperImport(single,{},facade));
    }
    text(*retained,1,FieldId::TrackName,"Voix 'été'"); retained.reset();
}
void mappingCancellation() {
    // Cancel after the second owner is admitted: exercises mapping retirement,
    // rather than only the structure preflight's already-canceled path.
    std::string many="<REAPER_PROJECT\n";
    for (unsigned i=0;i<15000;++i) many+="<TRACK\nNAME ordinary\nNCHAN 2\n>\n";
    many+=">\n";
    ResourceLedger resources(256*1024*1024);
    std::stop_source stop; std::atomic<bool> finished=false, sawAdmission=false;
    std::jthread interrupt([&](std::stop_token done) {
        while (!done.stop_requested() && !finished.load(std::memory_order_acquire)) {
            if (resources.usage().owners>=2) { sawAdmission=true; stop.request_stop(); return; }
            std::this_thread::yield();
        }
    });
    bool canceledMapping=false;
    try { auto p=inspectReaperImport(many,{},resources,stop.get_token()); }
    catch (const ProjectError &e) { check(e.code()==ErrorCode::Canceled,"Large mapping failed for another reason"); canceledMapping=true; }
    finished.store(true,std::memory_order_release); interrupt.join();
    check(sawAdmission && canceledMapping,"Mapping-stage cancellation was not observed"); empty(resources);
}
}
int main(int argc,char **argv) {
    static_assert(!std::is_copy_constructible_v<ReaperImportPreview>);
    static_assert(!std::is_move_assignable_v<ReaperImportPreview>);
    try {
        if (argc!=2) throw std::runtime_error("Native corpus path required");
        nativeCorpus(std::filesystem::path(argv[1])); opaqueAndRefusals(); mappingCancellation();
        std::cout << "Reaper import properties: " << checks << " checks; original scalar/byte preservation only, conversion/render unqualified\n";
        return 0;
    } catch (const std::exception &e) { std::cerr << e.what() << '\n'; return 1; }
}
