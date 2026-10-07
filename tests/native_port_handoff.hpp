// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include <cstdint>
#include <filesystem>
namespace native_fixture {
// Test-only API observations. No mutation of ports, IO, buffers or scheduling.
void handoffBeforeDsp(void *, std::uint32_t) noexcept;
void handoffAfterDsp(const void *) noexcept;
void writePortHandoffs(const std::filesystem::path &); // All callbacks joined.
} // namespace native_fixture
