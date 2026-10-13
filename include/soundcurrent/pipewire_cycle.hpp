// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include "audio_bridge.hpp"
namespace soundcurrent::daw {
enum class PipeWireBufferStatus { Unavailable, Ready, Invalid, Silence };
struct PipeWirePortLeaseResult {
    PipeWireBufferStatus status = PipeWireBufferStatus::Unavailable;
    bool acquired = false;
    std::optional<std::int32_t> queueResult; // Absent if queue_buffer was not called.
    bool operator==(const PipeWirePortLeaseResult &) const = default;
};
// Borrowed only during the callback, after EVERY attempted native buffer return.
// Exact queue_buffer results, not HRESULTs or proof of audible delivery.
struct PipeWireCycleReceipt {
    DeviceBlockClock original;
    bool processingInvoked = false;
    std::span<const PipeWirePortLeaseResult> inputs, outputs;
};
} // namespace soundcurrent::daw
