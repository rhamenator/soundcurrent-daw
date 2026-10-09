// SPDX-License-Identifier: GPL-3.0-only
#include <soundcurrent/media_staging.hpp>
#include <fstream>
#include <iostream>
#include <optional>
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <sys/stat.h>
#include <cerrno>
#include <unistd.h>
#endif
using namespace soundcurrent::daw;
#ifdef SC_STAGING_IO_WRAP
namespace { enum class Fault { None, ShortWrite, DiskFull, ZeroWrite, Flush }; Fault fault=Fault::None;unsigned shortWrites=0; }
extern "C" ssize_t __real_pwrite(int,const void *,size_t,off_t);
extern "C" int __real_fsync(int);
extern "C" ssize_t __wrap_pwrite(int fd,const void *bytes,size_t count,off_t offset) {
    if (fault==Fault::DiskFull) {errno=ENOSPC;return -1;}
    if (fault==Fault::ZeroWrite) return 0;
    if (fault==Fault::ShortWrite && count>17) {count=17;++shortWrites;}
    return __real_pwrite(fd,bytes,count,offset);
}
extern "C" int __wrap_fsync(int fd) {
    if (fault==Fault::Flush) {errno=EIO;return -1;}
    return __real_fsync(fd);
}
#endif

namespace {
unsigned checks=0;
void check(bool value,const char *message) { ++checks;if (!value) throw std::runtime_error(message); }
template<class F> void refuses(F function,ErrorCode code) {
    bool caught=false;try {function();} catch (const ProjectError &e) {caught=true;check(e.code()==code,"Unexpected staging refusal");}
    check(caught,"Expected staging refusal missing");
}
std::filesystem::path utf8(std::string_view s) {
    return std::filesystem::path(std::u8string(reinterpret_cast<const char8_t *>(s.data()),s.size()));
}
void write(const std::filesystem::path &p,std::string_view bytes) {
    std::ofstream out(p,std::ios::binary);out.write(bytes.data(),static_cast<std::streamsize>(bytes.size()));out.close();check(bool(out),"Fixture write failed");
}
std::string read(const std::filesystem::path &p) {
    std::ifstream in(p,std::ios::binary);check(bool(in),"Fixture read failed");return {std::istreambuf_iterator<char>(in),{}};
}
void usage(const ResourceLedger &ledger,ResourceUsage before) {
    const auto after=ledger.usage();check(after.owners==before.owners && after.reservedBytes==before.reservedBytes,"Staging credit leaked");
}
void directoryLink(const std::filesystem::path &target,const std::filesystem::path &link) {
#ifdef _WIN32
    bool made=CreateSymbolicLinkW(link.c_str(),target.c_str(),SYMBOLIC_LINK_FLAG_DIRECTORY|SYMBOLIC_LINK_FLAG_ALLOW_UNPRIVILEGED_CREATE)!=0;
    if (!made) made=CreateSymbolicLinkW(link.c_str(),target.c_str(),SYMBOLIC_LINK_FLAG_DIRECTORY)!=0;
    check(made,"Native staging link fixture unavailable");
#else
    std::filesystem::create_directory_symlink(target,link);
#endif
}
std::size_t handles() {
#ifdef _WIN32
    DWORD count=0;check(GetProcessHandleCount(GetCurrentProcess(),&count)!=0,"Cannot measure handles");return count;
#else
    return static_cast<std::size_t>(std::distance(std::filesystem::directory_iterator("/proc/self/fd"),std::filesystem::directory_iterator{}));
#endif
}
}
int main() {
    const auto fixture=std::filesystem::temp_directory_path()/utf8("sc-stage-Κиїв-"+Id::generate().str());
    try {
        const auto input=fixture/"source",output=fixture/"destination";
        std::filesystem::create_directories(input);std::filesystem::create_directory(output);
        std::string bytes(3*65536+17,'\0');for (std::size_t i=0;i<bytes.size();++i) bytes[i]=char(i%251);
        const auto name=utf8("été-Κиїв.wav");write(input/name,bytes);write(input/"empty.wav","");
        ResourceLedger resources(2*1024*1024,"Staging test payload");
        std::optional<StagedMedia> retained;std::string staged;
        {
            ApprovedMediaRoot root(input,resources);auto source=root.openSelectedFilename(name,bytes.size());
            const CheckedMediaBytes checked{source.size(),source.digest()};const auto before=resources.usage();const auto oldHandles=handles();
            {
                const auto id=Id::generate();auto result=stageVerifiedMedia(source,output,checked,bytes.size(),id);
                staged=result.relativePath();check(result.operation()==id,"Operation identity changed");
                check(staged==id.str()+"/media.partial","Foreign reference became destination path");
                check(result.checkedBytes().sha256==checked.sha256 && result.checkedBytes().bytes==bytes.size(),"Copy snapshot changed");
                check(result.chargedBytes()>0,"Verified result has no retained credit");
                check(handles()==oldHandles+3,"Native staging ownership exceeds three handles");
#ifdef _WIN32
                check(result.durability()==StageDurability::FileFlushed,"Windows claimed unsupported directory durability");
#else
                check(result.durability()==StageDurability::FileAndDirectoriesFlushed,"Linux directories were not flushed");
#endif
                auto moved=std::move(result);refuses([&]{result.operation();},ErrorCode::InvalidState);
                check(moved.operation()==id,"Moved staging result lost ownership");
            }
            usage(resources,before);check(handles()==oldHandles,"Staging native handles leaked");
            check(read(output/utf8(staged))==bytes,"Independent copied bytes differ");check(read(input/name)==bytes,"Source was modified");
            const auto collision=Id::generate();std::filesystem::create_directory(output/collision.str());
            write(output/collision.str()/"sentinel","preserve");
            refuses([&]{stageVerifiedMedia(source,output,checked,bytes.size(),collision);},ErrorCode::Io);
            check(read(output/collision.str()/"sentinel")=="preserve" && !std::filesystem::exists(output/collision.str()/"media.partial"),"Collision overwritten");usage(resources,before);
            auto mismatch=checked;mismatch.sha256[0]=mismatch.sha256[0]=='0' ? '1':'0';const auto wrong=Id::generate();
            refuses([&]{stageVerifiedMedia(source,output,mismatch,bytes.size(),wrong);},ErrorCode::MediaMismatch);
            check(!std::filesystem::exists(output/wrong.str()),"Digest mismatch created destination");usage(resources,before);
            mismatch=checked;--mismatch.bytes;
            refuses([&]{stageVerifiedMedia(source,output,mismatch,bytes.size());},ErrorCode::MediaMismatch);
            mismatch=checked;mismatch.sha256[0]='G';
            refuses([&]{stageVerifiedMedia(source,output,mismatch,bytes.size());},ErrorCode::InvalidParameter);
            refuses([&]{stageVerifiedMedia(source,output,checked,bytes.size()-1);},ErrorCode::InvalidParameter);
            refuses([&]{stageVerifiedMedia(source,"relative",checked,bytes.size());},ErrorCode::InvalidParameter);
            refuses([&]{stageVerifiedMedia(source,output/"absent",checked,bytes.size());},ErrorCode::Io);
            directoryLink(output,fixture/"linked");
#ifdef _WIN32
            refuses([&]{stageVerifiedMedia(source,fixture/"linked",checked,bytes.size());},ErrorCode::InvalidParameter);
#else
            refuses([&]{stageVerifiedMedia(source,fixture/"linked",checked,bytes.size());},ErrorCode::Io);
#endif
            usage(resources,before);
            for (const auto at : {StageBoundary::BeforeDirectory,StageBoundary::BeforeFile,StageBoundary::BeforeWrite,
                     StageBoundary::BeforeFileFlush,StageBoundary::BeforeReadback,StageBoundary::BeforeFinalSourceCheck,StageBoundary::BeforeDirectoryFlush}) {
                const auto id=Id::generate();std::stop_source stop;
                refuses([&]{stageVerifiedMedia(source,output,checked,bytes.size(),id,stop.get_token(),
                    [&](StageBoundary boundary,std::uint64_t){if (boundary==at) stop.request_stop();});},ErrorCode::Canceled);
                check(std::filesystem::exists(output/id.str())==(at!=StageBoundary::BeforeDirectory),"Cancellation directory boundary changed");
                check(read(input/name)==bytes,"Canceled stage modified source");usage(resources,before);check(handles()==oldHandles,"Canceled stage leaked native handles");
                const auto failure=Id::generate();
                refuses([&]{stageVerifiedMedia(source,output,checked,bytes.size(),failure,{},
                    [&](StageBoundary boundary,std::uint64_t){if (boundary==at) throw ProjectError(ErrorCode::Io,"Injected staging boundary failure");});},ErrorCode::Io);
                usage(resources,before);check(handles()==oldHandles,"Failed stage leaked native handles");
            }
            { // Failure after one full block retains partial work, never a result.
                const auto id=Id::generate();std::stop_source stop;
                refuses([&]{stageVerifiedMedia(source,output,checked,bytes.size(),id,stop.get_token(),
                    [&](StageBoundary at,std::uint64_t offset){if (at==StageBoundary::BeforeWrite && offset==65536) stop.request_stop();});},ErrorCode::Canceled);
                check(read(output/id.str()/"media.partial")==bytes.substr(0,65536),"Canceled partial extent is not one block");usage(resources,before);
            }
#ifndef _WIN32
            { // Same-handle readback detects tampering by a trusted fault fixture.
                const auto id=Id::generate();
                refuses([&]{stageVerifiedMedia(source,output,checked,bytes.size(),id,{},
                    [&](StageBoundary at,std::uint64_t){if (at==StageBoundary::BeforeReadback) write(output/id.str()/"media.partial","tampered");});},ErrorCode::MediaMismatch);
                usage(resources,before);
            }
            { // Open parent capability survives a namespace rename.
                const auto renamed=fixture/"renamed-parent";const auto id=Id::generate();
                auto result=stageVerifiedMedia(source,output,checked,bytes.size(),id,{},
                    [&](StageBoundary at,std::uint64_t){if (at==StageBoundary::BeforeDirectory) std::filesystem::rename(output,renamed);});
                check(read(renamed/id.str()/"media.partial")==bytes,"Staging reopened renamed parent by pathname");
                std::filesystem::rename(renamed,output);
            }
#endif
#ifdef SC_STAGING_IO_WRAP
            {
                const auto id=Id::generate();fault=Fault::ShortWrite;
                {auto result=stageVerifiedMedia(source,output,checked,bytes.size(),id);}
                fault=Fault::None;check(shortWrites>1000,"Partial-write fixture did not exercise looping");
                check(read(output/id.str()/"media.partial")==bytes,"Native short writes lost bytes");usage(resources,before);
            }
            for (const auto injected : {Fault::DiskFull,Fault::ZeroWrite,Fault::Flush}) {
                const auto id=Id::generate();fault=injected;
                refuses([&]{stageVerifiedMedia(source,output,checked,bytes.size(),id);},ErrorCode::Io);
                fault=Fault::None;usage(resources,before);check(handles()==oldHandles,"Native I/O fault leaked handles");
                check(read(input/name)==bytes,"Native I/O fault changed source");
                check(std::filesystem::exists(output/id.str()/"media.partial"),"Native I/O failure removed inspectable partial data");
            }
#endif
            { // Admission refuses before any destination mutation.
                const auto id=Id::generate();const auto current=resources.usage();resources.configure(current.reservedBytes+1024);
                refuses([&]{stageVerifiedMedia(source,output,checked,bytes.size(),id);},ErrorCode::ResourceLimit);
                check(!std::filesystem::exists(output/id.str()),"Unadmitted staging mutated destination");resources.configure(2*1024*1024);usage(resources,before);
            }
            {auto empty=root.open("empty.wav",1);auto result=stageVerifiedMedia(empty,output,{0,empty.digest()},1);check(result.checkedBytes().bytes==0,"Empty byte-copy changed size");}
            retained.emplace(stageVerifiedMedia(source,output,checked,bytes.size()));
        }
        check(resources.usage().owners==1 && resources.usage().reservedBytes==retained->chargedBytes(),"Result depended on retired source/root facade");
        staged=retained->relativePath();retained.reset();check(resources.usage().owners==0 && resources.usage().reservedBytes==0,"Retained stage credit leaked");
        check(read(output/utf8(staged))==bytes,"Retired stage removed inspection data");
        check(!std::filesystem::exists(output/"project.json"),"Copy published project state");
        std::filesystem::remove_all(fixture);std::cout<<"Passed "<<checks<<" media staging checks\n";return 0;
    } catch (const std::exception &e) {
        std::cerr<<"Media staging failure after "<<checks<<" checks: "<<e.what()<<'\n';std::error_code ec;std::filesystem::remove_all(fixture,ec);return 1;
    }
}
