// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include <filesystem>
#ifdef _WIN32
#include <string>
namespace soundcurrent::daw {
// Representation for explicit Win32 file APIs only. Keep stored/project paths
// conventional. This does not admit a path or replace reparse/handle checks.
// Extended syntax avoids dependence on machine-wide LongPathsEnabled policy.
inline std::wstring windowsFileApiPath(const std::filesystem::path &input) {
    auto path=std::filesystem::absolute(input).lexically_normal();
    path.make_preferred();auto native=path.native();
    if(native.starts_with(L"\\\\?\\") || native.starts_with(L"\\\\.\\"))return native;
    if(native.starts_with(L"\\\\"))return L"\\\\?\\UNC\\"+native.substr(2);
    return L"\\\\?\\"+native;
}
} // namespace soundcurrent::daw
#endif

namespace soundcurrent::daw {
inline std::filesystem::path fileSystemApiPath(const std::filesystem::path &path) {
#ifdef _WIN32
    return std::filesystem::path(windowsFileApiPath(path));
#else
    return path;
#endif
}
} // namespace soundcurrent::daw
