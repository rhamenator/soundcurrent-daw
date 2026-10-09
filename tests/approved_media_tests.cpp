// SPDX-License-Identifier: GPL-3.0-only
#include <soundcurrent/approved_media.hpp>
#include <algorithm>
#include <fstream>
#include <iostream>
#include <optional>
#include <vector>
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <sys/stat.h>
#endif
using namespace soundcurrent::daw;
namespace {
unsigned checks=0;
void check(bool good,const char *message) { ++checks;if (!good) throw std::runtime_error(message); }
template<class F> void refused(F fn,ErrorCode code) {
    bool seen=false;try { fn(); } catch (const ProjectError &e) { seen=true;check(e.code()==code,"Incorrect approved media refusal"); }
    check(seen,"Expected approved media refusal missing");
}
std::filesystem::path utf8(std::string_view s) {
    std::u8string bytes;bytes.reserve(s.size());
    for (unsigned char c:s) bytes.push_back(static_cast<char8_t>(c));
    return std::filesystem::path(bytes);
}
void write(const std::filesystem::path &path,std::string_view bytes) {
    std::ofstream out(path,std::ios::binary);out.write(bytes.data(),static_cast<std::streamsize>(bytes.size()));out.close();
    check(bool(out),"Cannot write owned fixture");
}
std::string hash(ApprovedMediaFile &file) { const auto h=file.digest();return {h.data(),h.size()}; }
void retired(const ResourceLedger &ledger) {
    check(ledger.usage().owners==0 && ledger.usage().reservedBytes==0,"Approved media credit leaked");
}
std::size_t handles() {
#ifdef _WIN32
    DWORD count=0;
    if (!GetProcessHandleCount(GetCurrentProcess(),&count))
        throw std::runtime_error("Cannot measure native handle count");
    return count;
#else
    std::size_t count=0;
    for (const auto &entry:std::filesystem::directory_iterator("/proc/self/fd")) {
        (void)entry;++count;
    }
    return count;
#endif
}
void link(const std::filesystem::path &target,const std::filesystem::path &name,bool directory=false) {
#ifdef _WIN32
    const DWORD flags=(directory ? SYMBOLIC_LINK_FLAG_DIRECTORY : 0)|SYMBOLIC_LINK_FLAG_ALLOW_UNPRIVILEGED_CREATE;
    bool made=CreateSymbolicLinkW(name.c_str(),target.c_str(),flags)!=0;
    if (!made) made=CreateSymbolicLinkW(name.c_str(),target.c_str(),directory ? SYMBOLIC_LINK_FLAG_DIRECTORY : 0)!=0;
    check(made,"Windows link fixture unavailable; native link refusal remains unqualified");
#else
    if (directory) std::filesystem::create_directory_symlink(target,name);
    else std::filesystem::create_symlink(target,name);
#endif
}
void counters(const ResourceLedger &ledger,ResourceUsage baseline) {
    const auto now=ledger.usage();check(now.owners==baseline.owners && now.reservedBytes==baseline.reservedBytes,"Failed media admission retained ownership");
}
#ifdef _WIN32
void caseSensitiveDirectory(const std::filesystem::path &path) {
    const auto directory=CreateFileW(path.c_str(),FILE_READ_ATTRIBUTES|FILE_WRITE_ATTRIBUTES,
        FILE_SHARE_READ|FILE_SHARE_WRITE|FILE_SHARE_DELETE,nullptr,OPEN_EXISTING,
        FILE_FLAG_BACKUP_SEMANTICS|FILE_FLAG_OPEN_REPARSE_POINT,nullptr);
    check(directory!=INVALID_HANDLE_VALUE,"Cannot open owned case-sensitive fixture directory");
    // FileCaseSensitiveInfo (23) is gated out by older SDK target macros.
    constexpr auto informationClass=static_cast<FILE_INFO_BY_HANDLE_CLASS>(23);
    FILE_CASE_SENSITIVE_INFO enabled{};enabled.Flags=1; // FILE_CS_FLAG_CASE_SENSITIVE_DIR
    const bool changed=SetFileInformationByHandle(directory,informationClass,&enabled,sizeof(enabled))!=0;
    const auto error=GetLastError();FILE_CASE_SENSITIVE_INFO actual{};
    const bool queried=GetFileInformationByHandleEx(directory,informationClass,&actual,sizeof(actual))!=0;
    CloseHandle(directory);
    if (!changed) throw std::runtime_error("Owned NTFS case-sensitivity fixture unavailable; qualification fails; Windows error="+std::to_string(error));
    check(queried && actual.Flags==1,"Owned NTFS case-sensitivity setting did not take effect");
}
#endif
void caseReferences(const std::filesystem::path &fixture,const ResourceLedger &memory) {
    const auto path=fixture/"case-sensitive";std::filesystem::create_directory(path);
#ifdef _WIN32
    caseSensitiveDirectory(path);
#endif
    write(path/"sample.wav","lower");write(path/"SAMPLE.wav","UPPER");
    std::filesystem::create_directory(path/"takes");std::filesystem::create_directory(path/"TAKES");
#ifdef _WIN32
    caseSensitiveDirectory(path/"takes");caseSensitiveDirectory(path/"TAKES");
#endif
    write(path/"takes"/"part.wav","nested-lower");write(path/"TAKES"/"part.wav","nested-upper");
    ApprovedMediaRoot root(path,memory);
    for (const auto &[relative,expected] : std::array<std::pair<const char *,const char *>,4>{{
             {"sample.wav","lower"},{"SAMPLE.wav","UPPER"},
             {"takes/part.wav","nested-lower"},{"TAKES/part.wav","nested-upper"}}}) {
        auto file=root.open(relative,100);std::array<char,32> bytes{};
        file.readAt(0,std::span(bytes).first(static_cast<std::size_t>(file.size())));
        check(std::string_view(bytes.data(),static_cast<std::size_t>(file.size()))==expected,
              "Case-distinct media reference selected a different file");
    }
    refused([&]{root.open("Sample.wav",100);},ErrorCode::MissingMedia);
    refused([&]{root.open("Takes/part.wav",100);},ErrorCode::MissingMedia);
    check(root.openFiles()==0,"Case-distinct fixture retained file handles");
}
}
int main() {
    const auto fixture=std::filesystem::temp_directory_path()/utf8("sc-approved-Κиїв-"+Id::generate().str());
    try {
        std::filesystem::create_directories(fixture/"approved"/"nested");
        std::filesystem::create_directories(fixture/"outside");
        const auto path=fixture/"approved";write(path/"abc.wav","abc");write(path/"empty.wav","");
        write(fixture/"outside"/"outside.wav","outside");
        const std::string unicode="nested/été-Κиїв.wav";write(path/utf8(unicode),"abc");
        std::string longBytes(3*65536+17,'\0');for (std::size_t i=0;i<longBytes.size();++i) longBytes[i]=char(i%127);
        write(path/"long.wav",longBytes);
        ResourceLedger memory(2*1024*1024,"Approved media acceptance");
        { // Prime the crypto provider before comparing OS-wide handle counts.
            ApprovedMediaRoot root(path,memory);auto file=root.open("abc.wav",3);file.digest();
        }
        retired(memory);const auto originalHandles=handles();
        {
            ApprovedMediaRoot root(path,memory,{2});const auto baseline=memory.usage();
            const auto rootHandles=handles();
            {
                auto file=root.open("abc.wav",3);check(file.size()==3,"Approved file size changed");
                check(hash(file)=="ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad","Pinned content hash changed");
                std::array<char,3> bytes{};file.readAt(0,bytes);check(std::string_view(bytes.data(),bytes.size())=="abc","Exact descriptor read changed");
                std::array<char,2> tail{};file.readAt(1,tail);check(std::string_view(tail.data(),tail.size())=="bc","Positional reads used stale cursor");
                file.readAt(3,{});refused([&]{file.readAt(2,tail);},ErrorCode::InvalidParameter);
                refused([&]{file.readAt(UINT64_MAX,{});},ErrorCode::InvalidParameter);
                auto second=root.open(unicode,10);check(hash(second)==hash(file),"Unicode media resolution changed bytes");
                check(root.openFiles()==2,"Open handle count wrong");
                check(handles()==rootHandles+2,"Native descriptor count exceeds admitted files");
                refused([&]{root.open("empty.wav",1);},ErrorCode::ResourceLimit);
            }
            check(root.openFiles()==0,"Media handles did not retire");counters(memory,baseline);
            check(handles()==rootHandles,"Native file descriptors did not retire");
            {
                auto empty=root.open("empty.wav",1);check(empty.size()==0,"Empty plain media refused");
                check(hash(empty)=="e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855","Empty media digest changed");
            }
            for (const std::string bad : {"../outside/outside.wav","nested/../../outside/outside.wav","/outside.wav","C:/outside.wav","nested\\x.wav","a//x.wav","a/./x.wav","a/../x.wav","a/","CON.wav","con.foo","LPT1.wav","com².wav","a.","a ","a?.wav","a*.wav","a|.wav","a<.wav"}) {
                refused([&]{root.open(bad,100);},ErrorCode::InvalidParameter);counters(memory,baseline);
            }
            for (const auto &bad : {std::string("a\0b",3),std::string("\xc0\xaf.wav"),std::string(256,'a')}) {
                refused([&]{root.open(bad,100);},ErrorCode::InvalidParameter);counters(memory,baseline);
            }
            std::string deep;for (unsigned n=0;n<65;++n) deep+="a/";deep+="f";
            refused([&]{root.open(deep,100);},ErrorCode::InvalidParameter);
            refused([&]{root.open("missing.wav",100);},ErrorCode::MissingMedia);
            refused([&]{root.open("nested/missing.wav",100);},ErrorCode::MissingMedia);
            refused([&]{root.open("abc.wav",0);},ErrorCode::InvalidParameter);
            refused([&]{root.open("abc.wav",2);},ErrorCode::ResourceLimit);
            link(fixture/"outside"/"outside.wav",path/"linked.wav");
            link(fixture/"outside",path/"linked-dir",true);
            refused([&]{root.open("linked.wav",100);},ErrorCode::InvalidParameter);
            refused([&]{root.open("linked-dir/outside.wav",100);},ErrorCode::InvalidParameter);
            refused([&]{root.open("nested",100);},ErrorCode::InvalidParameter);
#ifndef _WIN32
            check(mkfifo((path/"pipe.wav").c_str(),0600)==0,"Cannot create owned FIFO fixture");
            refused([&]{root.open("pipe.wav",100);},ErrorCode::InvalidParameter);
#endif
            counters(memory,baseline);
            check(handles()==rootHandles,"Failed admissions leaked native descriptors");
            {
                auto file=root.open("long.wav",longBytes.size());std::array<char,65536> bytes{};
                for (std::size_t offset=0;offset<longBytes.size();) {
                    const auto n=std::min(bytes.size(),longBytes.size()-offset);file.readAt(offset,std::span(bytes).first(n));
                    check(std::string_view(bytes.data(),n)==std::string_view(longBytes).substr(offset,n),"Bounded streaming read lost original bytes");offset+=n;
                }
                std::vector<char> tooLarge(65537);refused([&]{file.readAt(0,tooLarge);},ErrorCode::InvalidParameter);
                std::stop_source cancel;unsigned chunks=0;
                refused([&]{file.digest(cancel.get_token(),[&]{if (++chunks==2) cancel.request_stop();});},ErrorCode::Canceled);
                check(chunks==2,"Hash cancellation was not a chunk boundary");
                file.verifyUnchanged();
                const auto occupied=memory.usage();memory.configure(occupied.reservedBytes);
                refused([&]{file.digest();},ErrorCode::ResourceLimit);counters(memory,occupied);memory.configure(2*1024*1024);
#ifndef _WIN32
                refused([&]{file.digest({},[&]{write(path/"long.wav","changed");});},ErrorCode::MediaMismatch);
#else
                const auto writer=CreateFileW((path/"long.wav").c_str(),GENERIC_WRITE,FILE_SHARE_READ|FILE_SHARE_WRITE,nullptr,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,nullptr);
                check(writer==INVALID_HANDLE_VALUE && GetLastError()==ERROR_SHARING_VIOLATION,"Native approved handle allowed a concurrent writer");
                file.verifyUnchanged();
#endif
            }
            counters(memory,baseline);
            std::stop_source canceled;canceled.request_stop();
            refused([&]{root.open("abc.wav",100,canceled.get_token());},ErrorCode::Canceled);counters(memory,baseline);
        }
        retired(memory);
        check(handles()==originalHandles,"Approved root leaked a native descriptor");
        caseReferences(fixture,memory);retired(memory);
        check(handles()==originalHandles,"Case-sensitive fixture leaked a native descriptor");
        {
            const auto original=fixture/"renamed-approved";std::filesystem::rename(path,original);
            ApprovedMediaRoot root(original,memory);
            const auto moved=fixture/"pinned-approved";std::filesystem::rename(original,moved);
            std::filesystem::create_directory(original);write(original/"abc.wav","different");
            auto file=root.open("abc.wav",100);check(hash(file)=="ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad","Root path replacement redirected approved handle");
        }
        retired(memory);
        std::optional<ApprovedMediaFile> survivor;
        {
            ApprovedMediaRoot root(fixture/"pinned-approved",memory);survivor.emplace(root.open("abc.wav",100));
        }
        check(memory.usage().owners==2,"File did not retain root ownership");
        check(hash(*survivor)=="ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad","File could not outlive approved facade");
        survivor.reset();retired(memory);
        ResourceLedger tiny(1);refused([&]{ApprovedMediaRoot root(fixture/"pinned-approved",tiny);},ErrorCode::ResourceLimit);retired(tiny);
        refused([&]{ApprovedMediaRoot root("relative",memory);},ErrorCode::InvalidParameter);retired(memory);
        std::filesystem::remove_all(fixture);
        std::cout<<"PASS: "<<checks<<" approved-root checks; owned files, bounded reads/hash, no audio or conversion\n";return 0;
    } catch (const std::exception &error) {
        std::cerr<<error.what()<<"; retained owned fixture="<<fixture<<'\n';return 1;
    }
}
