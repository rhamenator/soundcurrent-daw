// SPDX-License-Identifier: GPL-3.0-only
#include <soundcurrent/media_staging.hpp>
#include "media_hash.hpp"
#include <algorithm>
#include <bit>
#ifdef _WIN32
#include <winternl.h>
#else
#include <cerrno>
#include <fcntl.h>
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
void create(const Handle &parent,std::string_view leaf,bool directory,Handle &out) {
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
    const auto access=directory ? FILE_LIST_DIRECTORY|FILE_ADD_FILE|FILE_READ_ATTRIBUTES|SYNCHRONIZE
        : GENERIC_READ|GENERIC_WRITE|SYNCHRONIZE;
    const auto status=createFile(&out.value,access,&attributes,&result,nullptr,
        directory ? FILE_ATTRIBUTE_DIRECTORY : FILE_ATTRIBUTE_NORMAL,0,2,options,nullptr,0);
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
void create(const Handle &parent,std::string_view leaf,bool directory,Handle &out) {
    const std::string name(leaf);
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
}
struct StagedMediaState {
    ResourceLease lease;
    Id operation;
    std::string relative;
    CheckedMediaBytes checked;
    StageDurability durability=StageDurability::FileFlushed;
    Handle parent,directory,file;
    StagedMediaState(ResourceLease l,Id id,CheckedMediaBytes bytes)
        :lease(std::move(l)),operation(std::move(id)),relative(operation.str()+"/media.partial"),checked(bytes) {}
};
StagedMedia::StagedMedia(std::unique_ptr<StagedMediaState> state):state_(std::move(state)) {}
StagedMedia::StagedMedia(StagedMedia &&) noexcept=default;
StagedMedia::~StagedMedia()=default;
const Id &StagedMedia::operation() const { require(bool(state_),"Retired media stage",ErrorCode::InvalidState);return state_->operation; }
const std::string &StagedMedia::relativePath() const { operation();return state_->relative; }
CheckedMediaBytes StagedMedia::checkedBytes() const { operation();return state_->checked; }
StageDurability StagedMedia::durability() const { operation();return state_->durability; }
std::size_t StagedMedia::chargedBytes() const { operation();return state_->lease.bytes(); }
StagedMedia stageVerifiedMedia(ApprovedMediaFile &source,const std::filesystem::path &parent,
    CheckedMediaBytes checked,std::uint64_t maximum,Id id,std::stop_token stop,const StageObserver &observer) {
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
    auto state=std::make_unique<StagedMediaState>(ledger.reserve(sizeof(StagedMediaState)+1024),std::move(id),checked);
    auto scratch=ledger.reserve(65536+8192+16384); // transfer, crypto, native path/work allowance
    require(source.digest(stop)==checked.sha256,"Source differs from checked selection",ErrorCode::MediaMismatch);
#ifdef _WIN32
    state->parent.value=CreateFileW(parent.c_str(),FILE_LIST_DIRECTORY|FILE_ADD_SUBDIRECTORY|FILE_READ_ATTRIBUTES|SYNCHRONIZE,
        FILE_SHARE_READ|FILE_SHARE_WRITE|FILE_SHARE_DELETE,nullptr,OPEN_EXISTING,
        FILE_FLAG_BACKUP_SEMANTICS|FILE_FLAG_OPEN_REPARSE_POINT,nullptr);
    require(state->parent.value!=INVALID_HANDLE_VALUE,"Cannot open staging parent");plainDirectory(state->parent);
#else
    state->parent.value=::open(parent.c_str(),O_RDONLY|O_DIRECTORY|O_NOFOLLOW|O_CLOEXEC);
    require(state->parent.value>=0,"Cannot open plain staging parent");
#endif
    boundary(observer,StageBoundary::BeforeDirectory,0,stop);
    create(state->parent,state->operation.str(),true,state->directory);
    boundary(observer,StageBoundary::BeforeFile,0,stop);
    create(state->directory,"media.partial",false,state->file);
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
    poll(stop);return StagedMedia(std::move(state));
}
} // namespace soundcurrent::daw
