// SPDX-License-Identifier: GPL-3.0-only
#include <soundcurrent/asset_restoration.hpp>
#include <soundcurrent/restoration_record.hpp>
#include <soundcurrent/project_store.hpp>
#include <fstream>
#include <iostream>
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
template<class F> void refuses(F f) {bool refused=false;try {f();}catch(const ProjectError &){refused=true;}check(refused,"Invalid restoration admitted");}
void write(const std::filesystem::path &p,std::string_view bytes) {std::ofstream f(p,std::ios::binary);f.write(bytes.data(),static_cast<std::streamsize>(bytes.size()));check(bool(f),"Cannot write fixture");}
std::string read(const std::filesystem::path &p) {std::ifstream f(p,std::ios::binary);check(bool(f),"Cannot read fixture");return {std::istreambuf_iterator<char>(f),{}};}
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
}
int main() {
    const auto fixture=std::filesystem::temp_directory_path()/utf8Path("sc-restore-Κиїв-"+Id::generate().str());
    try {
        std::filesystem::create_directories(fixture/"input");const auto bytes=wave();write(fixture/"input/source.wav",bytes);
        ResourceLedger resources(8*1024*1024,"Restoration qualification");
        {
            ApprovedMediaRoot approved(fixture/"input",resources);auto source=approved.open("source.wav",1024);
            Asset asset;asset.relativePath="media/été-Κиїв/nested/take.wav";asset.frames=4;asset.layout={LayoutKind::Mono,1};
            const auto digest=source.digest();asset.sha256.assign(digest.begin(),digest.end());
            const AssetRestorationContext context{11,0,21};
            auto root=[&]{const auto p=fixture/Id::generate().str();std::filesystem::create_directory(p);return p;};
            const auto project=root();auto model=makeOneTrackSession("Restoration","Original");model.assets.push_back(asset);
            Clip clip;clip.assetId=asset.id;clip.lengthFrames=4;model.tracks[0].clips.push_back(clip);
            TakeLane lane;lane.name="Retained";lane.captureEndFrame=4;auto retained=clip;retained.id=Id::generate();lane.clips.push_back(retained);model.tracks[0].takeLanes.push_back(lane);
            EditHistory history(model);history.structural({RenameTrack{model.tracks[0].id,"Edited"}});const auto edited=model;
            std::filesystem::create_directories((project/utf8Path(asset.relativePath)).parent_path());
            write(project/utf8Path(asset.relativePath),bytes);
            ProjectStore(project).save(model);const auto manifest=read(project/"project.json");
            std::filesystem::remove_all(project/"media"/utf8Path("été-Κиїв")); // Simulate loss after a valid save.

            // Warm native/hash runtime before strict lifetime accounting.
            {auto warm=PreparedAssetRestoration::prepare(source,root(),asset,context,1024);}
            const auto baseline=resources.usage().reservedBytes;const auto owners=resources.usage().owners;const auto oldHandles=handles();
            std::string successfulOperation;
            {
                auto prepared=PreparedAssetRestoration::prepare(source,project,asset,context,1024);
                successfulOperation=prepared.operation().str();
                verifyAssetRestorationRecord(prepared.record(),asset,context,prepared.operation(),AssetRestorationPhase::Planned,resources);++checks;
                check(prepared.chargedBytes()>0 && !std::filesystem::exists(project/utf8Path(asset.relativePath)),"Prepare published an asset");
                auto moved=std::move(prepared);refuses([&]{prepared.operation();});
                const auto result=moved.publish(asset,context);check(result.published && result.receiptPublished && !result.postCommitFlushFailed,"Restoration/receipt not published");
                refuses([&]{moved.publish(asset,context);});
#ifdef _WIN32
                check(result.durability==StageDurability::FileFlushed,"Windows directory durability overstated");
#else
                check(result.durability==StageDurability::FileAndDirectoriesFlushed,"Restoration directories not flushed");
#endif
            }
            {
                auto intent=decodeAssetRestorationRecord(read(project/successfulOperation/"intent.json"),resources);
                auto receipt=decodeAssetRestorationRecord(read(project/successfulOperation/"receipt.json"),resources);
                verifyAssetRestorationRecord(intent,asset,context,Id(successfulOperation),AssetRestorationPhase::Planned,resources);++checks;
                verifyAssetRestorationRecord(receipt,asset,context,Id(successfulOperation),AssetRestorationPhase::Published,resources);++checks;
                check(!std::filesystem::exists(project/successfulOperation/"receipt.partial"),"Receipt partial survived complete publication");
            }
            check(resources.usage().reservedBytes==baseline && resources.usage().owners==owners && handles()==oldHandles,"Restoration owner leaked");
            check(read(project/utf8Path(asset.relativePath))==bytes && read(fixture/"input/source.wav")==bytes,"Restoration changed bytes");
            check(model==edited && read(project/"project.json")==manifest && ProjectStore(project).load()==edited,"Restoration changed project/provenance");
            check(history.undo() && model.tracks[0].name=="Original" && history.redo() && model==edited,"Restoration damaged Undo/Redo");
            ProjectStore(project).save(model);check(ProjectStore(project).load()==edited,"Restored project did not save/reopen");
            {
                auto collision=PreparedAssetRestoration::prepare(source,project,asset,context,1024);
                refuses([&]{collision.publish(asset,context);});check(read(project/utf8Path(asset.relativePath))==bytes,"Existing target overwritten");
            }
            {
                const auto target=root();std::filesystem::create_directories((target/utf8Path(asset.relativePath)).parent_path());
                std::filesystem::create_directory(target/utf8Path(asset.relativePath));
                auto prepared=PreparedAssetRestoration::prepare(source,target,asset,context,1024);
                refuses([&]{prepared.publish(asset,context);});
                check(std::filesystem::is_directory(target/utf8Path(asset.relativePath)),"Existing directory target replaced");
            }
            for(unsigned mode=0;mode<9;++mode) {
                const auto target=root();auto prepared=PreparedAssetRestoration::prepare(source,target,asset,context,1024);
                auto current=asset;auto currentContext=context;
                if(mode==0)++currentContext.projectEpoch;
                if(mode==1)++currentContext.modelRevision;
                if(mode==2)++currentContext.request;
                if(mode==3)current.id=Id::generate();
                if(mode==4)current.sha256[0]=current.sha256[0]=='a'?'b':'a';
                if(mode==5)++current.frames;
                if(mode==6)current.sampleRate=44100;
                if(mode==7)current.layout={LayoutKind::Stereo,2};
                if(mode==8)current.relativePath="media/other.wav";
                refuses([&]{prepared.publish(current,currentContext);});refuses([&]{prepared.publish(asset,context);});
                check(!std::filesystem::exists(target/"media"),"Stale operation touched canonical directories");
            }
            for(unsigned mode=0;mode<4;++mode) {
                const auto target=root();auto wrong=asset;
                if(mode==0)wrong.sha256[0]=wrong.sha256[0]=='a'?'b':'a';
                if(mode==1)++wrong.frames;
                if(mode==2)wrong.sampleRate=44100;
                if(mode==3)wrong.layout={LayoutKind::Stereo,2};
                refuses([&]{PreparedAssetRestoration::prepare(source,target,wrong,context,1024);});
                check(std::filesystem::is_empty(target),"Wrong candidate created staging");
            }
            for(const auto &path:{"../escape.wav","media/../../escape.wav","media/linked/../escape.wav","media/CON.wav","media/end/","session.json"}) {
                const auto target=root();auto wrong=asset;wrong.relativePath=path;
                refuses([&]{PreparedAssetRestoration::prepare(source,target,wrong,context,1024);});check(std::filesystem::is_empty(target),"Invalid path created staging");
            }
            {
                const auto target=root();std::filesystem::create_directory(target/"media");const auto outside=root();
                write(outside/"sentinel","preserve");directoryLink(outside,target/"media"/utf8Path("été-Κиїв"));
                auto prepared=PreparedAssetRestoration::prepare(source,target,asset,context,1024);
                refuses([&]{prepared.publish(asset,context);});check(read(outside/"sentinel")=="preserve" && !std::filesystem::exists(outside/"nested"),"Linked parent escaped root");
            }
            {
                const auto target=root();std::string partial;
                {
                    auto prepared=PreparedAssetRestoration::prepare(source,target,asset,context,1024);partial=prepared.stagedRelativePath();
                    std::stop_source stop;refuses([&]{prepared.publish(asset,context,stop.get_token(),[&](StageBoundary at,std::uint64_t){if(at==StageBoundary::BeforeCommit)stop.request_stop();});});
                    check(!std::filesystem::exists(target/utf8Path(asset.relativePath)),"Canceled operation published");
                }
                check(read(target/utf8Path(partial))==bytes,"Cancellation removed inspectable staging");
            }
            {
                const auto target=root();auto prepared=PreparedAssetRestoration::prepare(source,target,asset,context,1024);
                refuses([&]{prepared.publish(asset,context,{},[&](StageBoundary at,std::uint64_t){if(at==StageBoundary::BeforeCommit)write(target/utf8Path(asset.relativePath),"winner");});});
                check(read(target/utf8Path(asset.relativePath))=="winner","Racing destination overwritten");
            }
#ifndef _WIN32
            {
                const auto target=root();auto prepared=PreparedAssetRestoration::prepare(source,target,asset,context,1024);
                refuses([&]{prepared.publish(asset,context,{},[&](StageBoundary at,std::uint64_t){if(at==StageBoundary::BeforeCommit)write(target/utf8Path(prepared.stagedRelativePath()),"tampered");});});
                check(!std::filesystem::exists(target/utf8Path(asset.relativePath)),"Tampered staging published");
            }
#endif
            {
                const auto target=root();MediaCommitOutcome result;
                {
                    auto prepared=PreparedAssetRestoration::prepare(source,target,asset,context,1024);
                    result=prepared.publish(asset,context,{},[](StageBoundary at,std::uint64_t){if(at==StageBoundary::AfterCommitBeforeDirectoryFlush)throw ProjectError(ErrorCode::Io,"post-publication failure");});
                }
                check(result.published && !result.receiptPublished && result.postCommitFlushFailed && read(target/utf8Path(asset.relativePath))==bytes,"Committed truth lost after publication");
            }
            {
                const auto target=root();MediaCommitOutcome result;std::stop_source stop;
                {
                    auto prepared=PreparedAssetRestoration::prepare(source,target,asset,context,1024);
                    result=prepared.publish(asset,context,stop.get_token(),[&](StageBoundary at,std::uint64_t){if(at==StageBoundary::AfterCommitBeforeDirectoryFlush)stop.request_stop();});
                }
                check(stop.stop_requested() && result.published && result.receiptPublished && read(target/utf8Path(asset.relativePath))==bytes,"Post-commit cancellation hid publication/receipt");
            }
            {
                const auto target=root();std::stop_source stop;stop.request_stop();refuses([&]{PreparedAssetRestoration::prepare(source,target,asset,context,1024,{},Id::generate(),stop.get_token());});
                refuses([&]{PreparedAssetRestoration::prepare(source,target,asset,context,1);});check(std::filesystem::is_empty(target),"Preflight refusal touched target");
            }
            {
                std::string nonfinite="RIFF";put(nonfinite,52,4);nonfinite+="WAVEfmt ";put(nonfinite,16,4);put(nonfinite,3,2);put(nonfinite,1,2);
                put(nonfinite,48000,4);put(nonfinite,192000,4);put(nonfinite,4,2);put(nonfinite,32,2);nonfinite+="data";put(nonfinite,16,4);
                for(unsigned value:{0x7fc00000U,0U,0x80000000U,0x3f800000U})put(nonfinite,value,4);
                write(fixture/"input/nonfinite.wav",nonfinite);auto file=approved.open("nonfinite.wav",1024);auto invalid=asset;
                const auto hash=file.digest();invalid.sha256.assign(hash.begin(),hash.end());const auto target=root();bool finiteRefusal=false;
                try{(void)PreparedAssetRestoration::prepare(file,target,invalid,context,1024);}
                catch(const ProjectError &e){finiteRefusal=std::string(e.what()).find("non-finite")!=std::string::npos;}
                check(finiteRefusal && std::filesystem::is_empty(target),"Matching hash admitted nonfinite source or created staging");
            }
            for(const auto point:{StageBoundary::BeforeIntentWrite,StageBoundary::BeforeIntentFlush,StageBoundary::BeforeFile,StageBoundary::BeforeWrite}) {
                const auto target=root();const auto operation=Id::generate();std::stop_source stop;
                refuses([&]{PreparedAssetRestoration::prepare(source,target,asset,context,1024,{},operation,stop.get_token(),
                    [&](StageBoundary at,std::uint64_t){if(at==point)stop.request_stop();});});
                check(!std::filesystem::exists(target/utf8Path(asset.relativePath)) && !std::filesystem::exists(target/operation.str()/"receipt.json"),"Interrupted preparation claimed publication");
                if(point==StageBoundary::BeforeFile || point==StageBoundary::BeforeWrite) {
                    auto intent=decodeAssetRestorationRecord(read(target/operation.str()/"intent.json"),resources);
                    verifyAssetRestorationRecord(intent,asset,context,operation,AssetRestorationPhase::Planned,resources);++checks;
                }
            }
            for(const auto point:{StageBoundary::BeforeReceiptWrite,StageBoundary::BeforeReceiptFlush,StageBoundary::BeforeCommit}) {
                const auto target=root();std::string operation;
                {
                    auto prepared=PreparedAssetRestoration::prepare(source,target,asset,context,1024);operation=prepared.operation().str();std::stop_source stop;
                    refuses([&]{prepared.publish(asset,context,stop.get_token(),[&](StageBoundary at,std::uint64_t){if(at==point)stop.request_stop();});});
                }
                auto intent=decodeAssetRestorationRecord(read(target/operation/"intent.json"),resources);
                verifyAssetRestorationRecord(intent,asset,context,Id(operation),AssetRestorationPhase::Planned,resources);++checks;
                check(!std::filesystem::exists(target/utf8Path(asset.relativePath)) && !std::filesystem::exists(target/operation/"receipt.json"),"Interrupted receipt claimed publication");
            }
            {
                const auto target=root();std::string operation;MediaCommitOutcome result;
                {
                    auto prepared=PreparedAssetRestoration::prepare(source,target,asset,context,1024);operation=prepared.operation().str();
                    result=prepared.publish(asset,context,{},[](StageBoundary at,std::uint64_t){if(at==StageBoundary::BeforeRestorationReceiptPublish)throw ProjectError(ErrorCode::Io,"receipt publication failure");});
                }
                check(result.published && !result.receiptPublished && result.postCommitFlushFailed && read(target/utf8Path(asset.relativePath))==bytes,
                    "Missing audit receipt hid published media");
                check(!std::filesystem::exists(target/operation/"receipt.json") && std::filesystem::exists(target/operation/"receipt.partial"),"Failed audit fabricated receipt");
            }
#ifndef _WIN32
            for(const char *name:{"intent.json","receipt.partial"}) {
                const auto target=root();auto prepared=PreparedAssetRestoration::prepare(source,target,asset,context,1024);const auto operation=prepared.operation().str();
                refuses([&]{prepared.publish(asset,context,{},[&](StageBoundary at,std::uint64_t){if(at==StageBoundary::BeforeCommit)write(target/operation/name,"tampered");});});
                check(!std::filesystem::exists(target/utf8Path(asset.relativePath)),"Tampered evidence allowed publication");
            }
#endif
            {
                const auto target=root();auto wrong=asset;wrong.relativePath="media/"+std::string(256,'a')+".wav";
                refuses([&]{PreparedAssetRestoration::prepare(source,target,wrong,context,1024);});
                auto invalid=context;invalid.request=0;
                refuses([&]{PreparedAssetRestoration::prepare(source,target,asset,invalid,1024);});
                check(std::filesystem::is_empty(target),"Invalid admission touched destination");
            }
            {
                const auto target=root();ResourceLedger tiny(16384,"Insufficient restoration admission");
                {ApprovedMediaRoot budgetRoot(fixture/"input",tiny);auto file=budgetRoot.open("source.wav",1024);
                 refuses([&]{PreparedAssetRestoration::prepare(file,target,asset,context,1024);});}
                check(std::filesystem::is_empty(target) && tiny.usage().reservedBytes==0 && tiny.usage().owners==0,
                    "Budget refusal touched target or leaked credit");
            }
            check(resources.usage().reservedBytes==baseline && resources.usage().owners==owners && handles()==oldHandles,"Failure retirement leaked");
        }
        check(resources.usage().reservedBytes==0 && resources.usage().owners==0,"Restoration source credit leaked");
        std::filesystem::remove_all(fixture);std::cout<<checks<<" exclusive exact-asset restoration checks passed\n";return 0;
    }catch(const std::exception &e){std::cerr<<e.what()<<"; retained fixture "<<fixture<<'\n';return 1;}
}
