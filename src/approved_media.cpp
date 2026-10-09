// SPDX-License-Identifier: GPL-3.0-only
#include <soundcurrent/approved_media.hpp>
#include "media_hash.hpp"
#include <algorithm>
#include <bit>
#include <cstring>
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <winternl.h>
#include <bcrypt.h>
#else
#include <cerrno>
#include <fcntl.h>
#include <linux/openat2.h>
#include <openssl/evp.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <unistd.h>
#endif
namespace soundcurrent::daw {
namespace {
void require(bool ok,const char *message,ErrorCode code=ErrorCode::Io) {
    if (!ok) throw ProjectError(code,message);
}
void poll(std::stop_token stop) {
    require(!stop.stop_requested(),"Approved media operation canceled",ErrorCode::Canceled);
}
struct Handle {
#ifdef _WIN32
    HANDLE value=INVALID_HANDLE_VALUE;
    void close() noexcept { if (value!=INVALID_HANDLE_VALUE) CloseHandle(value); value=INVALID_HANDLE_VALUE; }
#else
    int value=-1;
    void close() noexcept { if (value>=0) ::close(value); value=-1; }
#endif
    ~Handle() { close(); }
    Handle()=default;
    Handle(const Handle &)=delete;
};
struct Identity {
    std::uint64_t volume=0,object=0,size=0;
    std::int64_t modified=0,modifiedExtra=0,changed=0,changedExtra=0;
    bool operator==(const Identity &) const=default;
};
Identity information(const Handle &file) {
#ifdef _WIN32
    BY_HANDLE_FILE_INFORMATION info{}; FILE_BASIC_INFO basic{};
    require(GetFileType(file.value)==FILE_TYPE_DISK && GetFileInformationByHandle(file.value,&info) &&
            GetFileInformationByHandleEx(file.value,FileBasicInfo,&basic,sizeof(basic)),"Cannot inspect approved media handle");
    require(!(info.dwFileAttributes&(FILE_ATTRIBUTE_DIRECTORY|FILE_ATTRIBUTE_REPARSE_POINT)),
            "Approved media is not a plain file",ErrorCode::InvalidParameter);
    return {info.dwVolumeSerialNumber,(std::uint64_t(info.nFileIndexHigh)<<32)|info.nFileIndexLow,
            (std::uint64_t(info.nFileSizeHigh)<<32)|info.nFileSizeLow,
            basic.LastWriteTime.QuadPart,0,basic.ChangeTime.QuadPart,0};
#else
    struct stat info{};
    require(fstat(file.value,&info)==0,"Cannot inspect approved media handle");
    require(S_ISREG(info.st_mode) && info.st_size>=0,"Approved media is not a plain file",ErrorCode::InvalidParameter);
    return {static_cast<std::uint64_t>(info.st_dev),static_cast<std::uint64_t>(info.st_ino),
            static_cast<std::uint64_t>(info.st_size),info.st_mtim.tv_sec,info.st_mtim.tv_nsec,
            info.st_ctim.tv_sec,info.st_ctim.tv_nsec};
#endif
}
void portableReference(std::string_view p) {
    require(!p.empty() && p.size()<=1024 && validUtf8(p) && p.front()!='/' &&
            p.find_first_of("\\:\0",0,3)==p.npos,"Media reference needs an explicit portable relative mapping",ErrorCode::InvalidParameter);
    std::size_t from=0,components=0;
    while (from<p.size()) {
        const auto end=p.find('/',from)==p.npos ? p.size() : p.find('/',from);
        const auto part=p.substr(from,end-from);
        require(!part.empty() && part.size()<=255 && part!="." && part!=".." && part.back()!='.' &&
                part.back()!=' ' && ++components<=64,"Unsafe media reference component",ErrorCode::InvalidParameter);
        std::array<char,256> base{};
        for (unsigned char c:part) {
            require(c>=32 && c!=127 && c!='<' && c!='>' && c!='"' && c!='|' && c!='?' && c!='*',
                    "Nonportable media reference",ErrorCode::InvalidParameter);
        }
        const auto prefixLength=part.find('.')==part.npos ? part.size() : part.find('.');
        for (std::size_t i=0;i<prefixLength;++i) {
            const auto c=static_cast<unsigned char>(part[i]);
            base[i]=c>='a' && c<='z' ? char(c-32) : char(c);
        }
        const std::string_view prefix(base.data(),prefixLength);
        const bool numbered=(prefix.starts_with("COM") || prefix.starts_with("LPT")) &&
            ((prefix.size()==4 && prefix[3]>='1' && prefix[3]<='9') ||
             (prefix.size()==5 && (prefix.substr(3)=="\xc2\xb9" || prefix.substr(3)=="\xc2\xb2" || prefix.substr(3)=="\xc2\xb3")));
        require(prefix!="CON" && prefix!="PRN" && prefix!="AUX" && prefix!="NUL" && !numbered,
                "Reserved media filename",ErrorCode::InvalidParameter);
        from=end+1;
    }
    require(p.back()!='/',"Media reference names a directory",ErrorCode::InvalidParameter);
}
#ifndef _WIN32
void openFailure(int error) {
    if (error==ENOENT || error==ENOTDIR) throw ProjectError(ErrorCode::MissingMedia,"Approved media reference is missing");
    if (error==ELOOP || error==EXDEV) throw ProjectError(ErrorCode::InvalidParameter,"Linked or mounted media reference refused");
    if (error==ENOSYS || error==EINVAL) throw ProjectError(ErrorCode::InvalidState,"Contained media opening unavailable; no pathname fallback");
    throw ProjectError(ErrorCode::Io,"Cannot open approved media reference");
}
#endif
using detail::Hash;
} // namespace
struct ApprovedMediaRootState {
    ResourceLease lease;
    ResourceLedger resources;
    ApprovedMediaLimits limits;
    std::size_t activeFiles=0;
    Handle root;
    ApprovedMediaRootState(ResourceLease l,ResourceLedger r,ApprovedMediaLimits limits_)
        :lease(std::move(l)),resources(std::move(r)),limits(limits_) {}
};
struct ApprovedMediaFileState {
    ResourceLease lease;
    std::shared_ptr<ApprovedMediaRootState> root;
    Handle file;
    Identity initial;
    ApprovedMediaFileState(ResourceLease l,std::shared_ptr<ApprovedMediaRootState> r)
        :lease(std::move(l)),root(std::move(r)) { ++root->activeFiles; }
    ~ApprovedMediaFileState() { file.close(); --root->activeFiles; }
};
ApprovedMediaRoot::ApprovedMediaRoot(const std::filesystem::path &path,ResourceLedger resources,ApprovedMediaLimits limits) {
    require(path.is_absolute() && path.native().size()<=4096 &&
            path.native().find(std::filesystem::path::value_type(0))==path.native().npos,
            "Approved root must be an explicit absolute directory",ErrorCode::InvalidParameter);
    require(limits.maximumOpenFiles>0 && limits.maximumOpenFiles<=4096,"Invalid approved media handle limit",ErrorCode::InvalidParameter);
    auto grant=resources.reserve(sizeof(ApprovedMediaRootState)+sizeof(ApprovedMediaRoot)+256);
    state_=std::make_shared<ApprovedMediaRootState>(std::move(grant),resources,limits);
#ifdef _WIN32
    state_->root.value=CreateFileW(path.c_str(),FILE_LIST_DIRECTORY|FILE_TRAVERSE|FILE_READ_ATTRIBUTES|SYNCHRONIZE,
        FILE_SHARE_READ|FILE_SHARE_WRITE|FILE_SHARE_DELETE,nullptr,OPEN_EXISTING,
        FILE_FLAG_BACKUP_SEMANTICS|FILE_FLAG_OPEN_REPARSE_POINT,nullptr);
    require(state_->root.value!=INVALID_HANDLE_VALUE,"Cannot open approved media directory");
    FILE_ATTRIBUTE_TAG_INFO info{};
    require(GetFileInformationByHandleEx(state_->root.value,FileAttributeTagInfo,&info,sizeof(info)) &&
            (info.FileAttributes&FILE_ATTRIBUTE_DIRECTORY) && !(info.FileAttributes&FILE_ATTRIBUTE_REPARSE_POINT),
            "Approved media root is not a plain directory",ErrorCode::InvalidParameter);
#else
    state_->root.value=::open(path.c_str(),O_PATH|O_DIRECTORY|O_NOFOLLOW|O_CLOEXEC);
    require(state_->root.value>=0,"Cannot open approved media directory");
#endif
}
ApprovedMediaRoot::ApprovedMediaRoot(ApprovedMediaRoot &&) noexcept=default;
ApprovedMediaRoot::~ApprovedMediaRoot()=default;
std::size_t ApprovedMediaRoot::openFiles() const { require(bool(state_),"Retired approved root",ErrorCode::InvalidState);return state_->activeFiles; }
ApprovedMediaFile ApprovedMediaRoot::open(std::string_view relative,std::uint64_t maximumBytes,std::stop_token stop) const {
    return openImpl(relative,maximumBytes,stop,false);
}
ApprovedMediaFile ApprovedMediaRoot::openSelectedFilename(const std::filesystem::path &basename,std::uint64_t maximumBytes,std::stop_token stop) const {
    require(!basename.empty() && !basename.has_parent_path() && !basename.has_root_name() && basename.filename()==basename &&
        basename!="." && basename!="..","Explicit media selection must name one native file",ErrorCode::InvalidParameter);
    const auto bytes=basename.u8string();
    return openImpl(std::string_view(reinterpret_cast<const char *>(bytes.data()),bytes.size()),maximumBytes,stop,true);
}
ApprovedMediaFile ApprovedMediaRoot::openImpl(std::string_view relative,std::uint64_t maximumBytes,std::stop_token stop,bool selectedLeaf) const {
    poll(stop);require(bool(state_),"Retired approved root",ErrorCode::InvalidState);
    auto work=state_->resources.reserve(8192); // Bounded path/native-open scratch, retired before return.
    if (!selectedLeaf) portableReference(relative);
    else {
        require(!relative.empty() && relative.size()<=1024 && validUtf8(relative) && relative.find('\0')==relative.npos &&
            relative.find('/')==relative.npos,"Invalid explicit native filename",ErrorCode::InvalidParameter);
#ifdef _WIN32
        require(relative.find_first_of("\\:")==relative.npos,"Native selection cannot name a path or alternate stream",ErrorCode::InvalidParameter);
#endif
    }
    require(maximumBytes>0,"Invalid approved media byte limit",ErrorCode::InvalidParameter);
    if (state_->activeFiles>=state_->limits.maximumOpenFiles)
        throw ResourceLimitError("Approved media handles",state_->activeFiles+1,state_->limits.maximumOpenFiles);
    auto file=std::make_unique<ApprovedMediaFileState>(state_->resources.reserve(sizeof(ApprovedMediaFileState)+256),state_);
#ifdef _WIN32
    std::array<wchar_t,1025> name{};
    const auto count=MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,relative.data(),static_cast<int>(relative.size()),name.data(),1024);
    require(count>0,"Cannot encode approved relative path",ErrorCode::InvalidParameter);
    for (int i=0;i<count;++i) if (name[static_cast<std::size_t>(i)]==L'/') name[static_cast<std::size_t>(i)]=L'\\';
    UNICODE_STRING unicode{};unicode.Buffer=name.data();unicode.Length=static_cast<USHORT>(count*sizeof(wchar_t));unicode.MaximumLength=unicode.Length;
    OBJECT_ATTRIBUTES attributes{};attributes.Length=sizeof(attributes);attributes.RootDirectory=state_->root.value;attributes.ObjectName=&unicode;
    // OBJ_DONT_REPARSE, with the OS/directory's default case rules. Do not
    // request case folding: an approved NTFS subtree can be case-sensitive.
    attributes.Attributes=0x1000UL;
    using Open=NTSTATUS (NTAPI *)(PHANDLE,ACCESS_MASK,POBJECT_ATTRIBUTES,PIO_STATUS_BLOCK,ULONG,ULONG);
    const auto module=GetModuleHandleW(L"ntdll.dll");
    const auto entry=module ? GetProcAddress(module,"NtOpenFile") : nullptr;
    require(entry!=nullptr,"Contained media opening unavailable; no pathname fallback",ErrorCode::InvalidState);
    const auto openFile=std::bit_cast<Open>(entry);IO_STATUS_BLOCK result{};
    // Synchronous read, non-directory, open reparse point rather than follow it.
    const auto status=openFile(&file->file.value,GENERIC_READ|SYNCHRONIZE,&attributes,&result,FILE_SHARE_READ,0x20UL|0x40UL|0x200000UL);
    if (status!=0) {
        const auto code=static_cast<std::uint32_t>(status);
        if (code==0xc0000034U || code==0xc000003aU) throw ProjectError(ErrorCode::MissingMedia,"Approved media reference is missing");
        if (code==0xc000050bU || code==0xc00000baU || code==0xc0000279U)
            throw ProjectError(ErrorCode::InvalidParameter,"Linked or non-file media reference refused");
        throw ProjectError(ErrorCode::Io,"Cannot open approved media reference; native status="+std::to_string(code));
    }
#else
    std::array<char,1025> name{};std::copy(relative.begin(),relative.end(),name.begin());
    open_how how{};how.flags=O_PATH|O_CLOEXEC|O_NOFOLLOW;
    how.resolve=RESOLVE_BENEATH|RESOLVE_NO_SYMLINKS|RESOLVE_NO_MAGICLINKS|RESOLVE_NO_XDEV;
    Handle pin;pin.value=static_cast<int>(syscall(SYS_openat2,state_->root.value,name.data(),&how,sizeof(how)));
    if (pin.value<0) openFailure(errno);
    const auto pinned=information(pin); // Class/size before opening for I/O.
    if (pinned.size>maximumBytes) throw ResourceLimitError("Approved media bytes",static_cast<std::size_t>(pinned.size),static_cast<std::size_t>(maximumBytes));
    // Reopen only our pinned plain inode, never the mutable foreign path. This
    // requires the OS proc descriptor facility and has no pathname fallback.
    const auto descriptor="/proc/self/fd/"+std::to_string(pin.value);
    file->file.value=::open(descriptor.c_str(),O_RDONLY|O_NONBLOCK|O_CLOEXEC);
    require(file->file.value>=0,"Cannot read pinned approved media descriptor");
    require(information(file->file)==pinned,"Approved media changed before read admission",ErrorCode::MediaMismatch);
#endif
    file->initial=information(file->file);
    if (file->initial.size>maximumBytes)
        throw ResourceLimitError("Approved media bytes",static_cast<std::size_t>(file->initial.size),static_cast<std::size_t>(maximumBytes));
    poll(stop);return ApprovedMediaFile(std::move(file));
}
ApprovedMediaFile::ApprovedMediaFile(std::unique_ptr<ApprovedMediaFileState> state):state_(std::move(state)) {}
ApprovedMediaFile::ApprovedMediaFile(ApprovedMediaFile &&) noexcept=default;
ApprovedMediaFile::~ApprovedMediaFile()=default;
std::uint64_t ApprovedMediaFile::size() const { require(bool(state_),"Retired approved media",ErrorCode::InvalidState);return state_->initial.size; }
std::size_t ApprovedMediaFile::chargedBytes() const { require(bool(state_),"Retired approved media",ErrorCode::InvalidState);return state_->lease.bytes(); }
ResourceLedger ApprovedMediaFile::resourceLedger() const { require(bool(state_),"Retired approved media",ErrorCode::InvalidState);return state_->root->resources; }
void ApprovedMediaFile::verifyUnchanged() const { require(bool(state_),"Retired approved media",ErrorCode::InvalidState);require(information(state_->file)==state_->initial,"Approved media changed during reading",ErrorCode::MediaMismatch); }
void ApprovedMediaFile::readAt(std::uint64_t offset,std::span<char> target,std::stop_token stop) {
    poll(stop);const auto length=size();require(target.size()<=65536 && offset<=length && target.size()<=length-offset,
        "Approved media read exceeds admitted extent/chunk",ErrorCode::InvalidParameter);verifyUnchanged();
#ifdef _WIN32
    LARGE_INTEGER position{};position.QuadPart=static_cast<LONGLONG>(offset);
    require(SetFilePointerEx(state_->file.value,position,nullptr,FILE_BEGIN),"Cannot seek approved media");
#endif
    std::size_t used=0;
    while (used<target.size()) {
        poll(stop);
#ifdef _WIN32
        DWORD count=0;require(ReadFile(state_->file.value,target.data()+used,static_cast<DWORD>(target.size()-used),&count,nullptr),"Approved media read failed");
#else
        const auto count=pread(state_->file.value,target.data()+used,target.size()-used,static_cast<off_t>(offset+used));
        if (count<0 && errno==EINTR) continue;
        require(count>=0,"Approved media read failed");
#endif
        require(count>0,"Approved media truncated during reading",ErrorCode::MediaMismatch);used+=static_cast<std::size_t>(count);
    }
    verifyUnchanged();poll(stop);
}
std::array<char,64> ApprovedMediaFile::digest(std::stop_token stop,const std::function<void()> &beforeChunk) {
    poll(stop);require(bool(state_),"Retired approved media",ErrorCode::InvalidState);
    auto scratch=state_->root->resources.reserve(65536+8192); // Buffer and conservative crypto workspace.
    std::array<char,65536> buffer{};Hash hash;verifyUnchanged();
    for (std::uint64_t offset=0;offset<size();) {
        poll(stop);if (beforeChunk) beforeChunk();poll(stop);
        const auto n=static_cast<std::size_t>(std::min<std::uint64_t>(buffer.size(),size()-offset));
        readAt(offset,std::span(buffer).first(n),stop);hash.add(std::span(buffer).first(n));offset+=n;
    }
    verifyUnchanged();poll(stop);return hash.finish();
}
} // namespace soundcurrent::daw
