// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include "pipewire_receipt_queues.hpp"
#include <variant>
namespace soundcurrent::daw {
using PipeWireReceipt = std::variant<PipeWireReceiptRecord<PipeWireCaptureCycle>,PipeWireReceiptRecord<PipeWireRenderCycle>>;
// Disk/control only. Version1.0 bounded little-endian record with explicit POSIX
// nanosecond domain. Structural codec, NOT proof of continuity/ownership/PCM or
// successful native output. Malformed/unknown versions and trailing bytes refuse.
std::string encodePipeWireReceipt(const PipeWireReceipt &);
PipeWireReceipt decodePipeWireReceipt(std::string_view);
} // namespace soundcurrent::daw
