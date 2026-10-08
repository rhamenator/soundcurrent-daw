// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include <soundcurrent/wasapi_capture.hpp>
#define WIN32_LEAN_AND_MEAN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <audioclient.h>
#include <mmdeviceapi.h>
#include <functiondiscoverykeys_devpkey.h>
#include <algorithm>
#include <exception>
#include <limits>
#include <thread>

#include <avrt.h>
namespace soundcurrent::daw::wasapi_detail {
inline void check(HRESULT hr, const char *operation) {
    if (FAILED(hr))
        throw ProjectError(ErrorCode::Io, std::string(operation) + " (HRESULT " +
                           std::to_string(static_cast<std::int32_t>(hr)) + ")");
}
struct Apartment {
    bool owned = false;
    Apartment() {
        const auto hr = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
        // Inventory is also valid in an existing UI/OLE STA. A failed mode
        // change did not add a COM initialization reference: never release it.
        if (hr == RPC_E_CHANGED_MODE) return;
        check(hr, "Initialize audio COM");
        owned = true; // S_OK and S_FALSE both require a matching uninitialize.
    }
    ~Apartment() { if (owned) CoUninitialize(); }
};
template <class T> struct Com {
    T *p = nullptr;
    ~Com() { if (p) p->Release(); }
    Com() = default;
    Com(const Com &) = delete;
    Com &operator=(const Com &) = delete;
    T **out() { return &p; }
    T *operator->() const { return p; }
};
struct Memory {
    void *p = nullptr;
    ~Memory() { CoTaskMemFree(p); }
};
struct Variant {
    PROPVARIANT value{};
    ~Variant() { PropVariantClear(&value); }
};
struct Event {
    HANDLE value;
    explicit Event(bool manual = true) : value(CreateEventW(nullptr, manual, FALSE, nullptr)) {
        if (!value) check(HRESULT_FROM_WIN32(GetLastError()), "Create audio event");
    }
    ~Event() { CloseHandle(value); }
};
inline std::string utf8(const wchar_t *s) {
    if (!s) return {};
    const auto length = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, s, -1,
                                           nullptr, 0, nullptr, nullptr);
    if (!length) check(HRESULT_FROM_WIN32(GetLastError()), "Encode endpoint identity");
    std::string result(static_cast<std::size_t>(length), '\0');
    if (!WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, s, -1, result.data(), length,
                            nullptr, nullptr))
        check(HRESULT_FROM_WIN32(GetLastError()), "Encode endpoint identity");
    result.pop_back();
    return result;
}
inline std::wstring wide(const std::string &s) {
    if (s.empty() || s.size() > 32768 || s.find('\0') != std::string::npos)
        throw ProjectError(ErrorCode::InvalidState, "Invalid endpoint identity");
    const auto length = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, s.data(),
                                           static_cast<int>(s.size()), nullptr, 0);
    if (!length) check(HRESULT_FROM_WIN32(GetLastError()), "Decode endpoint identity");
    std::wstring result(static_cast<std::size_t>(length), L'\0');
    if (!MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, s.data(), static_cast<int>(s.size()),
                            result.data(), length))
        check(HRESULT_FROM_WIN32(GetLastError()), "Decode endpoint identity");
    return result;
}
inline void enumerator(Com<IMMDeviceEnumerator> &e) {
    check(CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL,
                           __uuidof(IMMDeviceEnumerator), reinterpret_cast<void **>(e.out())),
          "Enumerate audio endpoints");
}
inline constexpr GUID floatSubtype{3, 0, 0x10, {0x80, 0, 0, 0xaa, 0, 0x38, 0x9b, 0x71}};
// Registration/reversion belongs to this native owner thread, outside prepared
// processing. Fail preparation honestly if the requested scheduling task fails.
struct Scheduling {
    HANDLE task = nullptr;
    Scheduling() {
        DWORD index = 0;
        task = AvSetMmThreadCharacteristicsW(L"Pro Audio", &index);
        if (!task) check(HRESULT_FROM_WIN32(GetLastError()), "Register Pro Audio scheduling");
        if (!AvSetMmThreadPriority(task, AVRT_PRIORITY_HIGH)) {
            const auto error = GetLastError();
            AvRevertMmThreadCharacteristics(task); task = nullptr;
            check(HRESULT_FROM_WIN32(error), "Set audio scheduling priority");
        }
    }
    ~Scheduling() { if (task) AvRevertMmThreadCharacteristics(task); }
    Scheduling(const Scheduling &) = delete;
    Scheduling &operator=(const Scheduling &) = delete;
};
} // namespace soundcurrent::daw::wasapi_detail
