// SPDX-License-Identifier: GPL-3.0-only
#include <soundcurrent/restoration_audit.hpp>
#include <soundcurrent/saved_publication_audit.hpp>
#include <fstream>
#include <iostream>
#include <map>
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif
using namespace soundcurrent::daw;
namespace {
unsigned checks=0;
void check(bool ok,const char *message) {++checks;if(!ok)throw std::runtime_error(message);}
template<class F> void refuses(F f,std::optional<ErrorCode> code={}) {
    bool refused=false;try {f();}catch(const ProjectError &e){refused=true;if(code)check(e.code()==*code,"Wrong refusal category");}
    check(refused,"Invalid audit admitted");
}
void write(const std::filesystem::path &p,std::string_view bytes) {
    std::filesystem::create_directories(p.parent_path());std::ofstream f(p,std::ios::binary);
    f.write(bytes.data(),static_cast<std::streamsize>(bytes.size()));check(bool(f),"Cannot write fixture");
}
std::string read(const std::filesystem::path &p) {std::ifstream f(p,std::ios::binary);check(bool(f),"Cannot read fixture");return {std::istreambuf_iterator<char>(f),{}};}
using Tree=std::map<std::filesystem::path,std::string>;
Tree tree(const std::filesystem::path &root) {
    Tree result;for(const auto &entry:std::filesystem::recursive_directory_iterator(root)) {
        check(!entry.is_symlink(),"Unexpected fixture link");
        result.emplace(entry.path().lexically_relative(root),entry.is_directory() ? "directory" : "file:"+read(entry.path()));
    }return result;
}
void put(std::string &s,unsigned value,unsigned count) {for(unsigned i=0;i<count;++i)s+=static_cast<char>((value>>(8*i))&255);}
std::string wave() {
    std::string s="RIFF";put(s,44,4);s+="WAVEfmt ";put(s,16,4);put(s,1,2);put(s,1,2);
    put(s,48000,4);put(s,96000,4);put(s,2,2);put(s,16,2);s+="data";put(s,8,4);
    for(unsigned x:{0,1,32767,32768})put(s,x,2);
    return s;
}
std::size_t handles() {
#ifdef _WIN32
    DWORD count=0;check(GetProcessHandleCount(GetCurrentProcess(),&count),"Cannot count handles");return count;
#else
    return static_cast<std::size_t>(std::distance(std::filesystem::directory_iterator("/proc/self/fd"),std::filesystem::directory_iterator{}));
#endif
}
void directoryLink(const std::filesystem::path &target,const std::filesystem::path &link) {
#ifdef _WIN32
    auto made=CreateSymbolicLinkW(link.c_str(),target.c_str(),SYMBOLIC_LINK_FLAG_DIRECTORY|SYMBOLIC_LINK_FLAG_ALLOW_UNPRIVILEGED_CREATE)!=0;
    if(!made)made=CreateSymbolicLinkW(link.c_str(),target.c_str(),SYMBOLIC_LINK_FLAG_DIRECTORY)!=0;
    check(made,"Native link fixture unavailable");
#else
    std::filesystem::create_directory_symlink(target,link);
#endif
}
void savedPublicationAudits(const std::filesystem::path &fixture) {
    const auto bytes=wave();const auto input=fixture/"saved-input";write(input/"source.wav",bytes);
    ResourceLedger memory(32*1024*1024,"Saved publication audit qualification");
    ProjectBudget budget;budget.encodedBytes=65536;budget.parserBytes=2*1024*1024;budget.state.memoryBudgetBytes=2*1024*1024;
    ApprovedMediaRoot sourceRoot(input,memory);auto source=sourceRoot.open("source.wav",1024);
    Asset asset;asset.relativePath="media/été-Κиїв/nested/take.wav";asset.frames=4;asset.layout={LayoutKind::Mono,1};
    const auto hash=source.digest();asset.sha256.assign(hash.begin(),hash.end());
    const CheckedMediaBytes content{52,hash};
    auto model=makeOneTrackSession("Saved audit","Original");model.assets.push_back(asset);
    auto unrelated=asset;unrelated.id=Id::generate();unrelated.relativePath="media/absent.wav";model.assets.push_back(unrelated);
    const SavedRestorationRecordContext context{{0,0,7,9},9,11,13,17};
    const auto root=[&] {const auto p=fixture/Id::generate().str();write(p/"project.json",encodeProject(model,budget));return p;};
    const auto record=[&](const std::filesystem::path &p,const Id &op,SavedRestorationRecordContext owner,
        SavedRestorationPhase phase,std::string_view name) {
        SavedRestorationRecordData d{op,model.id,asset.id};d.context=owner;d.relativePath=asset.relativePath;
        d.content=content;d.frames=asset.frames;d.rate=asset.sampleRate;d.layout=asset.layout;
        auto admitted=SavedRestorationRecord::planned(d,memory);auto encoded=encodeSavedRestorationRecord(admitted,phase,memory);
        write(p/op.str()/utf8Path(name),encoded.bytes());
    };
    const auto audit=[&](const std::filesystem::path &p,const Id &op) {
        return inspectSavedPublication(p,model.id,asset,content,context,op,1024,memory,budget);
    };
    // Warm the strict decoder/native hashing paths before lifetime measurements.
    {const auto p=root();const auto op=Id::generate();record(p,op,context,SavedRestorationPhase::Planned,"intent.json");auto a=audit(p,op);a.reverify();}
    const auto baseline=memory.usage();const auto nativeBaseline=handles();
    for(unsigned state=0;state<6;++state) {
        const auto p=root();const auto op=Id::generate();record(p,op,context,SavedRestorationPhase::Planned,"intent.json");
        if(state==1 || state==3 || state==5)write(p/op.str()/"media.partial",bytes);
        if(state==2 || state==3 || state==4)write(p/utf8Path(asset.relativePath),bytes);
        if(state>=2)record(p,op,context,SavedRestorationPhase::Published,state==2 ? "receipt.partial" : "receipt.json");
        const auto before=tree(p);
        {
            auto a=audit(p,op);
            check(a.manifest()==model,"Saved audit passive manifest changed");
            check(a.intent().context==context && a.intent().context.source.projectEpoch==0,"Saved zero epoch/context lost");
            check(a.canonical()==((state==2 || state==3 || state==4) ? RestorationMediaObservation::Verified : RestorationMediaObservation::Missing),"Saved canonical observation");
            check(a.staged()==((state==1 || state==3 || state==5) ? RestorationMediaObservation::Verified : RestorationMediaObservation::Missing),"Saved stage observation");
            check(a.receipt()==(state<2 ? RestorationReceiptObservation::Missing : state==2 ? RestorationReceiptObservation::Partial : RestorationReceiptObservation::Final),"Saved receipt inferred media");
            check(a.ownedBy(memory) && !a.ownedBy(ResourceLedger{}),"Saved audit credit scope");
            auto moved=std::move(a);refuses([&]{a.manifest();});moved.reverify();++checks;
            std::stop_source stop;stop.request_stop();refuses([&]{moved.reverify(stop.get_token());},ErrorCode::Canceled);
        }
        check(tree(p)==before,"Saved audit wrote disk evidence");
        refuses([&]{ProjectStore(p,budget).load();},ErrorCode::MissingMedia);
        // Strict accepted-model decoding must never silently accept saved records.
        refuses([&]{inspectSavedAssetRestoration(p,model.id,asset.id,op,1024,memory,budget);});
    }
    const auto p=root();const auto op=Id::generate();record(p,op,context,SavedRestorationPhase::Planned,"intent.json");
    write(p/utf8Path(asset.relativePath),bytes);record(p,op,context,SavedRestorationPhase::Published,"receipt.json");
    for(unsigned field=0;field<8;++field) {
        auto wrong=context;
        switch(field) {case 0:++wrong.source.projectEpoch;break;case 1:++wrong.source.modelRevision;break;
            case 2:++wrong.source.request;break;case 3:++wrong.source.job;break;case 4:++wrong.preparationJob;break;
            case 5:++wrong.request;break;case 6:++wrong.job;break;default:++wrong.workerPid;}
        refuses([&]{inspectSavedPublication(p,model.id,asset,content,wrong,op,1024,memory,budget);});
        record(p,op,wrong,SavedRestorationPhase::Published,"receipt.json");refuses([&]{audit(p,op);});
    }
    record(p,op,context,SavedRestorationPhase::Published,"receipt.json");
    auto wrong=asset;wrong.frames=3;refuses([&]{inspectSavedPublication(p,model.id,wrong,content,context,op,1024,memory,budget);});
    wrong=asset;wrong.nativeEvidence.push_back({"media/evidence.json",asset.sha256,52});
    refuses([&]{inspectSavedPublication(p,model.id,wrong,content,context,op,1024,memory,budget);});
    auto changed=content;++changed.bytes;refuses([&]{inspectSavedPublication(p,model.id,asset,changed,context,op,1024,memory,budget);});
    changed=content;changed.sha256[0]=changed.sha256[0]=='a' ? 'b' : 'a';
    refuses([&]{inspectSavedPublication(p,model.id,asset,changed,context,op,1024,memory,budget);});
    refuses([&]{inspectSavedPublication(p,Id::generate(),asset,content,context,op,1024,memory,budget);});
    refuses([&]{audit(p,Id::generate());},ErrorCode::MissingMedia);
    refuses([&]{inspectSavedPublication(p,model.id,asset,content,context,op,32,memory,budget);},ErrorCode::ResourceLimit);
    refuses([&]{inspectSavedPublication(p,model.id,asset,content,context,op,1024,ResourceLedger(64),budget);},ErrorCode::ResourceLimit);
    auto tiny=budget;tiny.encodedBytes=1;refuses([&]{inspectSavedPublication(p,model.id,asset,content,context,op,1024,memory,tiny);},ErrorCode::ResourceLimit);
    tiny=budget;tiny.parserBytes=memory.usage().limitBytes;
    refuses([&]{inspectSavedPublication(p,model.id,asset,content,context,op,1024,memory,tiny);},ErrorCode::ResourceLimit);
    std::stop_source stop;stop.request_stop();
    refuses([&]{inspectSavedPublication(p,model.id,asset,content,context,op,1024,memory,budget,stop.get_token());},ErrorCode::Canceled);
    record(p,op,context,SavedRestorationPhase::Published,"receipt.partial");refuses([&]{audit(p,op);});
    std::filesystem::remove(p/op.str()/"receipt.partial");
    const auto intent=read(p/op.str()/"intent.json");write(p/op.str()/"intent.json",read(p/op.str()/"receipt.json"));
    refuses([&]{audit(p,op);});write(p/op.str()/"intent.json",intent);
    auto corrupt=bytes;corrupt.back()^=1;write(p/utf8Path(asset.relativePath),corrupt);refuses([&]{audit(p,op);},ErrorCode::MediaMismatch);
    write(p/utf8Path(asset.relativePath),bytes);
    write(p/op.str()/"media.partial",corrupt);refuses([&]{audit(p,op);},ErrorCode::MediaMismatch);std::filesystem::remove(p/op.str()/"media.partial");
    {
        auto a=audit(p,op);write(p/op.str()/"media.partial",bytes);
        refuses([&]{a.reverify();},ErrorCode::MediaMismatch);
    }
    std::filesystem::remove(p/op.str()/"media.partial");
    {
        auto a=audit(p,op);
#ifdef _WIN32
        const auto h=CreateFileW((p/utf8Path(asset.relativePath)).c_str(),GENERIC_WRITE,FILE_SHARE_READ|FILE_SHARE_WRITE|FILE_SHARE_DELETE,nullptr,OPEN_EXISTING,0,nullptr);
        check(h==INVALID_HANDLE_VALUE,"Pinned saved canonical allowed Windows mutation");if(h!=INVALID_HANDLE_VALUE)CloseHandle(h);
        a.reverify();++checks;
#else
        write(p/utf8Path(asset.relativePath),corrupt);refuses([&]{a.reverify();},ErrorCode::MediaMismatch);
#endif
    }
    write(p/utf8Path(asset.relativePath),bytes);
    const auto linked=fixture/"saved-linked-root";directoryLink(p,linked);refuses([&]{audit(linked,op);});std::filesystem::remove(linked);
    const auto linkRoot=root();directoryLink(p/op.str(),linkRoot/op.str());refuses([&]{audit(linkRoot,op);});std::filesystem::remove(linkRoot/op.str());
    // Same bytes at a replacement root cannot hide loss of the original root.
    {
        auto a=audit(p,op);const auto renamed=fixture/Id::generate().str();std::error_code error;
        std::filesystem::rename(p,renamed,error);
        if(error) {
#ifdef _WIN32
            check(error.value()==ERROR_ACCESS_DENIED || error.value()==ERROR_SHARING_VIOLATION || error.value()==ERROR_LOCK_VIOLATION,"Unexpected Windows root rename error");
            a.reverify();++checks;
#else
            check(false,"Linux root rename fixture failed");
#endif
        } else {
            std::filesystem::copy(renamed,p,std::filesystem::copy_options::recursive);
            refuses([&]{a.reverify();},ErrorCode::MediaMismatch);
        }
    }
    const auto noManifest=root();const auto noOp=Id::generate();record(noManifest,noOp,context,SavedRestorationPhase::Planned,"intent.json");
    std::filesystem::remove(noManifest/"project.json");const auto before=tree(noManifest);
    refuses([&]{audit(noManifest,noOp);},ErrorCode::MissingMedia);check(tree(noManifest)==before,"Saved audit synthesized manifest");
    check(memory.usage().reservedBytes==baseline.reservedBytes && memory.usage().owners==baseline.owners && handles()==nativeBaseline,"Saved audit native/credit leak");
}
}
int main() {
    const auto fixture=std::filesystem::temp_directory_path()/utf8Path("sc-restore-Κиїв-")/Id::generate().str();
    try {
        savedPublicationAudits(fixture);
        const auto bytes=wave();check(bytes.size()==52,"Independent WAVE extent");write(fixture/"input/source.wav",bytes);
        ResourceLedger memory(32*1024*1024,"Restoration audit qualification");
        ProjectBudget budget;budget.encodedBytes=65536;budget.parserBytes=2*1024*1024;budget.state.memoryBudgetBytes=2*1024*1024;
        for(const bool savedManifest:{false,true}) {
            ApprovedMediaRoot sourceRoot(fixture/"input",memory);auto source=sourceRoot.open("source.wav",1024);
            Asset asset;asset.relativePath="media/été-Κиїв/nested/take.wav";asset.frames=4;asset.layout={LayoutKind::Mono,1};
            const auto sha=source.digest();asset.sha256.assign(sha.begin(),sha.end());
            auto model=makeOneTrackSession("Audit","Original");model.assets.push_back(asset);
            // An unrelated missing asset must not prevent passive manifest admission.
            auto unrelated=asset;unrelated.id=Id::generate();unrelated.relativePath="media/unrelated.wav";model.assets.push_back(unrelated);
            const AssetRestorationContext context{11,0,21};
            auto root=[&]{const auto p=fixture/Id::generate().str();std::filesystem::create_directory(p);
                write(p/"project.json",encodeProject(model,budget));return p;};
            auto audit=[&](const std::filesystem::path &p,const Id &operation){return savedManifest ? inspectSavedAssetRestoration(p,model.id,asset.id,operation,1024,memory,budget) :
                inspectAssetRestoration(p,model.id,asset,operation,1024,memory,budget);};
            // Warm native/hash/decoder runtime before lifetime accounting.
            {
                const auto p=root();const auto op=Id::generate();
                {
                    auto prepared=PreparedAssetRestoration::prepare(source,p,asset,context,1024,{},op);
#ifdef _WIN32
                    // The live writer deliberately denies this read pin. Audit
                    // interrupted state only after its original owner retires.
                    refuses([&]{audit(p,op);},ErrorCode::Io);
#endif
                }
                auto report=audit(p,op);report.reverify();++checks;
            }
            const auto baseline=memory.usage();const auto nativeBaseline=handles();
            const auto project=root();Id operation=Id::generate();
            {
                auto prepared=PreparedAssetRestoration::prepare(source,project,asset,context,1024,{},operation);
                check(prepared.publish(asset,context).receiptPublished,"Complete publication fixture");
            }
            const auto before=tree(project);
            {
                auto report=audit(project,operation);
                check(report.manifest()==model && report.manifestBytes()==read(project/"project.json"),"Passive manifest identity/bytes");
                check(report.canonical()==RestorationMediaObservation::Verified && report.staged()==RestorationMediaObservation::Missing &&
                    report.receipt()==RestorationReceiptObservation::Final,"Complete native observations");
                check(report.intent().context==context && report.intent().content.bytes==52,"Historical evidence lost");
                check(report.ownedBy(memory) && !report.ownedBy(ResourceLedger{}),"Audit resource scope");
                auto moved=std::move(report);refuses([&]{report.manifest();});moved.reverify();++checks;
                std::stop_source stop;stop.request_stop();refuses([&]{moved.reverify(stop.get_token());},ErrorCode::Canceled);
            }
            check(tree(project)==before,"Audit wrote project/records/media");
            refuses([&]{ProjectStore(project,budget).load();},ErrorCode::MissingMedia);
            check(memory.usage().reservedBytes==baseline.reservedBytes && memory.usage().owners==baseline.owners && handles()==nativeBaseline,"Audit owner leaked");
            for(unsigned state=0;state<3;++state) {
                const auto p=root();const auto op=Id::generate();
                {
                    auto prepared=PreparedAssetRestoration::prepare(source,p,asset,context,1024,{},op);
                    if(state) {
                        bool reached=false;
                        try {prepared.publish(asset,context,{},[&](StageBoundary at,std::uint64_t){
                            if(at==(state==1 ? StageBoundary::BeforeCommit : StageBoundary::BeforeRestorationReceiptPublish)) {
                                reached=true;throw ProjectError(ErrorCode::Canceled,"Qualification interruption");
                            }
                        });}catch(const ProjectError &) {}
                        check(reached,"Interruption boundary not reached");
                    }
                }
                const auto old=tree(p);auto report=audit(p,op);
                check(report.canonical()==(state==2 ? RestorationMediaObservation::Verified : RestorationMediaObservation::Missing),"Canonical interruption observation");
                check(report.staged()==(state==2 ? RestorationMediaObservation::Missing : RestorationMediaObservation::Verified),"Staged interruption observation");
                check(report.receipt()==(state ? RestorationReceiptObservation::Partial : RestorationReceiptObservation::Missing),"Prospective receipt observation");
                report.reverify();check(tree(p)==old,"Interrupted audit modified physical state");
            }
            // A final receipt never substitutes for currently observed media.
            std::filesystem::remove(project/utf8Path(asset.relativePath));
            {auto report=audit(project,operation);check(report.receipt()==RestorationReceiptObservation::Final && report.canonical()==RestorationMediaObservation::Missing,"Receipt inferred publication");}
            write(project/utf8Path(asset.relativePath),"mismatch");refuses([&]{audit(project,operation);},ErrorCode::MediaMismatch);
            auto corrupt=bytes;corrupt.back()^=1;write(project/utf8Path(asset.relativePath),corrupt);
            refuses([&]{audit(project,operation);},ErrorCode::MediaMismatch);
            write(project/utf8Path(asset.relativePath),bytes);
            auto wrong=asset;wrong.frames=3;refuses([&]{inspectAssetRestoration(project,model.id,wrong,operation,1024,memory,budget);});
            wrong=asset;wrong.nativeEvidence.push_back({"media/evidence.json",asset.sha256,52});
            refuses([&]{inspectAssetRestoration(project,model.id,wrong,operation,1024,memory,budget);});
            refuses([&]{inspectAssetRestoration(project,Id::generate(),asset,operation,1024,memory,budget);});
            refuses([&]{audit(project,Id::generate());},ErrorCode::MissingMedia);
            refuses([&]{inspectAssetRestoration(project,model.id,asset,operation,32,memory,budget);},ErrorCode::ResourceLimit);
            refuses([&]{inspectAssetRestoration(project,model.id,asset,operation,1024,ResourceLedger(64),budget);},ErrorCode::ResourceLimit);
            std::stop_source stop;stop.request_stop();refuses([&]{inspectAssetRestoration(project,model.id,asset,operation,1024,memory,budget,stop.get_token());},ErrorCode::Canceled);
            // Pre-open selectors and budgets are trusted call inputs, never inferred from an intent.
            refuses([&]{inspectSavedAssetRestoration(project,Id::generate(),asset.id,operation,1024,memory,budget);});
            refuses([&]{inspectSavedAssetRestoration(project,model.id,Id::generate(),operation,1024,memory,budget);});
            refuses([&]{inspectSavedAssetRestoration(project,model.id,unrelated.id,operation,1024,memory,budget);});
            refuses([&]{inspectSavedAssetRestoration(project,model.id,asset.id,operation,32,memory,budget);},ErrorCode::ResourceLimit);
            refuses([&]{inspectSavedAssetRestoration(project,model.id,asset.id,operation,1024,ResourceLedger(64),budget);},ErrorCode::ResourceLimit);
            refuses([&]{inspectSavedAssetRestoration(project,model.id,asset.id,operation,1024,memory,budget,stop.get_token());},ErrorCode::Canceled);
            auto tiny=budget;tiny.encodedBytes=1;
            refuses([&]{inspectSavedAssetRestoration(project,model.id,asset.id,operation,1024,memory,tiny);},ErrorCode::ResourceLimit);
            tiny=budget;tiny.parserBytes=memory.usage().limitBytes;
            refuses([&]{inspectSavedAssetRestoration(project,model.id,asset.id,operation,1024,memory,tiny);},ErrorCode::ResourceLimit);
            const auto noManifest=root();const auto noManifestOp=Id::generate();
            {auto prepared=PreparedAssetRestoration::prepare(source,noManifest,asset,context,1024,{},noManifestOp);}
            check(std::filesystem::remove(noManifest/"project.json"),"Missing-manifest fixture removal failed");
            const auto journalOnly=tree(noManifest);
            refuses([&]{inspectSavedAssetRestoration(noManifest,model.id,asset.id,noManifestOp,1024,memory,budget);},ErrorCode::MissingMedia);
            check(tree(noManifest)==journalOnly,"Journal-only inspection synthesized a manifest");
            const auto inconsistent=root();const auto inconsistentOp=Id::generate();
            {auto prepared=PreparedAssetRestoration::prepare(source,inconsistent,asset,context,1024,{},inconsistentOp);}
            auto changedModel=model;changedModel.assets.front().frames=3;
            write(inconsistent/"project.json",encodeProject(changedModel,budget));const auto inconsistentBefore=tree(inconsistent);
            refuses([&]{inspectSavedAssetRestoration(inconsistent,model.id,asset.id,inconsistentOp,1024,memory,budget);});
            check(tree(inconsistent)==inconsistentBefore,"Saved descriptor mismatch changed physical evidence");
            const auto stagedBad=root();const auto stagedOp=Id::generate();
            {auto prepared=PreparedAssetRestoration::prepare(source,stagedBad,asset,context,1024,{},stagedOp);}
            auto badStage=bytes;badStage.back()^=1;write(stagedBad/stagedOp.str()/"media.partial",badStage);
            const auto badStageBefore=tree(stagedBad);refuses([&]{audit(stagedBad,stagedOp);},ErrorCode::MediaMismatch);
            check(tree(stagedBad)==badStageBefore,"Corrupt staged bytes changed during audit");
            const auto receiptPath=project/operation.str()/"receipt.json";const auto receiptBytes=read(receiptPath);
            auto replace=[](std::string text,std::string_view from,std::string_view to) {
                const auto at=text.find(from);check(at!=std::string::npos,"Mutation target missing");text.replace(at,from.size(),to);return text;
            };
            for(const auto &bad:{std::string("{}"),std::string(8193,'x'),replace(receiptBytes,"\"request\":21","\"request\":22"),
                replace(receiptBytes,"\"sourceRootsPersisted\":false","\"sourceRootsPersisted\":true"),replace(receiptBytes,"\"phase\":2","\"phase\":1"),
                replace(receiptBytes,"\"sourceBytes\":52","\"sourceBytes\":53")}) {
                write(receiptPath,bad);const auto old=tree(project);refuses([&]{audit(project,operation);});check(tree(project)==old,"Refusal modified evidence");
            }
            write(receiptPath,receiptBytes);write(project/operation.str()/"receipt.partial",receiptBytes);refuses([&]{audit(project,operation);});
            std::filesystem::remove(project/operation.str()/"receipt.partial");
            const auto intentPath=project/operation.str()/"intent.json";const auto intentBytes=read(intentPath);
            write(intentPath,receiptBytes);refuses([&]{audit(project,operation);});std::filesystem::remove(intentPath);
            refuses([&]{audit(project,operation);},ErrorCode::MissingMedia);write(intentPath,intentBytes);
            const auto manifestBytes=read(project/"project.json");write(project/"project.json","{}");refuses([&]{audit(project,operation);});write(project/"project.json",manifestBytes);
            // Existing operation directory and root links must not be traversed.
            const auto linkRoot=root();directoryLink(project/operation.str(),linkRoot/operation.str());
            refuses([&]{audit(linkRoot,operation);});std::filesystem::remove(linkRoot/operation.str());
            directoryLink(project,fixture/"linked-root");refuses([&]{audit(fixture/"linked-root",operation);});std::filesystem::remove(fixture/"linked-root");
            const auto visibility=root();const auto op=Id::generate();
            {auto prepared=PreparedAssetRestoration::prepare(source,visibility,asset,context,1024,{},op);}
            {
                auto report=audit(visibility,op);write(visibility/utf8Path(asset.relativePath),bytes);
                refuses([&]{report.reverify();},ErrorCode::MediaMismatch);
            }
            {
                auto report=audit(project,operation);
#ifdef _WIN32
                const auto h=CreateFileW((project/utf8Path(asset.relativePath)).c_str(),GENERIC_WRITE,FILE_SHARE_READ|FILE_SHARE_WRITE|FILE_SHARE_DELETE,nullptr,OPEN_EXISTING,0,nullptr);
                check(h==INVALID_HANDLE_VALUE,"Pinned Windows canonical allowed mutation");if(h!=INVALID_HANDLE_VALUE)CloseHandle(h);
                report.reverify();++checks;
#else
                auto changed=bytes;changed.back()^=1;write(project/utf8Path(asset.relativePath),changed);
                refuses([&]{report.reverify();},ErrorCode::MediaMismatch);
#endif
            }
            check(read(fixture/"input/source.wav")==bytes,"Audit changed/reopened source bytes");
            check(memory.usage().reservedBytes==baseline.reservedBytes && memory.usage().owners==baseline.owners && handles()==nativeBaseline,"Refusal/native audit leaked");
        }
        check(memory.usage().reservedBytes==0 && memory.usage().owners==0,"Final audit credit leak");
        std::filesystem::remove_all(fixture);std::cout<<checks<<" native read-only restoration audit checks passed (accepted-asset, saved-manifest and saved-publication selectors)\n";return 0;
    }catch(const std::exception &e) {
        std::cerr<<e.what()<<'\n';const auto path=fixture.generic_u8string();std::cerr<<"Retained restoration fixture ";
        std::cerr.write(reinterpret_cast<const char *>(path.data()),static_cast<std::streamsize>(path.size()));std::cerr<<'\n';return 1;
    }
}
