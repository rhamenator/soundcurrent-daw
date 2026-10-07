// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include <cstdint>
#include <filesystem>
namespace native_fixture {
// Test-only API observations. No mutation of ports, IO, buffers or scheduling.
void handoffBeforeDsp(void *, std::uint32_t) noexcept;
void handoffAfterDsp(const void *) noexcept;
#ifdef SC_NATIVE_STARTUP_GATE
bool handoffSuppressDsp() noexcept; // Explicit counterfactual fixture only.
#endif
#ifdef SC_NATIVE_BUFFER_ACQUISITION
void handoffNativeDequeue(void *) noexcept;
void handoffAfterAcquisition(const void *, unsigned status) noexcept;
void handoffNativeQueue(void *key, void *buffer, int result) noexcept;
#endif
void writePortHandoffs(const std::filesystem::path &); // All callbacks joined.
} // namespace native_fixture
