// SPDX-License-Identifier: GPL-3.0-only
#include <soundcurrent/project_import_state.hpp>
#include <soundcurrent/wave_validation.hpp>
#include <algorithm>
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif
namespace soundcurrent::daw {
namespace {
void check(bool good,const char *message,ErrorCode code=ErrorCode::MediaMismatch) {
    if (!good) throw ProjectError(code,message);
}
void poll(std::stop_token stop) {
    if (stop.stop_requested()) throw ProjectError(ErrorCode::Canceled,"Import evidence canceled");
}
void directory(const std::filesystem::path &path) {
    check(std::filesystem::is_directory(std::filesystem::symlink_status(path)),
          "Import evidence parent must be a plain directory",ErrorCode::Io);
#ifdef _WIN32
    const auto attributes=GetFileAttributesW(path.c_str());
    check(attributes!=INVALID_FILE_ATTRIBUTES && !(attributes&FILE_ATTRIBUTE_REPARSE_POINT),
          "Import evidence reparse parent refused",ErrorCode::Io);
#endif
}
std::string digest(const std::array<char,64> &value) { return {value.data(),value.size()}; }
std::size_t descriptorCharge(const ImportedProjectSource &source) {
    PayloadCharge charge("Import evidence descriptor",64*1024*1024);
    charge.add(4096);
    const auto string=[&](const std::string &s){charge.add(s.size(),2);charge.add(128);};
    string(source.adapterId);string(source.sourceSha256);
    string(source.inspection.relativePath);string(source.inspection.sha256);
    charge.add(source.media.size(),sizeof(ImportedMediaOrigin)+sizeof(std::unique_ptr<MediaProvenance>)+512);
    for (const auto &media:source.media) {string(media.receipt.relativePath);string(media.receipt.sha256);}
    return charge.bytes();
}
ImportInspectionReport inspection(ApprovedMediaRoot &root,const ImportedProjectSource &source,
    ResourceLedger memory,InspectionBundleLimits limits,std::stop_token stop,
    const std::function<void()> &beforeRead = {}) {
    if (beforeRead) beforeRead();
    validateImportedProjectSource(source);
    check(limits.sourceBytes>0 && limits.sourceBytes<=16*1024*1024 &&
          limits.protocolBytes>0 && limits.protocolBytes<=64*1024*1024,
          "Import inspection policy exceeds project envelope",ErrorCode::InvalidParameter);
    const std::uint64_t maximum=std::uint64_t(limits.sourceBytes)+limits.protocolBytes+96;
    auto file=root.open(source.inspection.relativePath,maximum,stop);
    InspectionBundleFingerprint fingerprint;
    auto report=loadInspectionBundle(file,memory,limits,stop,&fingerprint,beforeRead);
    check(fingerprint.bytes==source.inspection.bytes && digest(fingerprint.sha256)==source.inspection.sha256 &&
          report.source().size()==source.sourceBytes && report.sha256()==source.sourceSha256,
          "Owned import inspection differs from project evidence");
    check(report.hasProperties()==(source.adapterId=="reaper-rpp-properties-v1"),
          "Import evidence adapter differs from inspection");
    return report;
}
void agrees(const ImportInspectionReport &report,const MediaProvenance &origin,const Asset &asset,
    ApprovedMediaRoot &root,ResourceLedger memory,std::uint64_t maximum,std::stop_token stop,
    const std::function<void()> &beforeRead = {}) {
    if (beforeRead) beforeRead();
    check(report.ownedBy(memory) && origin.ownedBy(memory),"Import evidence belongs to another scope",ErrorCode::InvalidState);
    check(origin.data().phase==MediaReceiptPhase::Verified,"Imported media is not a verified receipt",ErrorCode::InvalidState);
    validateRelativeMediaPath(asset.relativePath);
    auto file=root.open(asset.relativePath,maximum,stop);
    const auto audio=validateApprovedWave(file,{},stop,{},beforeRead);
    check(digest(audio.sourceSha256)==asset.sha256 && audio.frames==std::uint64_t(asset.frames) &&
          audio.rate==asset.sampleRate && audio.channels==asset.layout.channels,
          "Imported media asset differs from actual owned audio");
    const auto &p=origin.data();
    auto bound=bindMediaProvenance(report,p.sourceProperty,audio,p.selectedReference,p.selection,p.operation,memory,stop);
    auto expected=encodeMediaProvenance(origin,MediaReceiptPhase::Planned,memory,stop);
    auto actual=encodeMediaProvenance(bound,MediaReceiptPhase::Planned,memory,stop);
    check(expected.bytes()==actual.bytes(),"Imported media receipt differs from original inspection/asset");
    file.verifyUnchanged();
}
}
bool ProjectImportEvidence::ownedBy(const ResourceLedger &memory) const {
    return memory.owns(lease_) && inspection_.ownedBy(memory) &&
        std::all_of(media_.begin(),media_.end(),[&](const auto &p){return p && p->ownedBy(memory);});
}
ImportedProjectSource preserveProjectImportInspection(const std::filesystem::path &root,
    const ImportInspectionReport &report,ResourceLedger memory,Id sourceId,
    const InspectionBundleSaveOptions &options) {
    poll(options.stop);
    check(report.ownedBy(memory),"Import inspection belongs to another scope",ErrorCode::InvalidState);
    auto work=memory.reserve(65536); // Before metadata copies/directories/files.
    directory(root);
    const auto imports=root/"imports";
    if (!std::filesystem::create_directory(imports)) directory(imports);
    const auto folder=imports/sourceId.str();
    check(std::filesystem::create_directory(folder),"Import source identity already exists",ErrorCode::Io);
    ImportedProjectSource source;source.id=std::move(sourceId);
    source.adapterId=report.hasProperties() ? "reaper-rpp-properties-v1" : "reaper-rpp-outline-v1";
    source.sourceSha256=std::string(report.sha256());source.sourceBytes=report.source().size();
    source.inspection.relativePath="imports/"+source.id.str()+"/inspection.scinspect";
    const auto saved=saveInspectionBundle(root/utf8Path(source.inspection.relativePath),report,memory,{},options);
    ApprovedMediaRoot approved(std::filesystem::absolute(root),memory,{1});
    auto file=approved.open(source.inspection.relativePath,80ULL*1024*1024+96,options.stop);
    InspectionBundleFingerprint fingerprint;
    auto reopened=loadInspectionBundle(file,memory,{},options.stop,&fingerprint);
    check(saved.bytes==fingerprint.bytes && reopened.source()==report.source() && reopened.protocol()==report.protocol(),
          "Preserved import inspection readback differs");
    source.inspection.bytes=fingerprint.bytes;source.inspection.sha256=digest(fingerprint.sha256);
    validateImportedProjectSource(source);
    return source;
}
ImportedMediaOrigin preserveProjectImportMedia(const std::filesystem::path &root,
    const ImportedProjectSource &source,const ImportInspectionReport &report,const MediaProvenance &origin,
    const Asset &asset,ResourceLedger memory,std::stop_token stop) {
    poll(stop);
    check(report.ownedBy(memory) && origin.ownedBy(memory),"Import receipt inputs belong to another scope",ErrorCode::InvalidState);
    auto work=memory.reserve(descriptorCharge(source)+65536);
    validateImportedProjectSource(source);
    directory(root);directory(root/"imports");directory(root/"imports"/source.id.str());
    ApprovedMediaRoot approved(std::filesystem::absolute(root),memory,{2});
    auto archived=inspection(approved,source,memory,{},stop);
    check(archived.source()==report.source() && archived.protocol()==report.protocol(),
          "Receipt selection refers to another inspection");
    agrees(archived,origin,asset,approved,memory,8192ULL*1024*1024,stop);
    ImportedMediaOrigin result;result.assetId=asset.id;result.operation=origin.data().operation;
    result.sourceProperty=origin.data().sourceProperty;
    result.receipt.relativePath="imports/"+source.id.str()+"/"+result.operation.str()+".json";
    auto encoded=encodeMediaProvenance(origin,MediaReceiptPhase::Verified,memory,stop);
    const auto saved=saveNewProjectEvidenceFile(root/utf8Path(result.receipt.relativePath),encoded.bytes(),memory,stop);
    auto file=approved.open(result.receipt.relativePath,mediaReceiptMaximumBytes,stop);
    result.receipt.bytes=file.size();result.receipt.sha256=digest(file.digest(stop));
    check(saved.bytes==result.receipt.bytes && result.receipt.sha256==digest(hashForeignSnapshot(encoded.bytes(),memory,stop)),
          "Preserved import receipt readback differs");
    file.verifyUnchanged();return result;
}
ProjectImportEvidence openProjectImportEvidence(const std::filesystem::path &root,const Session &session,
    const Id &sourceId,ResourceLedger memory,ProjectImportLimits limits,std::stop_token stop,
    const std::function<void()> &beforeRead) {
    poll(stop);ValidatedSession checked(session);
    return openProjectImportEvidence(root,checked,sourceId,memory,limits,stop,beforeRead);
}
ProjectImportEvidence openProjectImportEvidence(const std::filesystem::path &root,const ValidatedSession &session,
    const Id &sourceId,ResourceLedger memory,ProjectImportLimits limits,std::stop_token stop,
    const std::function<void()> &beforeRead) {
    if (beforeRead) beforeRead();
    poll(stop);const auto *found=&session.importedSource(sourceId);
    check(limits.maximumMediaBytes>0 && limits.maximumMediaBytes<=8192ULL*1024*1024,
          "Invalid import media policy",ErrorCode::InvalidParameter);
    auto work=memory.reserve(descriptorCharge(*found));
    ApprovedMediaRoot approved(std::filesystem::absolute(root),memory,{2});
    auto report=inspection(approved,*found,memory,limits.inspection,stop,beforeRead);
    ProjectImportEvidence result(std::move(work),*found,std::move(report));
    result.media_.reserve(found->media.size());
    for (const auto &media:found->media) {
        if (beforeRead) beforeRead();
        poll(stop);auto bank=memory.reserve(mediaReceiptMaximumBytes*2+1024);
        auto file=approved.open(media.receipt.relativePath,mediaReceiptMaximumBytes,stop);
        check(file.size()==media.receipt.bytes && digest(file.digest(stop,beforeRead))==media.receipt.sha256,
              "Imported receipt differs from manifest");
        std::string bytes(static_cast<std::size_t>(file.size()),'\0');
        if (beforeRead) beforeRead();
        file.readAt(0,bytes,stop);
        auto origin=std::make_unique<MediaProvenance>(decodeMediaProvenance(bytes,memory,stop));file.verifyUnchanged();
        check(origin->data().operation==media.operation && origin->data().sourceProperty==media.sourceProperty,
              "Imported receipt identity differs");
        const auto &asset=session.asset(media.assetId);
        agrees(result.inspection_,*origin,asset,approved,memory,limits.maximumMediaBytes,stop,beforeRead);
        result.media_.push_back(std::move(origin));
    }
    return result;
}
} // namespace soundcurrent::daw
