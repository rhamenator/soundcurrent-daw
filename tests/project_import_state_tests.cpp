// SPDX-License-Identifier: GPL-3.0-only
#include <soundcurrent/project_import_state.hpp>
#include <soundcurrent/media_staging.hpp>
#include <soundcurrent/media_recovery.hpp>
#include <soundcurrent/export.hpp>
#include <nlohmann/json.hpp>
#include <fstream>
#include <iostream>
#include <limits>
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif
using namespace soundcurrent::daw;
namespace {
unsigned checks=0;
void check(bool yes,const char *why) {++checks;if (!yes) throw std::runtime_error(why);}
template<class F> void refuses(F f,std::optional<ErrorCode> expected={}) {
    try {f();} catch (const ProjectError &e) {++checks;if (expected) check(e.code()==*expected,"Wrong import-state refusal code");return;}
    throw std::runtime_error("Missing import-state refusal");
}
std::string read(const std::filesystem::path &p) {
    std::ifstream f(p,std::ios::binary);check(bool(f),"Fixture read failed");
    return {std::istreambuf_iterator<char>(f),{}};
}
void write(const std::filesystem::path &p,std::string_view value) {
    std::ofstream f(p,std::ios::binary|std::ios::trunc);f.write(value.data(),std::streamsize(value.size()));f.close();
    check(bool(f),"Fixture write failed");
}
void balanced(const ResourceLedger &ledger,ResourceUsage before) {
    const auto after=ledger.usage();check(after.reservedBytes==before.reservedBytes && after.owners==before.owners,"Import evidence credit leaked");
}
int test(const std::filesystem::path &input) {
    ResourceLedger memory(128*1024*1024,"Import state actual-child fixture");
    {
        auto report=loadInspectionBundle(input/"inspection.scinspect",memory);
        std::size_t ordinal=0;
        for (;ordinal<report.properties().size();++ordinal)
            if (report.properties()[ordinal].id==ImportPropertyId::SourceFile) break;
        check(ordinal<report.properties().size(),"Missing actual-inspector media occurrence");
        const auto &property=report.properties()[ordinal];
        const std::string reference(report.source().substr(property.value.begin,property.value.length));
        auto sourceRoot=std::make_unique<ApprovedMediaRoot>(input,memory);
        auto sourceFile=std::make_unique<ApprovedMediaFile>(sourceRoot->open(reference,1024*1024));
        const auto audio=validateApprovedWave(*sourceFile);
        const auto original=read(input/utf8Path(reference));
        const auto root=input/utf8Path("Owned — Κиїв — Ελλάδα");
        std::filesystem::create_directory(root);std::filesystem::create_directory(root/"media");
        const auto base=memory.usage();
        ImportedProjectSource archive;
        {
            ResourceLedger foreign(1024*1024);
            refuses([&]{preserveProjectImportInspection(root,report,foreign);});
            check(!std::filesystem::exists(root/"imports"),"Foreign-scope failure mutated destination");
            const auto limit=memory.usage().limitBytes;
            memory.configure(memory.usage().reservedBytes+1);
            refuses([&]{preserveProjectImportInspection(root,report,memory);});
            memory.configure(limit);
            check(!std::filesystem::exists(root/"imports"),"Quota failure created destination");
            std::stop_source stopped;stopped.request_stop();
            refuses([&]{preserveProjectImportInspection(root,report,memory,Id::generate(),{stopped.get_token(),{}, {}});});
            check(!std::filesystem::exists(root/"imports"),"Canceled archive mutated destination");
            archive=preserveProjectImportInspection(root,report,memory);
            check(archive.sourceSha256==report.sha256() && archive.sourceBytes==report.source().size() && archive.adapterId=="reaper-rpp-properties-v1","Original inspection authority changed");
            const auto before=read(root/utf8Path(archive.inspection.relativePath));
            refuses([&]{preserveProjectImportInspection(root,report,memory,archive.id);});
            check(read(root/utf8Path(archive.inspection.relativePath))==before,"Existing source archive overwritten");
        }
        balanced(memory,base);
        const auto operation=Id::generate();
        auto planned=bindMediaProvenance(report,ordinal,audio,reference,MediaSelectionKind::ApprovedReference,operation,memory);
        {
            auto staged=stageBoundMedia(*sourceFile,root/"media",planned,1024*1024);
            check(commitStagedMedia(staged,planned).published,"Actual owned audio commit failed");
        }
        sourceFile.reset();sourceRoot.reset();
        auto recovery=recoverStagedMedia(root/"media",operation,memory,1024*1024);
        check(recovery.phase==MediaRecoveryPhase::Verified && recovery.provenance,"Missing actual verified receipt");
        Asset asset;asset.relativePath="media/"+operation.str()+"/media.wav";
        asset.sha256=std::string(audio.sourceSha256.data(),64);asset.sampleRate=audio.rate;asset.frames=Frame(audio.frames);
        asset.layout={audio.channels==1 ? LayoutKind::Mono : audio.channels==2 ? LayoutKind::Stereo : LayoutKind::Discrete,audio.channels};
        refuses([&]{preserveProjectImportMedia(root,archive,report,planned,asset,memory);});
        archive.media.push_back(preserveProjectImportMedia(root,archive,report,*recovery.provenance,asset,memory));
        refuses([&]{preserveProjectImportMedia(root,archive,report,*recovery.provenance,asset,memory);});
        const auto receipt=read(root/utf8Path(archive.media.front().receipt.relativePath));
        auto session=makeOneTrackSession("Portable imported evidence — été", "Owned raw audio",asset.sampleRate);
        session.tracks.front().layout=asset.layout;session.assets.push_back(asset);session.imports.push_back(archive);
        Clip clip;clip.assetId=asset.id;clip.lengthFrames=asset.frames;session.tracks.front().clips.push_back(clip);
        session.exportEndFrame=asset.frames;
        check(decodeProject(encodeProject(session))==session,"Import manifest exact roundtrip differs");
        const auto stable=memory.usage();
        {
            ResourceLedger other;
            auto foreignReport=loadInspectionBundle(input/"inspection.scinspect",other);
            refuses([&]{preserveProjectImportMedia(root,archive,foreignReport,*recovery.provenance,asset,memory);},ErrorCode::InvalidState);
            for (unsigned mode=0;mode<4;++mode) {
                ProjectImportLimits invalid;
                if (mode==0) invalid.inspection.sourceBytes=std::numeric_limits<std::size_t>::max();
                if (mode==1) invalid.inspection.protocolBytes=std::numeric_limits<std::size_t>::max();
                if (mode==2) invalid.maximumMediaBytes=0;
                if (mode==3) invalid.inspection.sourceBytes=0;
                refuses([&]{openProjectImportEvidence(root,session,archive.id,memory,invalid);},ErrorCode::InvalidParameter);
            }
        }
        balanced(memory,stable);
        {
            auto evidence=openProjectImportEvidence(root,session,archive.id,memory);
            check(evidence.ownedBy(memory) && evidence.source()==archive && evidence.inspection().source()==report.source() && evidence.inspection().protocol()==report.protocol(),"Owned source/loss data changed");
            check(evidence.media().size()==1 && evidence.media()[0]->data().audio.peak==audio.peak,"Typed provenance/headroom lost");
            check(evidence.inspection().properties().size()==report.properties().size() && evidence.inspection().lineEvidence().size()==report.lineEvidence().size(),"Opaque/loss inventory lost");
            for (const auto &p:evidence.inspection().properties()) check(p.status!=ImportEvidenceStatus::Converted,"Preservation claimed semantic conversion");
        }
        balanced(memory,stable);
        ProjectStore(root).save(session);check(ProjectStore(root).load()==session,"Imported state save/reopen differs");
        {
            // Determine the complete WAVE validation polling extent on this
            // platform, then cancel inside that extent through ProjectStore and
            // the real export workflow. Earlier manifest hashing cannot satisfy
            // these tests: the requested point lies in the final typed WAVE pass.
            const auto projectBytes=read(root/"project.json");
            ApprovedMediaRoot approved(root,memory,{2});
            auto wave=approved.open(asset.relativePath,1024*1024);
            std::size_t wavePolls=0;
            validateApprovedWave(wave,{}, {}, {},[&]{++wavePolls;});
            check(wavePolls>4,"WAVE validation did not poll content reads");
            std::size_t decodedBlocks=0;
            WaveValidationLimits blocks;blocks.blockFrames=1;
            refuses([&]{validateApprovedWave(wave,blocks,{},
                [&](std::uint64_t,std::span<const double>){++decodedBlocks;},[&]{
                    if(decodedBlocks) throw ProjectError(ErrorCode::Canceled,"Cancel after first decoded frame");
                });},ErrorCode::Canceled);
            check(decodedBlocks==1 && audio.frames>1,"Content cancellation decoded the whole source");
            auto bundle=approved.open(archive.inspection.relativePath,80ULL*1024*1024+96);
            InspectionBundleFingerprint untouched;untouched.bytes=123;
            std::size_t bundlePolls=0;
            refuses([&]{loadInspectionBundle(bundle,memory,{}, {},&untouched,[&]{
                if(++bundlePolls==2) throw ProjectError(ErrorCode::Canceled,"Cancel pinned bundle read");
            });},ErrorCode::Canceled);
            check(bundlePolls==2 && untouched.bytes==123,"Canceled bundle published a fingerprint");
            std::size_t verifyPolls=0;
            ProjectStore(root).verifyMedia(session,[&]{++verifyPolls;});
            check(verifyPolls>wavePolls,"Typed evidence reads did not reach ProjectStore callback");
            const auto stopAt=verifyPolls-wavePolls+wavePolls/2;
            std::size_t canceledPolls=0;
            refuses([&]{ProjectStore(root).verifyMedia(session,[&]{
                if(++canceledPolls==stopAt) throw ProjectError(ErrorCode::Canceled,"Cancel typed imported WAVE");
            });},ErrorCode::Canceled);
            check(canceledPolls==stopAt && canceledPolls<verifyPolls,"Imported WAVE cancellation waited for full validation");
            std::filesystem::create_directory(root/"exports");
            ExportSpec spec(session.tracks.front().id);spec.endFrame=asset.frames;
            bool late=false;std::size_t latePolls=0,beforePublication=0;
            ExportOptions observe;observe.resources=memory;
            observe.canceled=[&]{if(late) ++latePolls;return false;};
            observe.boundary=[&](ExportBoundary b,Frame){
                if(b==ExportBoundary::BeforeFlush) late=true;
                if(b==ExportBoundary::BeforePublish) beforePublication=latePolls;
            };
            const auto result=exportTrackWav(root,session,root/"exports"/"observed.wav",spec,observe);
            check(result.frames==asset.frames && beforePublication>wavePolls+1,"Observed import export did not complete validation");
            // BeforePublish's initial poll follows the final typed WAVE poll.
            const auto exportStopAt=beforePublication-1-wavePolls+wavePolls/2;
            late=false;latePolls=0;bool publishedBoundary=false;
            ExportOptions cancel;cancel.resources=memory;
            cancel.canceled=[&]{return late && ++latePolls==exportStopAt;};
            cancel.boundary=[&](ExportBoundary b,Frame){
                if(b==ExportBoundary::BeforeFlush) late=true;
                if(b==ExportBoundary::BeforePublish) publishedBoundary=true;
            };
            refuses([&]{exportTrackWav(root,session,root/"exports"/"canceled.wav",spec,cancel);},ErrorCode::Canceled);
            check(latePolls==exportStopAt && !publishedBoundary &&
                  !std::filesystem::exists(root/"exports"/"canceled.wav"),"Canceled typed import validation published an export");
            check(std::distance(std::filesystem::directory_iterator(root/"exports"),std::filesystem::directory_iterator{})==1,
                  "Canceled export leaked a temporary file");
            check(read(root/"project.json")==projectBytes,"Read cancellation changed the saved project");
        }
        balanced(memory,stable);
        EditHistory history(session);const auto &track=session.tracks.front();
        ParameterAddress gain{track.id,track.eq.id,track.eq.bands.front().id,BandParameter::GainDb};
        history.begin(gain);history.update(3);history.commit();
        ProjectStore(root).save(session);check(ProjectStore(root).load().imports==session.imports && history.undo() && history.redo(),"Edit/save/history dropped import evidence");
        check(ProjectStore(root).loadPrevious().imports==session.imports,"Backup dropped original loss state");
        auto j=nlohmann::json::parse(encodeProject(session));
        check(j["schemaMinor"]==14 && j["imports"][0]["media"][0]["sourceProperty"]==ordinal,"Stable import identifiers differ");
        for (unsigned mode=0;mode<20;++mode) {
            auto bad=j;auto &i=bad["imports"][0];auto &m=i["media"][0];
            switch(mode) {
            case 0:i["inspection"]["path"]="../outside";break;
            case 1:i["inspection"]["sha256"]="BAD";break;
            case 2:i["sourceBytes"]=1.5;break;
            case 3:i["sourceBytes"]=true;break;
            case 4:i["sourceBytes"]=UINT64_MAX;break;
            case 5:i["inspection"]["bytes"]=0;break;
            case 6:i["inspection"]["bytes"]=80*1024*1024+97;break;
            case 7:i["sourceSha256"]="";break;
            case 8:i["originalRoot"]="/unapproved";break;
            case 9:i.erase("inspection");break;
            case 10:bad["imports"].push_back(i);break;
            case 11:m["assetId"]=Id::generate().str();break;
            case 12:m["operation"]=session.id.str();break;
            case 13:m["sourceProperty"]=-1;break;
            case 14:m["sourceProperty"]=1000000;break;
            case 15:m["receipt"]["path"]="imports/CON.json";break;
            case 16:m["receipt"]["bytes"]=16385;break;
            case 17:i["media"].push_back(m);break;
            case 18:bad.erase("imports");break;
            case 19:i["adapterId"]="unknown-native-v99";break;
            }
            refuses([&]{decodeProject(bad.dump());});
        }
        auto duplicate=j.dump();duplicate.insert(1,"\"imports\":[],");refuses([&]{decodeProject(duplicate);});
        for (unsigned minor=0;minor<8;++minor) {
            auto plain=makeOneTrackSession("Legacy", "Raw");auto old=nlohmann::json::parse(encodeProject(plain));
            old["schemaMinor"]=minor;old.erase("imports");
            if (minor<4) old.erase("punchRecording");
            if (minor<3) old.erase("master");
            for (auto &t:old["tracks"]) {
                for (auto &c:t["clips"]) {c.erase("processing");c.erase("sourceTiming");c.erase("playbackRate");c.erase("stretch");}
                if (minor<5) t.erase("inputLatencyFrames");
                if (minor<2) t.erase("monitoringMode");
                if (minor==0) {t.erase("monitorIntent");t["inputIntent"].erase("ports");t["outputIntent"].erase("ports");}
            }
            check(decodeProject(old.dump())==plain,"Older schema invented/lost import state");
            old["imports"]=j["imports"];refuses([&]{decodeProject(old.dump());});
        }
        // The opened bundle, not a replacement path, is the decoder's authority.
        {
            ApprovedMediaRoot approved(root,memory,{1});
            auto pinned=approved.open(archive.inspection.relativePath,80ULL*1024*1024+96);
            InspectionBundleFingerprint fingerprint;
            const auto archivePath=root/utf8Path(archive.inspection.relativePath);
            const auto oldPath=std::filesystem::path(archivePath.native()+std::filesystem::path(".held").native());
#ifndef _WIN32
            auto loaded=loadInspectionBundle(pinned,memory,{}, {},&fingerprint);
            check(loaded.source()==report.source() && std::string(fingerprint.sha256.data(),64)==archive.inspection.sha256,
                  "Pinned bundle fingerprint differs from decoded container");
            std::filesystem::rename(archivePath,oldPath);write(archivePath,"replacement");
            // Rename changes ctime: the existing pinned metadata guard refuses,
            // rather than following the replacement path or updating the output.
            const auto unchanged=fingerprint;
            refuses([&]{loadInspectionBundle(pinned,memory,{}, {},&fingerprint);},ErrorCode::MediaMismatch);
            check(fingerprint.bytes==unchanged.bytes && fingerprint.sha256==unchanged.sha256,
                  "Refused pinned read changed success fingerprint");
            refuses([&]{ProjectStore(root).load();});std::filesystem::remove(archivePath);
            std::filesystem::rename(oldPath,archivePath);
#else
            std::error_code error;std::filesystem::rename(archivePath,oldPath,error);
            check(bool(error) && std::filesystem::exists(archivePath),"Windows pinned bundle allowed deletion/rename");
            auto loaded=loadInspectionBundle(pinned,memory,{}, {},&fingerprint);
            check(loaded.source()==report.source() && std::string(fingerprint.sha256.data(),64)==archive.inspection.sha256,
                  "Windows pinned bundle fingerprint differs");
#endif
            ResourceLedger foreign;
            refuses([&]{loadInspectionBundle(pinned,foreign);});
        }
        balanced(memory,stable);
        {
            const auto interrupted=Id::generate();std::stop_source canceled;
            InspectionBundleSaveOptions options;options.stop=canceled.get_token();
            options.beforePublish=[&]{canceled.request_stop();};
            refuses([&]{preserveProjectImportInspection(root,report,memory,interrupted,options);});
            check(std::filesystem::is_directory(root/"imports"/interrupted.str()) &&
                  !std::filesystem::exists(root/"imports"/interrupted.str()/"inspection.scinspect"),
                  "Interrupted archive publication was adopted/deleted");
            check(ProjectStore(root).load()==session,"Unreferenced residue changed saved Session");
        }
        balanced(memory,stable);
        const auto projectBefore=read(root/"project.json");
        for (unsigned mode=0;mode<4;++mode) {
            auto bad=session;
            if (mode==0) bad.imports[0].sourceSha256=std::string(64,'a');
            if (mode==1) bad.imports[0].sourceBytes++;
            if (mode==2) bad.imports[0].media[0].sourceProperty++;
            if (mode==3) bad.assets[0].sampleRate++;
            refuses([&]{ProjectStore(root).save(bad);});
            check(read(root/"project.json")==projectBefore,"Refused evidence save replaced current project");
            refuses([&]{openProjectImportEvidence(root,bad,archive.id,memory);});balanced(memory,stable);
        }
        auto tiny=memory.child(1,"Evidence refusal");refuses([&]{openProjectImportEvidence(root,session,archive.id,tiny);});
        balanced(memory,stable);
        ProjectBudget importWorkBudget;importWorkBudget.importEvidenceBytes=1;refuses([&]{ProjectStore(root,importWorkBudget).load();});
        const auto archivedPath=root/utf8Path(archive.inspection.relativePath);const auto archivedBytes=read(archivedPath);
        write(archivedPath,archivedBytes.substr(0,archivedBytes.size()-1));refuses([&]{ProjectStore(root).load();});
        write(archivedPath,archivedBytes);
        const auto receiptPath=root/utf8Path(archive.media.front().receipt.relativePath);
        auto badReceipt=nlohmann::json::parse(receipt);badReceipt["phase"]=1;
        write(receiptPath,badReceipt.dump());auto bad=session;bad.imports[0].media[0].receipt.bytes=std::filesystem::file_size(receiptPath);
        bad.imports[0].media[0].receipt.sha256=hashMediaFile(receiptPath);
        refuses([&]{ProjectStore(root).save(bad);});write(receiptPath,receipt);
        std::filesystem::remove(receiptPath);refuses([&]{ProjectStore(root).load();});write(receiptPath,receipt);
#ifndef _WIN32
        std::filesystem::rename(receiptPath,receiptPath.string()+".owned");
        std::filesystem::create_symlink(receiptPath.filename().string()+".owned",receiptPath);
        refuses([&]{ProjectStore(root).load();});std::filesystem::remove(receiptPath);
        std::filesystem::rename(receiptPath.string()+".owned",receiptPath);
#endif
        std::stop_source stop;stop.request_stop();refuses([&]{openProjectImportEvidence(root,session,archive.id,memory,{},stop.get_token());});
        // Move the entire owned project and remove original approval/source bytes.
        const auto relocated=input/utf8Path("Relocated — Łódź");std::filesystem::rename(root,relocated);
        check(read(input/utf8Path(reference))==original,"Original source modified");
        std::filesystem::remove(input/"inspection.scinspect");std::filesystem::remove(input/utf8Path(reference));
        check(read(relocated/utf8Path(asset.relativePath))==original,"Relocation changed owned samples");
        auto reopened=ProjectStore(relocated).load();check(reopened==session,"Relocation changed canonical state");
        auto evidence=openProjectImportEvidence(relocated,reopened,archive.id,memory);
        check(evidence.inspection().source()==report.source() && evidence.media().size()==1,"Reopen depends on original bundle");
        check(!std::filesystem::exists(input/utf8Path(reference)),"Original absence fixture failed");
        const auto usage=memory.usage();
        auto owned=std::make_shared<ProjectImportEvidence>(std::move(evidence));auto borrower=owned;owned.reset();
        check(borrower->ownedBy(memory) && memory.usage().reservedBytes==usage.reservedBytes,"Borrower lost evidence credit");
        borrower.reset();balanced(memory,stable);
    }
    check(memory.usage().reservedBytes==0 && memory.usage().owners==0,"Final import evidence ownership leaked");
    std::cout<<"PASS: "<<checks<<" import-state checks; actual inspector, immutable loss/provenance, save/reopen/relocation, refusals and ownership; conversion unqualified\n";
    return 0;
}
}
#ifdef _WIN32
int wmain(int argc,wchar_t **argv) {try {return argc==2 ? test(std::filesystem::path(argv[1])):2;}catch(const std::exception &e){std::cerr<<e.what()<<'\n';return 1;}}
#else
int main(int argc,char **argv) {try {return argc==2 ? test(std::filesystem::path(argv[1])):2;}catch(const std::exception &e){std::cerr<<e.what()<<'\n';return 1;}}
#endif
