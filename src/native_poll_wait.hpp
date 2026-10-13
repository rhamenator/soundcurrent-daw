// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include <soundcurrent/session.hpp>
#include <chrono>
#include <thread>
#include <optional>
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace soundcurrent::daw::detail {
// Prepare/retire on control; ONE waiting worker. NEVER invoke in an audio callback.
// No process-wide timer-resolution change, spinning or polling another owner's
// mutable state. Requested intervals are scheduling intents, not deadlines.
class NativePollWait {
    const std::uint32_t microseconds_;
#ifdef _WIN32
    HANDLE timer_ = nullptr;
#endif
    std::uint64_t calls_ = 0, maximumNs_ = 0;
  public:
    explicit NativePollWait(std::uint32_t microseconds = 100) : microseconds_(microseconds) {
        if(!microseconds || microseconds>1000000)throw ProjectError(ErrorCode::InvalidParameter,"Invalid native worker wait interval");
#ifdef _WIN32
        timer_=CreateWaitableTimerExW(nullptr,nullptr,DWORD(0x2),TIMER_MODIFY_STATE|SYNCHRONIZE);
        if(!timer_)throw ProjectError(ErrorCode::InvalidState,"Native high-resolution worker timer unavailable");
#endif
    }
    ~NativePollWait() {
#ifdef _WIN32
        if(timer_)CloseHandle(timer_);
#endif
    }
    NativePollWait(const NativePollWait &) = delete;
    NativePollWait &operator=(const NativePollWait &) = delete;
    std::optional<std::uint32_t> wait() noexcept {return waitFor(microseconds_);}
    // Off-RT cadence: account for work already spent in this service period.
    // Past deadlines return immediately; no catch-up replay or global timer change.
    std::optional<std::uint32_t> waitUntil(std::chrono::steady_clock::time_point deadline) noexcept {
        const auto now=std::chrono::steady_clock::now();if(deadline<=now)return {};
        const auto remaining=std::chrono::ceil<std::chrono::microseconds>(deadline-now).count();
        if(remaining>1000000)return 87; // Bounded parameter refusal, no wait.
        return waitFor(std::uint32_t(remaining));
    }
  private:
    std::optional<std::uint32_t> waitFor(std::uint32_t interval) noexcept {
        const auto begin=std::chrono::steady_clock::now();std::optional<std::uint32_t> error;
#ifdef _WIN32
        LARGE_INTEGER due{};due.QuadPart=-LONGLONG(interval)*10;
        if(!SetWaitableTimerEx(timer_,&due,0,nullptr,nullptr,nullptr,0))error=GetLastError();
        else {const auto result=WaitForSingleObject(timer_,1000);if(result!=WAIT_OBJECT_0)error=result==WAIT_FAILED?GetLastError():result;}
#else
        std::this_thread::sleep_for(std::chrono::microseconds(interval));
#endif
        const auto span=std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now()-begin).count();
        if(calls_!=UINT64_MAX)++calls_;
        if(span>0 && std::uint64_t(span)>maximumNs_)maximumNs_=std::uint64_t(span);
        return error;
    }
  public:
    std::uint64_t callsAfterJoin() const noexcept { return calls_; }
    std::uint64_t maximumNsAfterJoin() const noexcept { return maximumNs_; }
};
} // namespace soundcurrent::daw::detail
