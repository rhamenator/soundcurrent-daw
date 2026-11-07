// SPDX-License-Identifier: GPL-3.0-only
#include <soundcurrent/media_staging.hpp>
#include <soundcurrent/media_provenance.hpp>
#include "media_hash.hpp"
#include "native_directory_stamp.hpp"
#include <algorithm>
#include <bit>
#include <optional>
#ifdef _WIN32
#include <winternl.h>
#else
#include <cerrno>
#include <fcntl.h>
#include <linux/fs.h>
#include <linux/openat2.h>
#include <sys/syscall.h>
#include <sys/stat.h>
#include <unistd.h>
#endif
namespace soundcurrent::daw {
namespace {
void require(bool ok,const char *message,ErrorCode code=ErrorCode::Io) {
    if (!ok) throw ProjectError(code,message);
}
void poll(std::stop_token stop) {
    require(!stop.stop_requested(),"Media staging canceled",ErrorCode::Canceled);
}
void boundary(const StageObserver &observer,StageBoundary at,std::uint64_t offset,std::stop_token stop) {
    poll(stop);if (observer) observer(at,offset);poll(stop);
}
struct Handle {
#ifdef _WIN32
    HANDLE value=INVALID_HANDLE_VALUE;
    ~Handle() { if (value!=INVALID_HANDLE_VALUE) CloseHandle(value); }
#else
    int value=-1;
    ~Handle() { if (value>=0) ::close(value); }
#endif
    Handle()=default;Handle(const Handle &)=delete;
};
#ifdef _WIN32
void plainDirectory(const Handle &handle) {
    FILE_ATTRIBUTE_TAG_INFO info{};
    require(GetFileInformationByHandleEx(handle.value,FileAttributeTagInfo,&info,sizeof(info)) &&
        (info.FileAttributes&FILE_ATTRIBUTE_DIRECTORY) && !(info.FileAttributes&FILE_ATTRIBUTE_REPARSE_POINT),
        "Staging parent is not a plain directory",ErrorCode::InvalidParameter);
}
void create(const Handle &parent,std::string_view leaf,bool directory,Handle &out,bool renameable=false) {
    // Names are generated ASCII UUIDs or the constant media.partial, never
    // interpreted project references. RootDirectory binds the opened parent.
    std::array<wchar_t,64> name{};
    require(leaf.size()<name.size(),"Invalid staging leaf",ErrorCode::InvalidParameter);
    for (std::size_t i=0;i<leaf.size();++i) name[i]=wchar_t(static_cast<unsigned char>(leaf[i]));
    UNICODE_STRING unicode{};unicode.Buffer=name.data();
    unicode.Length=static_cast<USHORT>(leaf.size()*sizeof(wchar_t));unicode.MaximumLength=unicode.Length;
    OBJECT_ATTRIBUTES attributes{};attributes.Length=sizeof(attributes);attributes.RootDirectory=parent.value;
    attributes.ObjectName=&unicode;attributes.Attributes=0x1000UL; // OBJ_DONT_REPARSE
    using Create=NTSTATUS (NTAPI *)(PHANDLE,ACCESS_MASK,POBJECT_ATTRIBUTES,PIO_STATUS_BLOCK,
        PLARGE_INTEGER,ULONG,ULONG,ULONG,ULONG,PVOID,ULONG);
    const auto module=GetModuleHandleW(L"ntdll.dll");
    const auto entry=module ? GetProcAddress(module,"NtCreateFile") : nullptr;
    require(entry!=nullptr,"Contained staging creation unavailable",ErrorCode::InvalidState);
    const auto createFile=std::bit_cast<Create>(entry);IO_STATUS_BLOCK result{};
    // FILE_CREATE=2; synchronous=0x20; directory=1/non-directory=0x40.
    // Directory options deliberately omit FILE_OPEN_REPARSE_POINT, which is
    // not listed as compatible with FILE_DIRECTORY_FILE. FILE_CREATE plus
    // OBJ_DONT_REPARSE refuses an existing object without following it.
    const auto options=0x20UL|(directory ? 1UL : 0x40UL|0x200000UL);
    const auto access=directory ? FILE_LIST_DIRECTORY|FILE_ADD_FILE|FILE_TRAVERSE|FILE_READ_ATTRIBUTES|SYNCHRONIZE
        : GENERIC_READ|GENERIC_WRITE|SYNCHRONIZE|(renameable ? DELETE : 0UL);
    const auto status=createFile(&out.value,access,&attributes,&result,nullptr,
        directory ? FILE_ATTRIBUTE_DIRECTORY : FILE_ATTRIBUTE_NORMAL,
        directory && renameable ? FILE_SHARE_READ|FILE_SHARE_WRITE : 0UL,2,options,nullptr,0);
    require(status==0,"Cannot exclusively create staging object");
    if (directory) plainDirectory(out);
    else {
        FILE_ATTRIBUTE_TAG_INFO info{};
        require(GetFileType(out.value)==FILE_TYPE_DISK &&
            GetFileInformationByHandleEx(out.value,FileAttributeTagInfo,&info,sizeof(info)) &&
            !(info.FileAttributes&(FILE_ATTRIBUTE_DIRECTORY|FILE_ATTRIBUTE_REPARSE_POINT)),
            "Staging asset is not a plain file",ErrorCode::InvalidParameter);
    }
}
#else
void create(const Handle &parent,std::string_view leaf,bool directory,Handle &out,bool renameable=false) {
    (void)renameable;const std::string name(leaf);
    if (directory) {
        require(mkdirat(parent.value,name.c_str(),0700)==0,"Cannot exclusively create staging directory");
        out.value=openat(parent.value,name.c_str(),O_RDONLY|O_DIRECTORY|O_NOFOLLOW|O_CLOEXEC);
        require(out.value>=0,"Cannot open created staging directory");
        struct stat info{};
        require(fstat(out.value,&info)==0 && S_ISDIR(info.st_mode) && info.st_uid==geteuid() &&
            (info.st_mode&0777)==0700,"Created staging directory ownership changed",ErrorCode::InvalidParameter);
    } else {
        out.value=openat(parent.value,name.c_str(),O_RDWR|O_CREAT|O_EXCL|O_NOFOLLOW|O_CLOEXEC,0600);
        require(out.value>=0,"Cannot exclusively create staging asset");
        struct stat info{};
        require(fstat(out.value,&info)==0 && S_ISREG(info.st_mode) && info.st_nlink==1,
            "Staging asset is not a new plain file",ErrorCode::InvalidParameter);
    }
}
#endif
void seek(const Handle &file,std::uint64_t offset) {
#ifdef _WIN32
    LARGE_INTEGER position{};position.QuadPart=static_cast<LONGLONG>(offset);
    require(SetFilePointerEx(file.value,position,nullptr,FILE_BEGIN),"Cannot seek staging asset");
#else
    (void)file;(void)offset; // pread/pwrite keep positional access independent.
#endif
}
void write(const Handle &file,std::uint64_t offset,std::span<const char> bytes,std::stop_token stop) {
    seek(file,offset);std::size_t used=0;
    while (used<bytes.size()) {
        poll(stop);
#ifdef _WIN32
        DWORD count=0;
        require(WriteFile(file.value,bytes.data()+used,static_cast<DWORD>(bytes.size()-used),&count,nullptr),"Staging write failed");
#else
        const auto count=pwrite(file.value,bytes.data()+used,bytes.size()-used,static_cast<off_t>(offset+used));
        if (count<0 && errno==EINTR) continue;
        require(count>=0,"Staging write failed");
#endif
        require(count>0,"Staging write made no progress");used+=static_cast<std::size_t>(count);
    }
}
void read(const Handle &file,std::uint64_t offset,std::span<char> bytes,std::stop_token stop) {
    seek(file,offset);std::size_t used=0;
    while (used<bytes.size()) {
        poll(stop);
#ifdef _WIN32
        DWORD count=0;
        require(ReadFile(file.value,bytes.data()+used,static_cast<DWORD>(bytes.size()-used),&count,nullptr),"Staging readback failed");
#else
        const auto count=pread(file.value,bytes.data()+used,bytes.size()-used,static_cast<off_t>(offset+used));
        if (count<0 && errno==EINTR) continue;
        require(count>=0,"Staging readback failed");
#endif
        require(count>0,"Staged media truncated",ErrorCode::MediaMismatch);used+=static_cast<std::size_t>(count);
    }
}
std::uint64_t size(const Handle &file) {
#ifdef _WIN32
    LARGE_INTEGER bytes{};require(GetFileSizeEx(file.value,&bytes) && bytes.QuadPart>=0,"Cannot inspect staged extent");
    return static_cast<std::uint64_t>(bytes.QuadPart);
#else
    struct stat info{};require(fstat(file.value,&info)==0 && info.st_size>=0,"Cannot inspect staged extent");
    return static_cast<std::uint64_t>(info.st_size);
#endif
}
void flush(const Handle &file) {
#ifdef _WIN32
    require(FlushFileBuffers(file.value),"Cannot flush staged media");
#else
    int result=0;do {result=fsync(file.value);} while(result<0 && errno==EINTR);
    require(result==0,"Cannot flush staged media");
#endif
}

void renameExclusive(const Handle &directory,const Handle &file,const char *oldName,const char *newName) {
#ifdef _WIN32
    (void)oldName;
    // Original declaration from Microsoft's documented native layout; no vendor
    // implementation. Relative simple name binds the already-open directory.
    struct Rename { BOOLEAN replace;HANDLE root;ULONG bytes;WCHAR name[64]; } info{};
    const auto count=std::char_traits<char>::length(newName);require(count<64,"Invalid owned rename leaf");
    for (std::size_t i=0;i<count;++i) info.name[i]=wchar_t(static_cast<unsigned char>(newName[i]));
    info.bytes=static_cast<ULONG>(count*sizeof(wchar_t));info.root=directory.value; // replace=false
    using Set=NTSTATUS (NTAPI *)(HANDLE,PIO_STATUS_BLOCK,PVOID,ULONG,FILE_INFORMATION_CLASS);
    const auto module=GetModuleHandleW(L"ntdll.dll");const auto entry=module ? GetProcAddress(module,"NtSetInformationFile") : nullptr;
    require(entry!=nullptr,"Exclusive owned rename unavailable",ErrorCode::InvalidState);
    IO_STATUS_BLOCK result{};const auto set=std::bit_cast<Set>(entry);
    require(set(file.value,&result,&info,sizeof(info),static_cast<FILE_INFORMATION_CLASS>(10))==0,
        "Cannot exclusively publish owned staging name");
#else
    // Trusted destination writers, as for mkdir/open. No replacement fallback
    // if the kernel/filesystem does not implement RENAME_NOREPLACE.
    struct stat named{},held{};
    require(fstatat(directory.value,oldName,&named,AT_SYMLINK_NOFOLLOW)==0 && fstat(file.value,&held)==0 &&
        named.st_dev==held.st_dev && named.st_ino==held.st_ino && S_ISREG(named.st_mode),
        "Owned staging name changed",ErrorCode::MediaMismatch);
    require(syscall(SYS_renameat2,directory.value,oldName,directory.value,newName,RENAME_NOREPLACE)==0,
        "Cannot exclusively publish owned staging name");
#endif
}
void restorationDirectory(const Handle &parent,std::string_view leaf,Handle &out,bool mayCreate=true) {
#ifdef _WIN32
    std::array<wchar_t,256> name{};
    const auto count=MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,leaf.data(),static_cast<int>(leaf.size()),name.data(),255);
    require(count>0,"Cannot encode restoration directory",ErrorCode::InvalidParameter);
    UNICODE_STRING unicode{};unicode.Buffer=name.data();unicode.Length=static_cast<USHORT>(count*sizeof(wchar_t));unicode.MaximumLength=unicode.Length;
    OBJECT_ATTRIBUTES attributes{};attributes.Length=sizeof(attributes);attributes.RootDirectory=parent.value;
    attributes.ObjectName=&unicode;attributes.Attributes=0x1000UL; // OBJ_DONT_REPARSE
    using Create=NTSTATUS (NTAPI *)(PHANDLE,ACCESS_MASK,POBJECT_ATTRIBUTES,PIO_STATUS_BLOCK,PLARGE_INTEGER,ULONG,ULONG,ULONG,ULONG,PVOID,ULONG);
    const auto module=GetModuleHandleW(L"ntdll.dll");const auto entry=module ? GetProcAddress(module,"NtCreateFile") : nullptr;
    require(entry!=nullptr,"Contained restoration opening unavailable",ErrorCode::InvalidState);
    IO_STATUS_BLOCK result{};const auto createFile=std::bit_cast<Create>(entry);
    require(createFile(&out.value,FILE_LIST_DIRECTORY|FILE_ADD_FILE|FILE_ADD_SUBDIRECTORY|FILE_TRAVERSE|FILE_READ_ATTRIBUTES|SYNCHRONIZE,
        &attributes,&result,nullptr,FILE_ATTRIBUTE_DIRECTORY,FILE_SHARE_READ|FILE_SHARE_WRITE|FILE_SHARE_DELETE,
        mayCreate?3:1,0x21UL,nullptr,0)==0,"Cannot open/create plain restoration directory"); // FILE_OPEN_IF, synchronous directory
    plainDirectory(out);
#else
    const std::string name(leaf);open_how how{};how.flags=O_RDONLY|O_DIRECTORY|O_CLOEXEC;
    how.resolve=RESOLVE_BENEATH|RESOLVE_NO_SYMLINKS|RESOLVE_NO_MAGICLINKS|RESOLVE_NO_XDEV;
    out.value=static_cast<int>(syscall(SYS_openat2,parent.value,name.c_str(),&how,sizeof(how)));
    if (mayCreate && out.value<0 && errno==ENOENT) {
        const auto made=mkdirat(parent.value,name.c_str(),0700);
        require(made==0 || errno==EEXIST,"Cannot create restoration directory");
        out.value=static_cast<int>(syscall(SYS_openat2,parent.value,name.c_str(),&how,sizeof(how)));
    }
    require(out.value>=0,"Cannot open contained plain restoration directory");
#endif
}
void restoreRename(const Handle &sourceDirectory,const Handle &file,const Handle &destination,std::string_view leaf) {
#ifdef _WIN32
    (void)sourceDirectory;
    struct Rename { BOOLEAN replace;HANDLE root;ULONG bytes;WCHAR name[256]; } info{};
    const auto count=MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,leaf.data(),static_cast<int>(leaf.size()),info.name,255);
    require(count>0,"Cannot encode restoration filename",ErrorCode::InvalidParameter);
    info.root=destination.value;info.bytes=static_cast<ULONG>(count*sizeof(wchar_t)); // replace=false
    using Set=NTSTATUS (NTAPI *)(HANDLE,PIO_STATUS_BLOCK,PVOID,ULONG,FILE_INFORMATION_CLASS);
    const auto module=GetModuleHandleW(L"ntdll.dll");const auto entry=module ? GetProcAddress(module,"NtSetInformationFile") : nullptr;
    require(entry!=nullptr,"Exclusive restoration rename unavailable",ErrorCode::InvalidState);
    IO_STATUS_BLOCK result{};const auto set=std::bit_cast<Set>(entry);
    require(set(file.value,&result,&info,sizeof(info),static_cast<FILE_INFORMATION_CLASS>(10))==0,
        "Cannot exclusively restore missing asset");
#else
    struct stat named{},held{};
    require(fstatat(sourceDirectory.value,"media.partial",&named,AT_SYMLINK_NOFOLLOW)==0 && fstat(file.value,&held)==0 &&
        named.st_dev==held.st_dev && named.st_ino==held.st_ino && S_ISREG(named.st_mode),
        "Owned restoration staging name changed",ErrorCode::MediaMismatch);
    const std::string name(leaf);
    require(syscall(SYS_renameat2,sourceDirectory.value,"media.partial",destination.value,name.c_str(),RENAME_NOREPLACE)==0,
        "Cannot exclusively restore missing asset");
#endif
}
std::array<char,64> digest(const Handle &file,std::uint64_t extent,std::stop_token stop) {
    require(size(file)==extent,"Owned staged extent changed",ErrorCode::MediaMismatch);
    std::array<char,65536> buffer{};detail::Hash hash;
    for (std::uint64_t offset=0;offset<extent;) {
        const auto count=static_cast<std::size_t>(std::min<std::uint64_t>(buffer.size(),extent-offset));
        const auto bytes=std::span(buffer).first(count);read(file,offset,bytes,stop);hash.add(bytes);offset+=count;
    }
    require(size(file)==extent,"Owned staged extent changed",ErrorCode::MediaMismatch);poll(stop);return hash.finish();
}
void verifyRecord(const Handle &file,std::string_view expected,std::stop_token stop) {
    require(size(file)==expected.size(),"Owned intent/receipt extent changed",ErrorCode::MediaMismatch);
    std::array<char,mediaReceiptMaximumBytes> bytes{};read(file,0,std::span(bytes).first(expected.size()),stop);
    require(std::string_view(bytes.data(),expected.size())==expected && size(file)==expected.size(),
        "Owned intent/receipt changed",ErrorCode::MediaMismatch);
}
}
struct StagedMediaState {
    ResourceLease lease;
    ResourceLedger resources;
    Id operation;
    std::string relative;
    CheckedMediaBytes checked;
    StageDurability durability=StageDurability::FileFlushed;
    bool hasIntent=false,commitAttempted=false,committed=false,restoration=false;
    Handle parent,directory,file,intent;
    ApprovedDirectoryStamp parentBefore{},parentAfter{};
    StagedMediaState(ResourceLease l,ResourceLedger ledger,Id id,CheckedMediaBytes bytes)
        :lease(std::move(l)),resources(std::move(ledger)),operation(std::move(id)),relative(operation.str()+"/media.partial"),checked(bytes) {}
};
StagedMedia::StagedMedia(std::unique_ptr<StagedMediaState> state):state_(std::move(state)) {}
StagedMedia::StagedMedia(StagedMedia &&) noexcept=default;
StagedMedia::~StagedMedia()=default;
const Id &StagedMedia::operation() const { require(bool(state_),"Retired media stage",ErrorCode::InvalidState);return state_->operation; }
const std::string &StagedMedia::relativePath() const { operation();return state_->relative; }
CheckedMediaBytes StagedMedia::checkedBytes() const { operation();return state_->checked; }
StageDurability StagedMedia::durability() const { operation();return state_->durability; }
std::size_t StagedMedia::chargedBytes() const { operation();return state_->lease.bytes(); }
void StagedMedia::verifyRestorationParentTransition(ApprovedDirectoryStamp before,ApprovedDirectoryStamp current,
    CheckedMediaBytes expected,std::string_view planned,ResourceLedger memory,std::stop_token stop) const {
    operation();const auto &s=*state_;poll(stop);
    require(memory.owns(s.lease) && s.restoration && s.hasIntent && !s.commitAttempted && !s.committed,
        "No current unattempted restoration stage",ErrorCode::InvalidState);
    require(s.parentBefore==before && s.parentAfter==current && detail::nativeDirectoryStamp(s.parent.value)==s.parentAfter,
        "Owned staging parent transition changed",ErrorCode::MediaMismatch);
    require(s.checked.bytes==expected.bytes && s.checked.sha256==expected.sha256,
        "Owned restoration stage content differs",ErrorCode::MediaMismatch);
#ifndef _WIN32
    // Windows operation/file handles deny deletion and replacement. On Linux,
    // check current names against these exact owned descriptors as well.
    const auto named=[&](const Handle &parent,const char *name,const Handle &held,bool directory) {
        struct stat entry{},owner{};
        require(fstatat(parent.value,name,&entry,AT_SYMLINK_NOFOLLOW)==0 && fstat(held.value,&owner)==0 &&
            entry.st_dev==owner.st_dev && entry.st_ino==owner.st_ino &&
            (directory ? S_ISDIR(entry.st_mode) : S_ISREG(entry.st_mode) && owner.st_nlink==1),
            "Owned restoration stage name changed",ErrorCode::MediaMismatch);
    };
    named(s.parent,s.operation.str().c_str(),s.directory,true);
    named(s.directory,"intent.json",s.intent,false);named(s.directory,"media.partial",s.file,false);
#endif
    require(planned.size()<=mediaReceiptMaximumBytes,"Owned intent exceeds admitted audit bank",ErrorCode::InvalidState);
    auto scratch=memory.reserve(65536+8192+16384);
    verifyRecord(s.intent,planned,stop);
    require(digest(s.file,expected.bytes,stop)==expected.sha256,"Owned restoration staged bytes changed",ErrorCode::MediaMismatch);
    require(detail::nativeDirectoryStamp(s.parent.value)==s.parentAfter,"Owned staging parent changed during audit",ErrorCode::MediaMismatch);
    poll(stop);
}
StagedMedia stageMediaImpl(ApprovedMediaFile &source,const std::filesystem::path &parent,
    CheckedMediaBytes checked,std::uint64_t maximum,Id id,std::stop_token stop,const StageObserver &observer,const MediaProvenance *origin,bool restoration,const OwnedInspectionProtocol *restorationIntent,const ApprovedDirectoryStamp *expectedParent) {
    poll(stop);
    require(parent.is_absolute() && parent.native().size()<=4096 &&
        parent.native().find(std::filesystem::path::value_type(0))==parent.native().npos,
        "Destination must be an explicit absolute directory",ErrorCode::InvalidParameter);
    require(maximum>0 && maximum<=static_cast<std::uint64_t>(INT64_MAX) && checked.bytes<=maximum,
        "Invalid staging byte budget",ErrorCode::InvalidParameter);
    require(std::all_of(checked.sha256.begin(),checked.sha256.end(),[](char c){return (c>='0' && c<='9') || (c>='a' && c<='f');}),
        "Invalid checked media digest",ErrorCode::InvalidParameter);
    require(source.size()==checked.bytes,"Source size differs from checked selection",ErrorCode::MediaMismatch);
    const auto ledger=source.resourceLedger();
    auto state=std::make_unique<StagedMediaState>(ledger.reserve(sizeof(StagedMediaState)+1024),ledger,std::move(id),checked);
    state->restoration=restoration;
    auto scratch=ledger.reserve(65536+8192+16384); // transfer, crypto, native path/work allowance
    std::optional<OwnedInspectionProtocol> planned;
    if (origin) planned.emplace(encodeMediaProvenance(*origin,MediaReceiptPhase::Planned,ledger,stop));
    require(!restoration || (!origin && restorationIntent && restorationIntent->ownedBy(ledger) &&
        restorationIntent->bytes().size()<=mediaReceiptMaximumBytes),"Unadmitted restoration intent",ErrorCode::InvalidState);
    const auto *intentProtocol=origin ? &*planned : restorationIntent;
    require(source.digest(stop)==checked.sha256,"Source differs from checked selection",ErrorCode::MediaMismatch);
#ifdef _WIN32
    state->parent.value=CreateFileW(parent.c_str(),FILE_LIST_DIRECTORY|FILE_ADD_SUBDIRECTORY|FILE_READ_ATTRIBUTES|SYNCHRONIZE|
        (restoration ? FILE_ADD_FILE|FILE_TRAVERSE : 0UL),
        FILE_SHARE_READ|FILE_SHARE_WRITE|FILE_SHARE_DELETE,nullptr,OPEN_EXISTING,
        FILE_FLAG_BACKUP_SEMANTICS|FILE_FLAG_OPEN_REPARSE_POINT,nullptr);
    require(state->parent.value!=INVALID_HANDLE_VALUE,"Cannot open staging parent");plainDirectory(state->parent);
#else
    state->parent.value=::open(parent.c_str(),O_RDONLY|O_DIRECTORY|O_NOFOLLOW|O_CLOEXEC);
    require(state->parent.value>=0,"Cannot open plain staging parent");
#endif
    state->parentBefore=detail::nativeDirectoryStamp(state->parent.value);
    if(expectedParent)require(state->parentBefore==*expectedParent,"Saved staging destination differs from original audit",ErrorCode::MediaMismatch);
    boundary(observer,StageBoundary::BeforeDirectory,0,stop);
    if(expectedParent)require(detail::nativeDirectoryStamp(state->parent.value)==*expectedParent,
        "Saved staging destination changed before creation",ErrorCode::MediaMismatch);
    // Windows must share the held operation directory for handle-relative
    // receipt renames, just as for bound-media publication. DELETE stays denied.
    create(state->parent,state->operation.str(),true,state->directory,origin!=nullptr || restoration);
    if (intentProtocol) {
        const auto &intent=*intentProtocol;
        boundary(observer,StageBoundary::BeforeIntentWrite,0,stop);
        create(state->directory,"intent.json",false,state->intent);
        write(state->intent,0,std::span(intent.bytes().data(),intent.bytes().size()),stop);
        boundary(observer,StageBoundary::BeforeIntentFlush,0,stop);flush(state->intent);
        verifyRecord(state->intent,intent.bytes(),stop);
#ifndef _WIN32
        flush(state->directory);flush(state->parent);
#endif
        state->hasIntent=true;
    }
    boundary(observer,StageBoundary::BeforeFile,0,stop);
    create(state->directory,"media.partial",false,state->file,origin!=nullptr || restoration);
    std::array<char,65536> buffer{};
    {
        detail::Hash hash;
        for (std::uint64_t offset=0;offset<checked.bytes;) {
            const auto count=static_cast<std::size_t>(std::min<std::uint64_t>(buffer.size(),checked.bytes-offset));
            auto bytes=std::span(buffer).first(count);source.readAt(offset,bytes,stop);
            boundary(observer,StageBoundary::BeforeWrite,offset,stop);
            write(state->file,offset,bytes,stop);hash.add(bytes);offset+=count;
        }
        require(hash.finish()==checked.sha256,"Source changed during staging",ErrorCode::MediaMismatch);
    }
    boundary(observer,StageBoundary::BeforeFileFlush,checked.bytes,stop);flush(state->file);
    boundary(observer,StageBoundary::BeforeReadback,0,stop);
    require(size(state->file)==checked.bytes,"Staged extent differs",ErrorCode::MediaMismatch);
    {
        detail::Hash hash;
        for (std::uint64_t offset=0;offset<checked.bytes;) {
            const auto count=static_cast<std::size_t>(std::min<std::uint64_t>(buffer.size(),checked.bytes-offset));
            auto bytes=std::span(buffer).first(count);read(state->file,offset,bytes,stop);hash.add(bytes);offset+=count;
        }
        require(hash.finish()==checked.sha256 && size(state->file)==checked.bytes,
            "Staged bytes differ from checked selection",ErrorCode::MediaMismatch);
    }
    boundary(observer,StageBoundary::BeforeFinalSourceCheck,checked.bytes,stop);
    require(source.digest(stop)==checked.sha256,"Source changed after staging",ErrorCode::MediaMismatch);
    boundary(observer,StageBoundary::BeforeDirectoryFlush,checked.bytes,stop);
#ifndef _WIN32
    flush(state->directory);flush(state->parent);state->durability=StageDurability::FileAndDirectoriesFlushed;
#endif
    state->parentAfter=detail::nativeDirectoryStamp(state->parent.value);
    poll(stop);return StagedMedia(std::move(state));
}
StagedMedia stageVerifiedMedia(ApprovedMediaFile &source,const std::filesystem::path &parent,
    CheckedMediaBytes checked,std::uint64_t maximum,Id id,std::stop_token stop,const StageObserver &observer) {
    return stageMediaImpl(source,parent,checked,maximum,std::move(id),stop,observer,nullptr,false,nullptr,nullptr);
}
StagedMedia stageBoundMedia(ApprovedMediaFile &source,const std::filesystem::path &parent,
    const MediaProvenance &origin,std::uint64_t maximum,std::stop_token stop,const StageObserver &observer) {
    require(origin.ownedBy(source.resourceLedger()) && origin.data().phase==MediaReceiptPhase::Planned,
        "Unadmitted or non-planned media intent",ErrorCode::InvalidState);
    const auto &p=origin.data();return stageMediaImpl(source,parent,{p.audio.sourceBytes,p.audio.sourceSha256},maximum,
        p.operation,stop,observer,&origin,false,nullptr,nullptr);
}
MediaCommitOutcome commitStagedMedia(StagedMedia &owned,const MediaProvenance &origin,std::stop_token stop,const StageObserver &observer) {
    poll(stop);require(bool(owned.state_),"Retired media stage",ErrorCode::InvalidState);auto &state=*owned.state_;
    const auto &p=origin.data();require(origin.ownedBy(state.resources) && state.hasIntent && !state.commitAttempted &&
        p.phase==MediaReceiptPhase::Planned && p.operation==state.operation && p.audio.sourceBytes==state.checked.bytes &&
        p.audio.sourceSha256==state.checked.sha256,"Mismatched or already-attempted media commit",ErrorCode::InvalidState);
    auto scratch=state.resources.reserve(65536+8192+mediaReceiptMaximumBytes+16384);
    auto intent=encodeMediaProvenance(origin,MediaReceiptPhase::Planned,state.resources,stop);
    auto receipt=encodeMediaProvenance(origin,MediaReceiptPhase::Verified,state.resources,stop);
    // All mutable return storage is prepared before the irreversible publication.
    const auto finalRelative=state.operation.str()+"/media.wav";
    verifyRecord(state.intent,intent.bytes(),stop);
    require(digest(state.file,state.checked.bytes,stop)==state.checked.sha256,"Owned staged bytes changed",ErrorCode::MediaMismatch);
    state.commitAttempted=true;
    boundary(observer,StageBoundary::BeforeAssetRename,0,stop);
    state.durability=StageDurability::FileFlushed;
    renameExclusive(state.directory,state.file,"media.partial","media.wav");state.relative=finalRelative;
    boundary(observer,StageBoundary::BeforeReceiptWrite,0,stop);Handle pending;
    create(state.directory,"receipt.partial",false,pending,true);
    write(pending,0,std::span(receipt.bytes().data(),receipt.bytes().size()),stop);
    boundary(observer,StageBoundary::BeforeReceiptFlush,0,stop);flush(pending);verifyRecord(pending,receipt.bytes(),stop);
#ifndef _WIN32
    flush(state.directory);flush(state.parent);
#endif
    boundary(observer,StageBoundary::BeforeCommit,0,stop);
    // Recheck after trusted hooks and immediately before publication.
    verifyRecord(state.intent,intent.bytes(),stop);verifyRecord(pending,receipt.bytes(),stop);
    require(digest(state.file,state.checked.bytes,stop)==state.checked.sha256,"Owned media changed before publication",ErrorCode::MediaMismatch);
    poll(stop);renameExclusive(state.directory,pending,"receipt.partial","receipt.json");
    state.committed=true;MediaCommitOutcome outcome;outcome.published=true;outcome.receiptPublished=true;
    // Publication is now visible. Never throw cancellation/I/O as "not committed".
    try {
        if (observer) observer(StageBoundary::AfterCommitBeforeDirectoryFlush,state.checked.bytes);
#ifndef _WIN32
        flush(state.directory);flush(state.parent);outcome.durability=StageDurability::FileAndDirectoriesFlushed;
#endif
    } catch (...) {outcome.postCommitFlushFailed=true;}
    state.durability=outcome.durability;return outcome;
}
StagedMedia StagedMedia::prepareRestorationStage(ApprovedMediaFile &source,const std::filesystem::path &root,
    CheckedMediaBytes checked,std::uint64_t maximum,Id operation,const OwnedInspectionProtocol &intent,std::stop_token stop,const StageObserver &observer) {
    return stageMediaImpl(source,root,checked,maximum,std::move(operation),stop,observer,nullptr,true,&intent,nullptr);
}
StagedMedia StagedMedia::prepareSavedRestorationStage(ApprovedMediaFile &source,const std::filesystem::path &root,
    CheckedMediaBytes checked,std::uint64_t maximum,Id operation,const OwnedInspectionProtocol &intent,
    ApprovedDirectoryStamp before,std::stop_token stop,const StageObserver &observer) {
    return stageMediaImpl(source,root,checked,maximum,std::move(operation),stop,observer,nullptr,true,&intent,&before);
}
void StagedMedia::verifyRestorationPublicationNames(ResourceLedger memory,std::stop_token stop) const {
    operation();const auto &s=*state_;poll(stop);
    require(memory.owns(s.lease)&&s.restoration&&s.hasIntent&&s.commitAttempted&&!s.committed,"No current saved publication stage",ErrorCode::InvalidState);
#ifndef _WIN32
    const auto named=[&](const Handle &parent,const char *name,const Handle &held,bool directory) {
        struct stat entry{},owner{};
        require(fstatat(parent.value,name,&entry,AT_SYMLINK_NOFOLLOW)==0 && fstat(held.value,&owner)==0 &&
            entry.st_dev==owner.st_dev && entry.st_ino==owner.st_ino &&
            (directory?S_ISDIR(entry.st_mode):S_ISREG(entry.st_mode)&&owner.st_nlink==1),"Saved publication stage name changed",ErrorCode::MediaMismatch);
    };
    named(s.parent,s.operation.str().c_str(),s.directory,true);
    named(s.directory,"intent.json",s.intent,false);named(s.directory,"media.partial",s.file,false);
#endif
    poll(stop); // Windows held operation/file handles deny deletion/replacement.
}
MediaCommitOutcome StagedMedia::restoreMissing(std::string_view relative,const OwnedInspectionProtocol &intent,
    const OwnedInspectionProtocol &receipt,std::stop_token stop,const StageObserver &observer) {
    operation();auto &s=*state_;
    require(s.restoration && s.hasIntent && !s.commitAttempted && !s.committed,"Restoration stage already attempted",ErrorCode::InvalidState);
    s.commitAttempted=true;validateRelativeMediaPath(relative);
    require(relative.starts_with("media/") && relative.back()!='/',"Restoration requires an owned media path",ErrorCode::InvalidParameter);
    require(intent.ownedBy(s.resources) && receipt.ownedBy(s.resources) && intent.bytes().size()<=mediaReceiptMaximumBytes &&
        receipt.bytes().size()<=mediaReceiptMaximumBytes,"Unadmitted restoration records",ErrorCode::InvalidState);
    auto scratch=s.resources.reserve(65536+8192+16384);
    verifyRecord(s.intent,intent.bytes(),stop);
    std::array<Handle,64> directories;std::size_t count=0,begin=0;const Handle *parent=&s.parent;
    for (;;) {
        const auto end=relative.find('/',begin);const auto part=relative.substr(begin,end==relative.npos ? end : end-begin);
        require(!part.empty() && part.size()<=255 && count<directories.size(),"Restoration path limit",ErrorCode::InvalidParameter);
        if (end==relative.npos)break;
        poll(stop);restorationDirectory(*parent,part,directories[count]);parent=&directories[count++];begin=end+1;
    }
    boundary(observer,StageBoundary::BeforeReceiptWrite,s.checked.bytes,stop);Handle pending;
    create(s.directory,"receipt.partial",false,pending,true);
    write(pending,0,std::span(receipt.bytes().data(),receipt.bytes().size()),stop);
    boundary(observer,StageBoundary::BeforeReceiptFlush,s.checked.bytes,stop);flush(pending);
    verifyRecord(pending,receipt.bytes(),stop);
#ifndef _WIN32
    flush(s.directory);
#endif
    boundary(observer,StageBoundary::BeforeCommit,s.checked.bytes,stop);
    // Observer/cancellation work may have yielded. Reopen EVERY canonical parent
    // without creation and require the names to still select our exact handles.
    const Handle *namedParent=&s.parent;std::size_t index=0,partBegin=0;
    for(;;){const auto end=relative.find('/',partBegin);if(end==relative.npos)break;
        Handle named;restorationDirectory(*namedParent,relative.substr(partBegin,end-partBegin),named,false);
        const auto actual=detail::nativeDirectoryStamp(named.value),held=detail::nativeDirectoryStamp(directories[index].value);
        require(actual.identity[0]==held.identity[0]&&actual.identity[1]==held.identity[1],"Restoration destination parent changed",ErrorCode::MediaMismatch);
        namedParent=&directories[index++];partBegin=end+1;poll(stop);
    }
    require(index==count,"Restoration destination parent count changed",ErrorCode::InvalidState);
    verifyRecord(s.intent,intent.bytes(),stop);verifyRecord(pending,receipt.bytes(),stop);
    require(digest(s.file,s.checked.bytes,stop)==s.checked.sha256,"Restoration staged content changed",ErrorCode::MediaMismatch);
    flush(s.file);poll(stop);restoreRename(s.directory,s.file,*parent,relative.substr(begin));
    s.committed=true;MediaCommitOutcome outcome;outcome.published=true;
    try {
        if (observer)observer(StageBoundary::AfterCommitBeforeDirectoryFlush,s.checked.bytes);
        if (observer)observer(StageBoundary::BeforeRestorationReceiptPublish,s.checked.bytes);
        // The asset is already visible. Complete the receipt despite cancellation,
        // or report published=true/receiptPublished=false if this final step fails.
        renameExclusive(s.directory,pending,"receipt.partial","receipt.json");outcome.receiptPublished=true;
#ifndef _WIN32
        flush(s.directory);
        for (std::size_t i=count;i>0;--i)flush(directories[i-1]);
        flush(s.parent);outcome.durability=StageDurability::FileAndDirectoriesFlushed;
#endif
    } catch (...) {outcome.postCommitFlushFailed=true;}
    s.durability=outcome.durability;return outcome;
}
} // namespace soundcurrent::daw
