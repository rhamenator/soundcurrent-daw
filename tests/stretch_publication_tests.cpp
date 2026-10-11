// SPDX-License-Identifier: GPL-3.0-only
#include <soundcurrent/stretch_recovery.hpp>
#include <soundcurrent/project_store.hpp>
#include "media_io.hpp"
#include <algorithm>
#include <array>
#include <bit>
#include <fstream>
#include <iostream>
#include <map>
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <aclapi.h>
#include <cstring>
#endif
#ifdef SC_SELECTION_IO_WRAP
#include <cerrno>
#include <csignal>
#include <sys/resource.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>
#endif

using namespace soundcurrent::daw;
namespace {
unsigned checks=0;
void check(bool ok,const char *message) {++checks;if(!ok)throw std::runtime_error(message);}
std::string read(const std::filesystem::path &p) {
    std::ifstream f(p,std::ios::binary);check(bool(f),"Cannot read owned publication fixture");
    return {std::istreambuf_iterator<char>(f),{}};
}
void write(const std::filesystem::path &p,std::string_view bytes) {
    std::ofstream f(p,std::ios::binary);f.write(bytes.data(),std::streamsize(bytes.size()));f.close();
    check(bool(f),"Cannot write owned publication fixture");
}
void put(std::string &s,std::uint64_t v,unsigned n) {for(unsigned i=0;i<n;++i)s+=char((v>>(8*i))&255);}
Session fixture(const std::filesystem::path &root) {
    std::filesystem::create_directories(root/"media");
    std::string samples;for(unsigned i=0;i<128;++i)put(samples,std::bit_cast<std::uint32_t>(float(i%7)/4),4);
    std::string wav="RIFF";put(wav,36+samples.size(),4);wav+="WAVEfmt ";put(wav,16,4);put(wav,3,2);
    put(wav,1,2);put(wav,48000,4);put(wav,48000*4,4);put(wav,4,2);put(wav,32,2);wav+="data";put(wav,samples.size(),4);wav+=samples;
    const auto raw="media/raw-été.wav";write(root/utf8Path(raw),wav);
    auto s=makeOneTrackSession("Publication fault Κиїв","Audio");Asset a;
    a.relativePath=raw;a.sha256=hashMediaFile(root/utf8Path(raw));a.frames=128;a.layout={LayoutKind::Mono,1};s.assets={a};
    s.tracks[0]=makeAudioTrack("Audio",a.layout,s.sampleRate);Clip c;c.assetId=a.id;c.lengthFrames=128;s.tracks[0].clips={c};
    validate(s);ProjectStore(root).save(s);return s;
}
using Files=std::map<std::string,std::string>;
Files files(const std::filesystem::path &root) {
    Files result;for(const auto &e:std::filesystem::directory_iterator(root)) {
        check(e.is_regular_file(),"Unexpected publication fixture entry");const auto name=e.path().filename().u8string();
        result.emplace(std::string(reinterpret_cast<const char *>(name.data()),name.size()),read(e.path()));
    }return result;
}
void credit(const ResourceLedger &memory,ResourceUsage before) {
    const auto after=memory.usage();check(after.owners==before.owners && after.reservedBytes==before.reservedBytes,"Publication leaked resource credit");
}
template<class F>void ioRefusal(F f) {
    bool refused=false;try{f();}catch(const ProjectError &e){refused=true;check(e.code()==ErrorCode::Io,"Publication fault lost I/O classification");}
    check(refused,"Publication fault returned a success digest");
}
#ifdef _WIN32
struct NativeHandle {
    HANDLE value=nullptr;
    ~NativeHandle(){if(value && value!=INVALID_HANDLE_VALUE)CloseHandle(value);}
    NativeHandle()=default;
    NativeHandle(const NativeHandle &)=delete;
    NativeHandle &operator=(const NativeHandle &)=delete;
};
struct LocalBuffer {
    void *value=nullptr;
    ~LocalBuffer(){if(value)LocalFree(value);}
    LocalBuffer()=default;
    LocalBuffer(const LocalBuffer &)=delete;
    LocalBuffer &operator=(const LocalBuffer &)=delete;
};
// Only the already-created, owned fixture directory is altered. Deny creation
// to the current user without inheriting the deny into existing files. Capture
// the original DACL before setting it, and restore before removing any fixture.
class CreationDenial {
  public:
    explicit CreationDenial(const std::filesystem::path &directory):path(directory.wstring()) {
        NativeHandle token;
        check(OpenProcessToken(GetCurrentProcess(),TOKEN_QUERY,&token.value)!=0,"Cannot query owned fixture user token");
        DWORD bytes=0;
        check(!GetTokenInformation(token.value,TokenUser,nullptr,0,&bytes) && GetLastError()==ERROR_INSUFFICIENT_BUFFER && bytes>0 && bytes<65536,"Invalid fixture token buffer bound");
        std::vector<std::byte> user(bytes);
        check(GetTokenInformation(token.value,TokenUser,user.data(),bytes,&bytes)!=0,"Cannot read fixture token user");
        check(GetNamedSecurityInfoW(path.data(),SE_FILE_OBJECT,DACL_SECURITY_INFORMATION,nullptr,nullptr,&originalAcl,nullptr,reinterpret_cast<PSECURITY_DESCRIPTOR *>(&original.value))==ERROR_SUCCESS && originalAcl,"Cannot retain owned directory DACL");
        EXPLICIT_ACCESS_W deny{};deny.grfAccessPermissions=FILE_ADD_FILE;deny.grfAccessMode=DENY_ACCESS;
        deny.grfInheritance=NO_INHERITANCE;deny.Trustee.TrusteeForm=TRUSTEE_IS_SID;deny.Trustee.TrusteeType=TRUSTEE_IS_USER;
        deny.Trustee.ptstrName=reinterpret_cast<LPWSTR>(reinterpret_cast<TOKEN_USER *>(user.data())->User.Sid);
        check(SetEntriesInAclW(1,&deny,originalAcl,reinterpret_cast<PACL *>(&replacement.value))==ERROR_SUCCESS,"Cannot prepare owned creation denial");
        check(SetNamedSecurityInfoW(path.data(),SE_FILE_OBJECT,DACL_SECURITY_INFORMATION,nullptr,nullptr,static_cast<PACL>(replacement.value),nullptr)==ERROR_SUCCESS,"Cannot set owned creation denial");
        active=true;
    }
    ~CreationDenial(){if(active)restore();}
    bool restore() noexcept {
        if(!active)return true;
        if(SetNamedSecurityInfoW(path.data(),SE_FILE_OBJECT,DACL_SECURITY_INFORMATION,nullptr,nullptr,originalAcl,nullptr)!=ERROR_SUCCESS)return false;
        LocalBuffer restored;PACL acl=nullptr;
        const auto status=GetNamedSecurityInfoW(path.data(),SE_FILE_OBJECT,DACL_SECURITY_INFORMATION,nullptr,nullptr,&acl,nullptr,reinterpret_cast<PSECURITY_DESCRIPTOR *>(&restored.value));
        if(status!=ERROR_SUCCESS || !acl || acl->AclSize!=originalAcl->AclSize || std::memcmp(acl,originalAcl,acl->AclSize)!=0)return false;
        active=false;return true;
    }
    CreationDenial(const CreationDenial &)=delete;
    CreationDenial &operator=(const CreationDenial &)=delete;
  private:
    std::wstring path;
    LocalBuffer original,replacement;
    PACL originalAcl=nullptr;
    bool active=false;
};
DWORD processHandles() {
    DWORD n=0;check(GetProcessHandleCount(GetCurrentProcess(),&n)!=0,"Cannot count native publication handles");return n;
}
#endif
#ifdef SC_SELECTION_IO_WRAP
enum class Fault {None,ShortWrites,DiskFull,ZeroWrite,FileFlush,Link,Unlink,DirectoryFlush,CancelAfterFileFlush,KillAfterWrite,KillAfterFileFlush,KillAfterLink,KillAfterUnlink};
Fault fault=Fault::None;
std::string ownedDirectory;
unsigned intercepted=0,shortWrites=0;
int nativeError=0;
bool interruptedOnce=false;
std::stop_source *cancelSource=nullptr;
bool ownedPartial(const char *path) {
    const std::filesystem::path p(path);const auto name=p.filename().string();
    return p.parent_path().string()==ownedDirectory && name.starts_with("selection-") && name.ends_with(".partial");
}
bool descriptor(int fd,bool directory=false) {
    std::array<char,4096> bytes{};const auto n=readlink(("/proc/self/fd/"+std::to_string(fd)).c_str(),bytes.data(),bytes.size()-1);
    if(n<0)return false;
    bytes[std::size_t(n)]='\0';
    return directory?std::string_view(bytes.data())==ownedDirectory:ownedPartial(bytes.data());
}
void killSelf() {kill(getpid(),SIGKILL);_exit(99);}
void arm(Fault f) {fault=f;intercepted=shortWrites=0;nativeError=0;interruptedOnce=false;}
#endif
}

#ifdef SC_SELECTION_IO_WRAP
extern "C" ssize_t __real_write(int,const void *,size_t);
extern "C" int __real_fsync(int);
extern "C" int __real_link(const char *,const char *);
extern "C" int __real_unlink(const char *);
extern "C" ssize_t __wrap_write(int fd,const void *bytes,size_t count) {
    if(!descriptor(fd))return __real_write(fd,bytes,count);
    if(fault==Fault::DiskFull && intercepted++) {nativeError=errno=ENOSPC;return -1;}
    if(fault==Fault::ZeroWrite) {++intercepted;return 0;}
    if(fault==Fault::ShortWrites && !interruptedOnce) {interruptedOnce=true;++intercepted;nativeError=errno=EINTR;return -1;}
    if((fault==Fault::ShortWrites || fault==Fault::DiskFull) && count>17) {count=17;++shortWrites;}
    const auto n=__real_write(fd,bytes,count);if(n<0)nativeError=errno;
    if(n>0 && fault==Fault::KillAfterWrite)killSelf();
    return n;
}
extern "C" int __wrap_fsync(int fd) {
    if(descriptor(fd) && fault==Fault::FileFlush) {++intercepted;nativeError=errno=EIO;return -1;}
    if(descriptor(fd,true) && fault==Fault::DirectoryFlush) {++intercepted;nativeError=errno=EIO;return -1;}
    const int n=__real_fsync(fd);
    if(n==0 && descriptor(fd)) {
        if(fault==Fault::CancelAfterFileFlush) {++intercepted;cancelSource->request_stop();}
        if(fault==Fault::KillAfterFileFlush)killSelf();
    }
    return n;
}
extern "C" int __wrap_link(const char *from,const char *to) {
    if(ownedPartial(from) && fault==Fault::Link) {++intercepted;nativeError=errno=EIO;return -1;}
    const int n=__real_link(from,to);if(n==0 && ownedPartial(from) && fault==Fault::KillAfterLink)killSelf();return n;
}
extern "C" int __wrap_unlink(const char *path) {
    if(ownedPartial(path) && fault==Fault::Unlink && !intercepted++) {nativeError=errno=EIO;return -1;}
    const int n=__real_unlink(path);if(n==0 && ownedPartial(path) && fault==Fault::KillAfterUnlink)killSelf();return n;
}
#endif

int main() {
    const auto base=std::filesystem::temp_directory_path()/utf8Path("sc-selection-fault-Κиїв-"+Id::generate().str());
    try {
        const auto s=fixture(base);ResourceLedger memory(64*1024*1024,"Owned selection publication tests");
        const auto plan=prepareClipStretch(s,s.tracks[0].id,s.tracks[0].clips[0].id,{3,2,0,true});
        StretchRenderPolicy policy;StretchRecoveryLimits limits;limits.render=policy;
        const auto retained=Id::generate();const auto digest=persistStretchSelection(base,s,plan,retained,policy,memory,limits);
        check(digest.size()==64,"Healthy publication returned no digest");
        const auto directory=base/"media"/"stretch-selections";
        const auto before=memory.usage();const auto pristine=files(directory);
#ifdef SC_SELECTION_IO_WRAP
        const auto handles=std::distance(std::filesystem::directory_iterator("/proc/self/fd"),std::filesystem::directory_iterator{});
#endif
        const auto projectBytes=read(base/"project.json"),rawBytes=read(base/utf8Path(s.assets[0].relativePath));
        auto unchanged=[&] {
            check(read(base/"project.json")==projectBytes && ProjectStore(base).load()==s,"Publication altered saved project");
            check(read(base/utf8Path(s.assets[0].relativePath))==rawBytes,"Publication altered raw audio");
            for(const auto &[name,bytes]:pristine)check(read(directory/utf8Path(name))==bytes,"Publication altered an older selection/lock");
            credit(memory,before);
            {media_io::JobLease lease(directory,false,true);check(lease.status()==media_io::LeaseStatus::Held,"Publication left writer lease held");}
#ifdef SC_SELECTION_IO_WRAP
            check(std::distance(std::filesystem::directory_iterator("/proc/self/fd"),std::filesystem::directory_iterator{})==handles,"Publication leaked native descriptors");
#endif
        };
        ioRefusal([&]{persistStretchSelection(base,s,plan,retained,policy,memory,limits);});
        check(files(directory)==pristine,"Duplicate refusal retained a temporary file");unchanged();
#ifdef _WIN32
        const auto coldHandles=processHandles();
        {CreationDenial warm(directory);check(warm.restore(),"Cannot restore native security API warm-up");}
        const auto nativeHandles=processHandles();
        std::cout<<"Native Windows security API handle warm-up: "<<coldHandles<<" -> "<<nativeHandles<<'\n';
        {
            const auto op=Id::generate();CreationDenial denial(directory);
            // Confirm the OS refuses FILE_ADD_FILE, then run the actual parent
            // API under the same denial. Existing writer.lock remains readable.
            const auto probe=directory/"owned-denial-probe.partial";
            NativeHandle denied;denied.value=CreateFileW(probe.c_str(),GENERIC_WRITE,0,nullptr,CREATE_NEW,FILE_ATTRIBUTE_NORMAL,nullptr);
            const auto nativeError=GetLastError();
            check(denied.value==INVALID_HANDLE_VALUE && nativeError==ERROR_ACCESS_DENIED,"Owned creation denial did not cause real access refusal");
            ioRefusal([&]{persistStretchSelection(base,s,plan,op,policy,memory,limits);});
            check(files(directory)==pristine,"Denied parent publication altered leaves or retained temporary data");
            unchanged();check(denial.restore(),"Cannot restore owned publication directory DACL");
            const auto successful=persistStretchSelection(base,s,plan,op,policy,memory,limits);
            check(successful==hashMediaFile(directory/(op.str()+".json")),"Restored permissions did not permit correct parent publication");
            {const auto entry=inspectStretchSelection(base,s,op,memory,limits);check(entry.status==StretchRecoveryStatus::Incomplete && !entry.verified,"Unrendered Windows selection became verified");}
            std::filesystem::remove(directory/(op.str()+".json"));unchanged();
            std::cout<<"Native Windows parent creation denied with ERROR_ACCESS_DENIED; original DACL restored and retry verified\n";
        }
        {
            // Test the actual native no-overwrite primitive used by the parent.
            // A read handle that omits FILE_SHARE_DELETE prevents rename; this
            // is a primitive sharing-error test, not a parent timing hook.
            const auto temp=directory/("selection-"+Id::generate().str()+".partial");
            const auto final=directory/(Id::generate().str()+".native-fixture");
            const std::string payload="owned Windows sharing publication fixture";
            {media_io::File out(temp,true);out.write(payload);out.flush();out.close();}
            {
                NativeHandle hold;hold.value=CreateFileW(temp.c_str(),GENERIC_READ,FILE_SHARE_READ,nullptr,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,nullptr);
                check(hold.value!=INVALID_HANDLE_VALUE,"Cannot hold owned publication source without delete sharing");
                ioRefusal([&]{media_io::publishMedia(temp,final);});
                check(!std::filesystem::exists(final) && read(temp)==payload,"Native sharing refusal changed source or published final file");
            }
            media_io::publishMedia(temp,final);
            check(!std::filesystem::exists(temp) && read(final)==payload,"Native publication retry after handle release failed");
            std::filesystem::remove(final);unchanged();
            std::cout<<"Native Windows no-overwrite primitive refused while source handle denied delete sharing; release/retry preserved payload\n";
        }
        check(processHandles()==nativeHandles,"Windows publication leaked native process handles");
#endif
#ifdef SC_SELECTION_IO_WRAP
        ownedDirectory=directory.string();
        for(const auto f:{Fault::DiskFull,Fault::ZeroWrite,Fault::FileFlush,Fault::Link,Fault::Unlink,Fault::DirectoryFlush}) {
            const auto op=Id::generate();arm(f);
            ioRefusal([&]{persistStretchSelection(base,s,plan,op,policy,memory,limits);});
            const auto hits=intercepted;const auto error=nativeError;const auto shortCount=shortWrites;arm(Fault::None);
            check(hits>0,"Publication fault missed owned native boundary");
            if(f==Fault::DiskFull)check(error==ENOSPC && shortCount==1,"Wrong simulated disk-full error/partial write");
            else if(f!=Fault::ZeroWrite)check(error==EIO,"Wrong injected native boundary error");
            // A failure after the no-overwrite link can leave a complete final
            // leaf. It remains independently inspectable, never an attached edit.
            const bool published=f==Fault::Unlink || f==Fault::DirectoryFlush;
            auto expected=pristine;
            if(published) {
                const auto final=directory/(op.str()+".json");check(std::filesystem::exists(final),"Post-link failure lost final selection");
                expected.emplace(op.str()+".json",read(final));
                {const auto recovered=inspectStretchSelection(base,s,op,memory,limits);check(recovered.status==StretchRecoveryStatus::Incomplete && !recovered.verified,"Failed publication acquired a verified render");}
            }
            check(files(directory)==expected,"Fault cleanup retained an unexpected partial or lost old files");unchanged();
            if(published)std::filesystem::remove(directory/(op.str()+".json"));
            std::cout<<"Injected owned publication boundary "<<unsigned(f)<<" refused; finalPresent="<<published<<" nativeError="<<error<<'\n';
        }
        {
            const auto op=Id::generate();std::stop_source stop;cancelSource=&stop;arm(Fault::CancelAfterFileFlush);
            bool canceled=false;
            try{persistStretchSelection(base,s,plan,op,policy,memory,limits,stop.get_token());}
            catch(const ProjectError &e){canceled=true;check(e.code()==ErrorCode::Canceled,"Post-flush cancellation lost classification");}
            const auto hits=intercepted;arm(Fault::None);cancelSource=nullptr;
            check(canceled && hits==1 && stop.stop_requested(),"Post-flush cancellation missed actual boundary");
            check(files(directory)==pristine,"Post-flush cancellation published or retained partial data");unchanged();
            std::cout<<"Cancellation after actual file flush refused before publication; owned partial cleaned\n";
        }
        {
            const auto op=Id::generate();arm(Fault::ShortWrites);
            const auto successful=persistStretchSelection(base,s,plan,op,policy,memory,limits);
            check(shortWrites>1 && interruptedOnce && nativeError==EINTR,"Short write/EINTR fixture missed retry loop");arm(Fault::None);
            check(successful==hashMediaFile(directory/(op.str()+".json")),"Retry loop published a truncated selection");unchanged();
            std::filesystem::remove(directory/(op.str()+".json"));
        }
        // Actual kernel file-size refusal, in an owned child only. This does not
        // assert a full filesystem or modify this process's resource limits.
        {
            const auto op=Id::generate();std::cout<<std::flush;const auto pid=fork();check(pid>=0,"Cannot fork owned file-size refusal");
            if(pid==0) {
                try {
                    arm(Fault::None);signal(SIGXFSZ,SIG_IGN);rlimit limit{128,128};check(setrlimit(RLIMIT_FSIZE,&limit)==0,"Cannot bound child file size");
                    ioRefusal([&]{persistStretchSelection(base,s,plan,op,policy,memory,limits);});check(nativeError==EFBIG,"Kernel did not refuse actual write with EFBIG");
                    check(files(directory)==pristine,"Kernel write refusal retained partial data");unchanged();
                    std::cout<<"Actual owned child RLIMIT_FSIZE write refused with EFBIG; cleanup preserved files\n"<<std::flush;_exit(0);
                }catch(const std::exception &e){std::cerr<<e.what()<<'\n'<<std::flush;_exit(1);}
            }
            int status=0;check(waitpid(pid,&status,0)==pid && WIFEXITED(status) && WEXITSTATUS(status)==0,"Owned kernel refusal child failed");
            check(files(directory)==pristine,"Child failure changed publication directory");unchanged();
        }
        for(const auto f:{Fault::KillAfterWrite,Fault::KillAfterFileFlush,Fault::KillAfterLink,Fault::KillAfterUnlink}) {
            const auto op=Id::generate();std::cout<<std::flush;const auto pid=fork();check(pid>=0,"Cannot fork owned interruption fixture");
            if(pid==0) {arm(f);try{persistStretchSelection(base,s,plan,op,policy,memory,limits);}catch(...){} _exit(2);}
            int status=0;check(waitpid(pid,&status,0)==pid && WIFSIGNALED(status) && WTERMSIG(status)==SIGKILL,"Publication child was not actually killed at boundary");
            const auto all=files(directory);const bool published=f==Fault::KillAfterLink || f==Fault::KillAfterUnlink;
            const bool partial=f!=Fault::KillAfterUnlink;
            check(all.size()==pristine.size()+std::size_t(published)+std::size_t(partial),"Interrupted publication left wrong file set");
            std::filesystem::path remnant;
            for(const auto &[name,bytes]:all)if(!pristine.contains(name) && name!=op.str()+".json") {
                check(ownedPartial((directory/utf8Path(name)).c_str()),"Interruption retained an unowned file");remnant=directory/utf8Path(name);
                check(!bytes.empty(),"Interruption happened before manifest write");
            }
            {const auto recovered=inspectStretchSelection(base,s,op,memory,limits);
                check(recovered.status==(published?StretchRecoveryStatus::Incomplete:StretchRecoveryStatus::Invalid) && !recovered.verified,"Interrupted selection acquired a verified render");}
            {StretchRecoveryScan scan(base,s,memory,limits);const auto page=scan.next();check(!page.hasMore && page.entries.size()==std::size_t(1+published),"Fresh scan admitted temporary remnant as selection");}
            if(f==Fault::KillAfterLink) {
                struct stat a{},b{};check(stat(remnant.c_str(),&a)==0 && stat((directory/(op.str()+".json")).c_str(),&b)==0 && a.st_dev==b.st_dev && a.st_ino==b.st_ino,"Post-link interruption lost actual hard-link alias");
            }
            unchanged();check(files(directory)==all,"Read-only recovery mutated interrupted files");
            // Only the fixture owner cleans these files, after immutable checks.
            if(partial)std::filesystem::remove(remnant);
            if(published)std::filesystem::remove(directory/(op.str()+".json"));
            check(files(directory)==pristine,"Owned interruption cleanup lost older selection");
            std::cout<<"Actual SIGKILL publication boundary "<<unsigned(f)<<" reaped; partialPresent="<<partial<<" finalPresent="<<published<<'\n';
        }
#endif
        std::filesystem::remove_all(base);
#ifdef _WIN32
        std::cout<<"PASS: "<<checks<<" owned selection publication checks; windows_access_denied_parent=true; windows_sharing_primitive=true; native audio=false; Windows-process-interruption/full-disk/power-loss qualification=false\n";
#else
        std::cout<<"PASS: "<<checks<<" owned selection publication checks; native audio=false; full-disk/power-loss/Windows-fault qualification=false\n";
#endif
        return 0;
    }catch(const std::exception &e) {
        std::cerr<<"Publication test failed after "<<checks<<" checks: "<<e.what()<<'\n';std::error_code ec;std::filesystem::remove_all(base,ec);return 1;
    }
}
