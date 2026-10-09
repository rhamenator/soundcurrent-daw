// SPDX-License-Identifier: GPL-3.0-only
#include <soundcurrent/inspection_bundle.hpp>
#include <soundcurrent/media_staging.hpp>
#include <soundcurrent/media_recovery.hpp>
#include <nlohmann/json.hpp>
#include <fstream>
#include <iostream>
#include <chrono>
#include <thread>
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif
#ifdef SC_TRANSACTION_IO_WRAP
#include <cerrno>
#include <sys/types.h>
#include <unistd.h>
namespace {enum class IoFault {None,ShortWrite,DiskFull,Flush};IoFault ioFault=IoFault::None;}
extern "C" ssize_t __real_pwrite(int,const void *,size_t,off_t);
extern "C" int __real_fsync(int);
extern "C" ssize_t __wrap_pwrite(int fd,const void *data,size_t count,off_t offset) {
    if (ioFault==IoFault::DiskFull) {errno=ENOSPC;return -1;}
    return __real_pwrite(fd,data,ioFault==IoFault::ShortWrite && count>7 ? 7:count,offset);
}
extern "C" int __wrap_fsync(int fd) {if (ioFault==IoFault::Flush) {errno=EIO;return -1;}return __real_fsync(fd);}
#endif
using namespace soundcurrent::daw;
namespace {
unsigned checks=0;
void check(bool good,const char *text) {++checks;if (!good) throw std::runtime_error(text);}
template<class F> void refuses(F fn,ErrorCode expected) {
    bool seen=false;try {fn();} catch(const ProjectError &e) {seen=true;check(e.code()==expected,"Unexpected media transaction refusal");}
    check(seen,"Expected media transaction refusal missing");
}
std::string read(const std::filesystem::path &p) {std::ifstream in(p,std::ios::binary);check(bool(in),"Owned read failed");return {std::istreambuf_iterator<char>(in),{}};}
void write(const std::filesystem::path &p,std::string_view bytes) {std::ofstream out(p,std::ios::binary);out.write(bytes.data(),static_cast<std::streamsize>(bytes.size()));out.close();check(bool(out),"Owned write failed");}
void balanced(const ResourceLedger &ledger,ResourceUsage before) {const auto after=ledger.usage();check(after.owners==before.owners && after.reservedBytes==before.reservedBytes,"Transaction credit leaked");}
std::size_t occurrence(const ImportInspectionReport &report) {
    for (std::size_t i=0;i<report.properties().size();++i) if (report.properties()[i].id==ImportPropertyId::SourceFile) return i;
    throw std::runtime_error("Actual inspector has no WAVE occurrence");
}
void planned(const std::filesystem::path &output,const Id &id,const ResourceLedger &ledger) {
    const auto before=ledger.usage();{auto recovery=recoverStagedMedia(output,id,ledger,1024*1024);check(recovery.phase==MediaRecoveryPhase::Planned && recovery.provenance,"Interrupted stage was adopted or lost intent");}
    balanced(ledger,before);check(!std::filesystem::exists(output/id.str()/"receipt.json"),"Failure published verified receipt");
}
}
int test(const std::filesystem::path &input) {
    const auto output=input/"unit-output";std::filesystem::create_directory(output);
    ResourceLedger ledger(128*1024*1024,"Transaction unit payload");
    try {
        {
            auto inspection=loadInspectionBundle(input/"inspection.scinspect",ledger);const auto ordinal=occurrence(inspection);
            ApprovedMediaRoot sourceRoot(input,ledger);auto source=sourceRoot.open("audio.wav",1024*1024);const auto audio=validateApprovedWave(source);
            const auto raw=read(input/"audio.wav");const auto baseline=ledger.usage();
            auto make=[&](Id id,MediaSelectionKind kind=MediaSelectionKind::ApprovedReference) {
                return bindMediaProvenance(inspection,ordinal,audio,"audio.wav",kind,std::move(id),ledger);
            };
            {
                auto origin=make(Id::generate());const auto &p=origin.data();
                check(p.originalReference=="audio.wav" && p.selectedReference=="audio.wav" && p.sourceProperty==ordinal,"Occurrence/original token was not bound");
                check(std::string(p.inspectionSha256.data(),64)==inspection.sha256() && p.audio.peak==2.5,"Original hash or float headroom changed");
                auto encoded=encodeMediaProvenance(origin,MediaReceiptPhase::Planned,ledger);
                {auto decoded=decodeMediaProvenance(encoded.bytes(),ledger);auto again=encodeMediaProvenance(decoded,MediaReceiptPhase::Planned,ledger);check(encoded.bytes()==again.bytes(),"Versioned receipt roundtrip changed bytes");}
                using J=nlohmann::json;const auto valid=J::parse(encoded.bytes());
                for (unsigned mutation=0;mutation<18;++mutation) {
                    auto bad=valid;
                    switch (mutation) {
                    case 0:bad["unknown"]=1;break;case 1:bad.erase("sourceBytes");break;case 2:bad["phase"]=3;break;
                    case 3:bad["assetLeaf"]="../outside.wav";break;case 4:bad["sourceRootsPersisted"]=true;break;
                    case 5:bad["frames"]=-1;break;case 6:bad["frames"]=1.0;break;case 7:bad["channels"]=0;break;
                    case 8:bad["channels"]=std::uint64_t(UINT32_MAX)+1;break;case 9:bad["bitsPerSample"]=16;break;
                    case 10:bad["sourceSha256"]="x";break;case 11:bad["stagedSha256"]=std::string(64,'0');break;
                    case 12:bad["originalReferenceHex"]="GG";break;case 13:bad["selectedReferenceHex"]="00";break;
                    case 14:bad["referenceBegin"]=UINT64_MAX;break;case 15:bad["sourceObject"]=100000;break;
                    case 16:bad["phase"]=J::array({1});break;case 17:bad["phase"]={{"nested",1}};break;
                    }
                    const auto before=ledger.usage();refuses([&]{decodeMediaProvenance(bad.dump(),ledger);},ErrorCode::InvalidState);balanced(ledger,before);
                }
                auto duplicate=std::string(encoded.bytes());duplicate.insert(1,"\"phase\":1,");
                refuses([&]{decodeMediaProvenance(duplicate,ledger);},ErrorCode::InvalidState);
                refuses([&]{decodeMediaProvenance(std::string(mediaReceiptMaximumBytes+1,'x'),ledger);},ErrorCode::InvalidState);
                ResourceLedger other(1024*1024,"Unrelated scope");refuses([&]{encodeMediaProvenance(origin,MediaReceiptPhase::Planned,other);},ErrorCode::InvalidState);
                refuses([&]{bindMediaProvenance(inspection,0,audio,"audio.wav",MediaSelectionKind::ApprovedReference,Id::generate(),ledger);},ErrorCode::InvalidState);
                refuses([&]{bindMediaProvenance(inspection,ordinal,audio,"different.wav",MediaSelectionKind::ApprovedReference,Id::generate(),ledger);},ErrorCode::InvalidState);
                refuses([&]{decodeMediaProvenance("[]",ledger);},ErrorCode::InvalidState);
            }
            balanced(ledger,baseline);
            for (const auto at : {StageBoundary::BeforeIntentWrite,StageBoundary::BeforeIntentFlush,StageBoundary::BeforeFile,
                     StageBoundary::BeforeWrite,StageBoundary::BeforeFileFlush,StageBoundary::BeforeReadback,StageBoundary::BeforeFinalSourceCheck,StageBoundary::BeforeDirectoryFlush}) {
                const auto id=Id::generate();
                {auto origin=make(id);std::stop_source stop;
                    refuses([&]{stageBoundMedia(source,output,origin,1024*1024,stop.get_token(),
                        [&](StageBoundary point,std::uint64_t){if (point==at) stop.request_stop();});},ErrorCode::Canceled);
                }
                if (at==StageBoundary::BeforeIntentWrite) {auto r=recoverStagedMedia(output,id,ledger,1024*1024);check(r.phase==MediaRecoveryPhase::NoIntentOrReceipt,"Empty operation was marked owned/complete");}
                else planned(output,id,ledger);
                check(source.digest()==audio.sourceSha256,"Canceled intent/copy modified source");balanced(ledger,baseline);
            }
            for (const auto at : {StageBoundary::BeforeAssetRename,StageBoundary::BeforeReceiptWrite,StageBoundary::BeforeReceiptFlush,StageBoundary::BeforeCommit}) {
                const auto id=Id::generate();
                {auto origin=make(id);auto stage=stageBoundMedia(source,output,origin,1024*1024);std::stop_source stop;
                    refuses([&]{commitStagedMedia(stage,origin,stop.get_token(),[&](StageBoundary point,std::uint64_t){if (point==at) stop.request_stop();});},ErrorCode::Canceled);
                    refuses([&]{commitStagedMedia(stage,origin);},ErrorCode::InvalidState);
                }
                planned(output,id,ledger);check(source.digest()==audio.sourceSha256,"Canceled commit modified source");balanced(ledger,baseline);
            }
#ifdef SC_TRANSACTION_IO_WRAP
            constexpr unsigned postCases=3;
#else
            constexpr unsigned postCases=2;
#endif
            for (unsigned post=0;post<postCases;++post) {
                const auto id=Id::generate();
                {auto origin=make(id);auto stage=stageBoundMedia(source,output,origin,1024*1024);std::stop_source stop;
                    const auto outcome=commitStagedMedia(stage,origin,stop.get_token(),[&](StageBoundary point,std::uint64_t){
                        if (point==StageBoundary::AfterCommitBeforeDirectoryFlush) {
                            stop.request_stop();
#ifdef SC_TRANSACTION_IO_WRAP
                            if (post==2) ioFault=IoFault::Flush;
#endif
                            if (post==1) throw ProjectError(ErrorCode::Io,"Injected post-publication flush failure");
                        }
                    });
#ifdef SC_TRANSACTION_IO_WRAP
                    ioFault=IoFault::None;
#endif
                    check(outcome.published && outcome.postCommitFlushFailed==bool(post),"Post-commit cancellation/fault was misreported as unpublished");
                    if (post) check(outcome.durability==StageDurability::FileFlushed && stage.durability()==outcome.durability,"Post-commit directory durability overstated");
                    check(stage.relativePath()==id.str()+"/media.wav","Committed asset name wrong");
                    refuses([&]{commitStagedMedia(stage,origin);},ErrorCode::InvalidState);
                }
                {auto recovery=recoverStagedMedia(output,id,ledger,1024*1024);check(recovery.phase==MediaRecoveryPhase::Verified && recovery.provenance->data().originalReference=="audio.wav","Visible commit was lost during recovery");}
                check(read(output/id.str()/"media.wav")==raw,"Committed copy differs independently");balanced(ledger,baseline);
            }
            { // Admission before commit is retryable; no namespace mutation yet.
                auto origin=make(Id::generate());auto stage=stageBoundMedia(source,output,origin,1024*1024);
                const auto before=ledger.usage();ledger.configure(before.reservedBytes+1);
                refuses([&]{commitStagedMedia(stage,origin);},ErrorCode::ResourceLimit);ledger.configure(128*1024*1024);balanced(ledger,before);
                check(stage.relativePath().ends_with("media.partial"),"Unadmitted commit renamed media");
                check(commitStagedMedia(stage,origin).published,"Admitted commit retry failed");
            }
            balanced(ledger,baseline);
            for (unsigned collision=0;collision<2;++collision) {
                const auto id=Id::generate();
                {auto origin=make(id);auto stage=stageBoundMedia(source,output,origin,1024*1024);
                    const auto target=output/id.str()/(collision ? "receipt.json":"media.wav");
                    refuses([&]{commitStagedMedia(stage,origin,{},[&](StageBoundary point,std::uint64_t){
                        if (point==(collision ? StageBoundary::BeforeCommit:StageBoundary::BeforeAssetRename)) write(target,"preserve");
                    });},ErrorCode::Io);
                }
                check(read(output/id.str()/(collision ? "receipt.json":"media.wav"))=="preserve","Exclusive commit overwrote collision");balanced(ledger,baseline);
            }
            { // Same UUID/hash but changed policy cannot alter the planned intent.
                const auto id=Id::generate();auto original=make(id);auto replacement=make(id,MediaSelectionKind::ExplicitReplacement);
                auto stage=stageBoundMedia(source,output,original,1024*1024);
                refuses([&]{commitStagedMedia(stage,replacement);},ErrorCode::MediaMismatch);
                check(commitStagedMedia(stage,original).published,"Original intent retry refused");
            }
            balanced(ledger,baseline);
            const auto id=Id::generate();
            {auto origin=make(id);auto stage=stageBoundMedia(source,output,origin,1024*1024);check(commitStagedMedia(stage,origin).published,"Corruption fixture commit failed");}
            const auto committed=output/id.str();const auto originalReceipt=read(committed/"receipt.json"),originalIntent=read(committed/"intent.json");
            write(committed/"receipt.json","{");refuses([&]{recoverStagedMedia(output,id,ledger,1024*1024);},ErrorCode::InvalidState);
            write(committed/"receipt.json",originalReceipt);write(committed/"media.wav",raw.substr(0,raw.size()-1));
            refuses([&]{recoverStagedMedia(output,id,ledger,1024*1024);},ErrorCode::MediaMismatch);write(committed/"media.wav",raw);
            auto changed=nlohmann::json::parse(originalReceipt);changed["peakLinear"]=1.5;write(committed/"receipt.json",changed.dump());
            refuses([&]{recoverStagedMedia(output,id,ledger,1024*1024);},ErrorCode::MediaMismatch);
            changed["phase"]=1;write(committed/"intent.json",changed.dump()); // Two matching lies still fail full decoder validation.
            refuses([&]{recoverStagedMedia(output,id,ledger,1024*1024);},ErrorCode::MediaMismatch);
            write(committed/"intent.json",originalIntent);write(committed/"receipt.json",originalReceipt);
            std::filesystem::remove(committed/"intent.json");refuses([&]{recoverStagedMedia(output,id,ledger,1024*1024);},ErrorCode::InvalidState);
            write(committed/"intent.json",originalIntent);std::filesystem::remove(committed/"media.wav");
            refuses([&]{recoverStagedMedia(output,id,ledger,1024*1024);},ErrorCode::MissingMedia);balanced(ledger,baseline);
#ifdef SC_TRANSACTION_IO_WRAP
            for (const auto fault : {IoFault::ShortWrite,IoFault::DiskFull,IoFault::Flush}) {
                const auto operation=Id::generate();
                {auto origin=make(operation);auto stage=stageBoundMedia(source,output,origin,1024*1024);ioFault=fault;
                    if (fault==IoFault::ShortWrite) check(commitStagedMedia(stage,origin).published,"Native partial receipt writes lost bytes");
                    else refuses([&]{commitStagedMedia(stage,origin);},ErrorCode::Io);
                    ioFault=IoFault::None;
                }
                if (fault==IoFault::ShortWrite) {auto r=recoverStagedMedia(output,operation,ledger,1024*1024);check(r.phase==MediaRecoveryPhase::Verified,"Partial native write receipt did not recover");}
                else planned(output,operation,ledger);
                balanced(ledger,baseline);
            }
#endif
            check(source.digest()==audio.sourceSha256 && read(input/"audio.wav")==raw,"Transaction changed original source bytes");
        }
        check(ledger.usage().owners==0 && ledger.usage().reservedBytes==0,"Retired transaction/report/recovery kept credit");
        std::filesystem::remove_all(output);std::cout<<"Passed "<<checks<<" media transaction checks\n";return 0;
    } catch (const std::exception &e) {
#ifdef SC_TRANSACTION_IO_WRAP
        ioFault=IoFault::None;
#endif
        std::cerr<<"Transaction failed after "<<checks<<" checks: "<<e.what()<<'\n';std::error_code error;std::filesystem::remove_all(output,error);return 1;
    }
}
int interruptionProbe(const std::filesystem::path &input,const Id &operation) {
    try {
        ResourceLedger ledger(128*1024*1024,"Abrupt interruption fixture");
        auto inspection=loadInspectionBundle(input/"inspection.scinspect",ledger);
        ApprovedMediaRoot root(input,ledger);auto source=root.open("audio.wav",1024*1024);
        const auto audio=validateApprovedWave(source);
        auto origin=bindMediaProvenance(inspection,occurrence(inspection),audio,"audio.wav",
            MediaSelectionKind::ApprovedReference,operation,ledger);
        auto stage=stageBoundMedia(source,input/"destination",origin,1024*1024);
        commitStagedMedia(stage,origin,{},[](StageBoundary at,std::uint64_t){
            if (at==StageBoundary::BeforeCommit) {
                std::cout<<"ready-before-commit\n"<<std::flush;
                // Deliberate test-only parked process. The parent observes this
                // exact live handle, terminates it and verifies terminal exit.
                for (;;) std::this_thread::sleep_for(std::chrono::milliseconds(10));
            }
        });return 1;
    } catch(const std::exception &e) {std::cerr<<"Interruption fixture failed: "<<e.what()<<'\n';return 1;}
}
#ifdef _WIN32
int wmain(int argc,wchar_t **argv) {
    if (argc==2) return test(std::filesystem::path(argv[1]));
    if (argc==4 && std::wstring_view(argv[2])==L"--interrupt") {
        std::string id;for (wchar_t c:std::wstring_view(argv[3])) {if (c>127) return 2;id+=static_cast<char>(c);}
        return interruptionProbe(std::filesystem::path(argv[1]),Id(std::move(id)));
    }return 2;
}
#else
int main(int argc,char **argv) {
    if (argc==2) return test(std::filesystem::path(argv[1]));
    return argc==4 && std::string_view(argv[2])=="--interrupt" ? interruptionProbe(std::filesystem::path(argv[1]),Id(argv[3])):2;
}
#endif
