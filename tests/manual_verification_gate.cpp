// SPDX-License-Identifier: GPL-3.0-only
// Test-only linker interception; production inspection and raw oracles unchanged.
#include <soundcurrent/manual_recording.hpp>

namespace manual_verification_test {
thread_local soundcurrent::daw::ManualRecordingInterrupt *interrupt = nullptr;
thread_local unsigned inspections = 0;
thread_local std::function<void()> afterInspection;
} // namespace manual_verification_test
using namespace soundcurrent::daw;
extern "C" RecordingRecovery
__real__ZN12soundcurrent3daw16inspectRecordingERKNSt10filesystem7__cxx114pathERKSt8functionIFvvEEb(
    const std::filesystem::path &, const std::function<void()> &, bool);
extern "C" RecordingRecovery
__wrap__ZN12soundcurrent3daw16inspectRecordingERKNSt10filesystem7__cxx114pathERKSt8functionIFvvEEb(
    const std::filesystem::path &path, const std::function<void()> &boundary, bool inactive) {
    auto result =
        __real__ZN12soundcurrent3daw16inspectRecordingERKNSt10filesystem7__cxx114pathERKSt8functionIFvvEEb(
            path, boundary, inactive);
    if (manual_verification_test::afterInspection)
        manual_verification_test::afterInspection();
    if (manual_verification_test::interrupt && ++manual_verification_test::inspections == 1)
        manual_verification_test::interrupt->requestStop();
    return result;
}
