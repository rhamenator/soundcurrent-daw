// SPDX-License-Identifier: GPL-3.0-only
#include <soundcurrent/inspection_bundle.hpp>
#include <algorithm>
#include <bit>
#include <cstring>
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <winternl.h>
#else
#include <cerrno>
#include <fcntl.h>
#include <unistd.h>
#endif

namespace soundcurrent::daw {
namespace {
constexpr std::string_view magic="SCIBND01";
constexpr std::size_t headerBytes=32,footerBytes=64;
void check(bool value,const char *message,ErrorCode code=ErrorCode::InvalidState) {
    if (!value) throw ProjectError(code,message);
}
void poll(std::stop_token stop) {
    if (stop.stop_requested()) throw ProjectError(ErrorCode::Canceled,"Inspection bundle canceled");
}
void put64(char *to,std::uint64_t number) {
    for (unsigned n=0;n<8;++n) { to[n]=char(number&255); number>>=8; }
}
std::size_t get64(const char *from) {
    std::uint64_t number=0;
    for (unsigned n=0;n<8;++n) number|=std::uint64_t(static_cast<unsigned char>(from[n]))<<(8*n);
    check(number<=std::numeric_limits<std::size_t>::max(),"Bundle integer outside host range");
    return static_cast<std::size_t>(number);
}
std::size_t totalBytes(std::size_t source,std::size_t protocol,std::size_t maximum) {
    PayloadCharge bytes("Inspection bundle bytes",maximum);
    bytes.add(headerBytes+footerBytes); bytes.add(source); bytes.add(protocol); return bytes.bytes();
}
// Publication remains tied to the open file and selected directory handles.
// No cleanup through a mutable temporary path or recursive directory removal.
class NewFile {
  public:
    explicit NewFile(const std::filesystem::path &requested) {
        check(!requested.empty(),"Empty inspection destination",ErrorCode::InvalidParameter);
        check(requested.native().find(std::filesystem::path::value_type(0))==std::filesystem::path::string_type::npos,
              "NUL in inspection destination refused",ErrorCode::InvalidParameter);
#ifdef _WIN32
        // Check the supplied final component before GetFullPathName-style
        // normalization can trim dots/spaces or turn a device name into an alias.
        check(!requested.has_root_name() || requested.has_root_directory(),
              "Drive-relative inspection destination refused",ErrorCode::InvalidParameter);
        const auto rawName=requested.filename().native();
        check(!rawName.empty(),"Empty inspection destination name",ErrorCode::InvalidParameter);
        check(rawName.find_first_of(L"<>:\"/\\|?*")==std::wstring::npos &&
              std::none_of(rawName.begin(),rawName.end(),[](wchar_t value){return value>0 && value<32;}),
              "Reserved Windows destination character/stream refused",ErrorCode::InvalidParameter);
        check(rawName.back()!=L'.' && rawName.back()!=L' ',"Nonportable Windows destination name refused",ErrorCode::InvalidParameter);
        auto prefix=rawName.substr(0,rawName.find(L'.'));
        while (!prefix.empty() && prefix.back()==L' ') prefix.pop_back();
        for (auto &letter:prefix) if (letter>=L'a' && letter<=L'z') letter=wchar_t(letter-L'a'+L'A');
        const bool device=prefix==L"CON" || prefix==L"PRN" || prefix==L"AUX" || prefix==L"NUL" ||
            (prefix.size()==4 && (prefix.substr(0,3)==L"COM" || prefix.substr(0,3)==L"LPT") &&
             ((prefix[3]>=L'1' && prefix[3]<=L'9') || prefix[3]==L'\u00b9' || prefix[3]==L'\u00b2' || prefix[3]==L'\u00b3'));
        check(!device,"Reserved Windows destination name refused",ErrorCode::InvalidParameter);
#endif
        const auto absolute=std::filesystem::absolute(requested);
        const auto name=absolute.filename().native();
        check(!requested.empty() && !name.empty() && name!=std::filesystem::path(".").native() &&
              name!=std::filesystem::path("..").native() &&
              absolute.native().find(std::filesystem::path::value_type(0))==std::filesystem::path::string_type::npos,
              "Invalid inspection destination",ErrorCode::InvalidParameter);
        destination_=absolute.filename();
#ifdef _WIN32
        check(name==rawName,"Normalized Windows destination name differs",ErrorCode::InvalidParameter);
        const auto temporary=absolute.parent_path()/std::filesystem::path(".sc-inspection-"+Id::generate().str()+".partial");
        parent_=CreateFileW(absolute.parent_path().c_str(),FILE_READ_ATTRIBUTES|FILE_TRAVERSE,
            FILE_SHARE_READ|FILE_SHARE_WRITE,nullptr,OPEN_EXISTING,FILE_FLAG_BACKUP_SEMANTICS,nullptr);
        check(parent_!=INVALID_HANDLE_VALUE,"Cannot open inspection destination directory",ErrorCode::Io);
        BY_HANDLE_FILE_INFORMATION info{};
        if (!GetFileInformationByHandle(parent_,&info) || !(info.dwFileAttributes&FILE_ATTRIBUTE_DIRECTORY)) {
            CloseHandle(parent_); parent_=INVALID_HANDLE_VALUE;
            throw ProjectError(ErrorCode::Io,"Inspection destination parent is not a directory");
        }
        file_=CreateFileW(temporary.c_str(),GENERIC_WRITE|DELETE,0,nullptr,CREATE_NEW,FILE_ATTRIBUTE_NORMAL,nullptr);
        if (file_==INVALID_HANDLE_VALUE) {
            CloseHandle(parent_); parent_=INVALID_HANDLE_VALUE;
            throw ProjectError(ErrorCode::Io,"Cannot create owned inspection temporary file");
        }
#else
        parent_=open(absolute.parent_path().c_str(),O_RDONLY|O_DIRECTORY|O_CLOEXEC);
        check(parent_>=0,"Cannot open inspection destination directory",ErrorCode::Io);
        file_=openat(parent_,".",O_TMPFILE|O_WRONLY|O_CLOEXEC,0600);
        if (file_<0) {
            close(parent_); parent_=-1;
            throw ProjectError(ErrorCode::Io,"Destination does not support unnamed inspection temporary files");
        }
#endif
    }
    NewFile(const NewFile &)=delete;
    NewFile &operator=(const NewFile &)=delete;
    ~NewFile() {
#ifdef _WIN32
        if (file_!=INVALID_HANDLE_VALUE) {
            if (!published_) {
                FILE_DISPOSITION_INFO disposition{}; disposition.DeleteFile=TRUE;
                SetFileInformationByHandle(file_,FileDispositionInfo,&disposition,sizeof(disposition));
            }
            CloseHandle(file_);
        }
        if (parent_!=INVALID_HANDLE_VALUE) CloseHandle(parent_);
#else
        if (file_>=0) close(file_);
        if (parent_>=0) close(parent_);
#endif
    }
    void write(std::string_view bytes,std::stop_token stop) {
        for (std::size_t position=0;position<bytes.size();) {
            poll(stop);
            const auto count=std::min<std::size_t>(65536,bytes.size()-position);
#ifdef _WIN32
            DWORD written=0;
            check(WriteFile(file_,bytes.data()+position,static_cast<DWORD>(count),&written,nullptr) && written,
                  "Cannot write inspection temporary file",ErrorCode::Io);
            position+=written;
#else
            const auto written=::write(file_,bytes.data()+position,count);
            if (written<0 && errno==EINTR) continue;
            check(written>0,"Cannot write inspection temporary file",ErrorCode::Io);
            position+=static_cast<std::size_t>(written);
#endif
        }
    }
    void flush() {
#ifdef _WIN32
        check(FlushFileBuffers(file_)!=0,"Cannot flush inspection file",ErrorCode::Io);
#else
        check(fsync(file_)==0,"Cannot flush inspection file",ErrorCode::Io);
#endif
    }
    Durability publish() {
#ifdef _WIN32
        const auto name=destination_.native();
        const auto length=name.size()*sizeof(wchar_t);
        // Original layout of documented FILE_RENAME_INFORMATION; the desktop
        // SDK's winternl.h does not expose this structure on every toolchain.
        struct Rename { BOOLEAN replace; HANDLE root; ULONG length; WCHAR name[1]; };
        alignas(Rename) std::array<std::byte,4096> bank{};
        check(length<=bank.size()-sizeof(Rename),"Inspection destination name too long",ErrorCode::InvalidParameter);
        auto *rename=reinterpret_cast<Rename *>(bank.data());
        rename->replace=FALSE; rename->root=parent_; rename->length=static_cast<ULONG>(length);
        std::memcpy(rename->name,name.data(),length);
        // The initial hosted Win32 relative-root call failed. Use the documented
        // native relative-root contract without resolving a mutable full path.
        // ntdll is already loaded by Windows; no DLL path/search is performed.
        using RenameFunction=NTSTATUS (NTAPI *)(HANDLE,PIO_STATUS_BLOCK,PVOID,ULONG,FILE_INFORMATION_CLASS);
        const auto module=GetModuleHandleW(L"ntdll.dll");
        const auto entry=module ? GetProcAddress(module,"NtSetInformationFile") : nullptr;
        check(entry!=nullptr,"Windows native rename unavailable",ErrorCode::Io);
        const auto renameFile=std::bit_cast<RenameFunction>(entry);
        IO_STATUS_BLOCK completion{};
        // CreateFileW above opens a synchronous file: the buffer/handles remain
        // owned until this synchronous call returns its completion status.
        const auto status=renameFile(file_,&completion,rename,static_cast<ULONG>(sizeof(Rename)+length),
                                    static_cast<FILE_INFORMATION_CLASS>(10));
        if (status!=0) throw ProjectError(ErrorCode::Io,
            "Cannot publish new inspection file; native status="+std::to_string(static_cast<std::uint32_t>(status)));
        published_=true; return Durability::FileFlushed;
#else
        const auto descriptor="/proc/self/fd/"+std::to_string(file_);
        check(linkat(AT_FDCWD,descriptor.c_str(),parent_,destination_.c_str(),AT_SYMLINK_FOLLOW)==0,
              "Cannot publish new inspection file; destination may exist or publication is unsupported",ErrorCode::Io);
        published_=true;
        return fsync(parent_)==0 ? Durability::FileAndDirectoryFlushed : Durability::FileFlushed;
#endif
    }
  private:
    std::filesystem::path destination_;
    bool published_=false;
#ifdef _WIN32
    HANDLE parent_=INVALID_HANDLE_VALUE,file_=INVALID_HANDLE_VALUE;
#else
    int parent_=-1,file_=-1;
#endif
};
}
InspectionBundleSaveResult saveInspectionBundle(const std::filesystem::path &path,
    const ImportInspectionReport &report,ResourceLedger memory,InspectionBundleLimits limits,
    const InspectionBundleSaveOptions &options) {
    poll(options.stop);
    check(report.ownedBy(memory),"Inspection result belongs to another memory scope");
    check(report.source().size()<=limits.sourceBytes && report.protocol().size()<=limits.protocolBytes &&
          !report.protocol().empty(),"Inspection bundle exceeds configured limits",ErrorCode::ResourceLimit);
    auto work=memory.reserve(65536); // Before hashing, file handles or temporary writes.
    const auto digest=hashForeignSnapshot(report.protocol(),memory,options.stop);
    const auto size=totalBytes(report.source().size(),report.protocol().size(),std::numeric_limits<std::size_t>::max());
    std::array<char,headerBytes> header{}; std::copy(magic.begin(),magic.end(),header.begin());
    put64(header.data()+8,report.source().size()); put64(header.data()+16,report.protocol().size());
    put64(header.data()+24,report.workerPid());
    NewFile file(path); std::size_t written=0;
    for (const auto chunk:{std::string_view(header.data(),header.size()),report.source(),report.protocol(),
                           std::string_view(digest.data(),digest.size())}) {
        file.write(chunk,options.stop); written+=chunk.size();
        if (options.afterWrite) options.afterWrite(written);
    }
    file.flush();
    if (options.beforePublish) options.beforePublish();
    poll(options.stop); // Publication is the point of no cancellation rollback.
    return {size,file.publish()};
}
ImportInspectionReport loadInspectionBundle(const std::filesystem::path &path,
    ResourceLedger memory,InspectionBundleLimits limits,std::stop_token stop) {
    const auto maximum=totalBytes(limits.sourceBytes,limits.protocolBytes,std::numeric_limits<std::size_t>::max());
    auto container=readForeignSnapshot(path,maximum,memory,stop);
    const auto bytes=container.bytes();
    check(bytes.size()>=headerBytes+footerBytes && bytes.substr(0,8)==magic,"Unsupported/truncated inspection bundle");
    const auto sourceSize=get64(bytes.data()+8),protocolSize=get64(bytes.data()+16),originPid=get64(bytes.data()+24);
    check(sourceSize && protocolSize && originPid && sourceSize<=limits.sourceBytes && protocolSize<=limits.protocolBytes,
          "Inspection bundle fields exceed envelope");
    check(totalBytes(sourceSize,protocolSize,bytes.size())==bytes.size(),"Inspection bundle extent mismatch");
    const auto encoded=bytes.substr(headerBytes+sourceSize,protocolSize);
    const auto protocolHash=hashForeignSnapshot(encoded,memory,stop);
    check(bytes.substr(bytes.size()-footerBytes)==std::string_view(protocolHash.data(),protocolHash.size()),
          "Inspection protocol checksum differs");
    auto source=copyForeignSnapshot(bytes.substr(headerBytes,sourceSize),memory,stop);
    const auto sourceHash=hashForeignSnapshot(source.bytes(),memory,stop);
    const auto lines=foreignSnapshotLines(source.bytes(),stop);
    check(lines && lines<=ReaperStructureLimits{}.maximumLines,"Inspection source line envelope differs");
    auto rows=memory.reserve(inspectionRowsCharge(lines));
    auto parser=memory.reserve(inspectionParserCharge(encoded.size()));
    auto protocolLease=memory.reserve(inspectionProtocolCharge(encoded.size()));
    OwnedInspectionProtocol protocol(std::move(protocolLease),std::string(encoded));
    return decodeInspectionReport(std::move(protocol),std::move(source),sourceHash,originPid,
                                  memory,std::move(rows),std::move(parser),stop);
}
} // namespace soundcurrent::daw
