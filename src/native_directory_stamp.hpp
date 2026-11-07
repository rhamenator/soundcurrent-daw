// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include <soundcurrent/approved_media.hpp>
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <sys/stat.h>
#endif
namespace soundcurrent::daw::detail {
inline ApprovedDirectoryStamp nativeDirectoryStamp(
#ifdef _WIN32
    HANDLE handle
#else
    int handle
#endif
) {
    const auto require=[](bool ok,const char *message){if(!ok)throw ProjectError(ErrorCode::Io,message);};
#ifdef _WIN32
    BY_HANDLE_FILE_INFORMATION info{}; FILE_BASIC_INFO basic{};
    require(GetFileInformationByHandle(handle,&info) &&
        GetFileInformationByHandleEx(handle,FileBasicInfo,&basic,sizeof(basic)) &&
        (info.dwFileAttributes&FILE_ATTRIBUTE_DIRECTORY) && !(info.dwFileAttributes&FILE_ATTRIBUTE_REPARSE_POINT),
        "Cannot inspect approved directory");
    return {{{info.dwVolumeSerialNumber,(std::uint64_t(info.nFileIndexHigh)<<32)|info.nFileIndexLow,
        0,std::uint64_t(basic.LastWriteTime.QuadPart),0,std::uint64_t(basic.ChangeTime.QuadPart),0}}};
#else
    struct stat info{};
    require(fstat(handle,&info)==0 && S_ISDIR(info.st_mode),"Cannot inspect approved directory");
    return {{{std::uint64_t(info.st_dev),std::uint64_t(info.st_ino),std::uint64_t(info.st_size),
        std::uint64_t(info.st_mtim.tv_sec),std::uint64_t(info.st_mtim.tv_nsec),
        std::uint64_t(info.st_ctim.tv_sec),std::uint64_t(info.st_ctim.tv_nsec)}}};
#endif
}
} // namespace soundcurrent::daw::detail
