// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include <spa/node/io.h>
#include <filesystem>
#include <string_view>
namespace native_fixture {
// One prepared, test-only startup intervention on owned source channel23.
// Hold the mainloop's first add_buffer notification until a genuine RT query
// observes the public buffer while synchronous IO is still unavailable.
void configureStartupGate(std::string_view);
void startupBufferPublished(std::string_view, unsigned channel, unsigned slot) noexcept;
bool startupSuppressQuery(std::string_view, unsigned channel, bool input, bool ioKnown,
                          unsigned liveBuffers) noexcept;
void startupAfterProcess(std::string_view, unsigned channel, const spa_io_clock &, bool ioKnown,
                         unsigned liveBuffers, bool returned, bool knownBuffer,
                         bool suppressed) noexcept;
bool startupGateHolding() noexcept;
void writeStartupGate(const std::filesystem::path &); // All callbacks joined.
} // namespace native_fixture
