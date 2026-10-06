// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include <cstdint>
#include <ostream>
#include <sys/resource.h>

namespace native_fixture {
// Linux fixture-only diagnostics. Accounting has timeval resolution and may
// update differently from CLOCK_THREAD_CPUTIME_ID; it is not pure DSP timing.
struct ThreadUsage {
    std::uint64_t userNs = 0, systemNs = 0, minorFaults = 0, majorFaults = 0;
    std::uint64_t voluntarySwitches = 0, involuntarySwitches = 0;
    bool known = false;
};
inline ThreadUsage threadUsageNow() noexcept {
    rusage r{};
    const auto validTime = [](const timeval &t) {
        return t.tv_sec >= 0 && t.tv_usec >= 0 && t.tv_usec < 1000000 &&
               std::uint64_t(t.tv_sec) <= (UINT64_MAX - 999999000ULL) / 1000000000ULL;
    };
    if (getrusage(RUSAGE_THREAD, &r) != 0 || !validTime(r.ru_utime) || !validTime(r.ru_stime) ||
        r.ru_minflt < 0 || r.ru_majflt < 0 || r.ru_nvcsw < 0 || r.ru_nivcsw < 0)
        return {};
    const auto ns = [](const timeval &t) {
        return std::uint64_t(t.tv_sec) * 1000000000ULL + std::uint64_t(t.tv_usec) * 1000ULL;
    };
    return {ns(r.ru_utime),
            ns(r.ru_stime),
            std::uint64_t(r.ru_minflt),
            std::uint64_t(r.ru_majflt),
            std::uint64_t(r.ru_nvcsw),
            std::uint64_t(r.ru_nivcsw),
            true};
}
inline ThreadUsage usageDelta(const ThreadUsage &begin, const ThreadUsage &end) noexcept {
    if (!begin.known || !end.known || end.userNs < begin.userNs || end.systemNs < begin.systemNs ||
        end.minorFaults < begin.minorFaults || end.majorFaults < begin.majorFaults ||
        end.voluntarySwitches < begin.voluntarySwitches ||
        end.involuntarySwitches < begin.involuntarySwitches)
        return {};
    return {end.userNs - begin.userNs,
            end.systemNs - begin.systemNs,
            end.minorFaults - begin.minorFaults,
            end.majorFaults - begin.majorFaults,
            end.voluntarySwitches - begin.voluntarySwitches,
            end.involuntarySwitches - begin.involuntarySwitches,
            true};
}
inline void writeUsage(std::ostream &out, const ThreadUsage &u) {
    out << "{\"known\":" << (u.known ? "true" : "false") << ",\"user_ns\":" << u.userNs
        << ",\"system_ns\":" << u.systemNs << ",\"minor_faults\":" << u.minorFaults
        << ",\"major_faults\":" << u.majorFaults
        << ",\"voluntary_switches\":" << u.voluntarySwitches
        << ",\"involuntary_switches\":" << u.involuntarySwitches << '}';
}
} // namespace native_fixture
