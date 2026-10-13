// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include "session.hpp"
namespace soundcurrent::daw {
// Control-side service budget. Minimum cycle sizes are explicit assumptions,
// not inferred from a maximum packet size or nominal hardware latency.
struct RecordingStorageBudget {
    std::uint32_t captureRate=48000,renderRate=48000,captureChannels=1;
    std::uint32_t minimumCaptureFrames=480,maximumCaptureFrames=480,minimumRenderFrames=480;
    std::uint32_t stallMilliseconds=500,headroomPackets=2;
    std::uint32_t maximumRawSlots=256,maximumReceiptSlots=64;
    std::size_t memoryBudgetBytes=64*1024*1024;
};
struct RecordingStorageCapacity {
    std::uint32_t rawSlots=0,captureReceiptSlots=0,renderReceiptSlots=0;
    std::uint64_t retainedRawFrames=0;
    std::size_t rawSampleBytes=0;
    std::uint32_t declaredStallMilliseconds=0;
};
// No allocation/driver access. Refuses a requested duration which existing queue
// bounds or PCM budget cannot cover; finite capacity is never an unlimited guarantee.
RecordingStorageCapacity prepareRecordingStorageCapacity(const RecordingStorageBudget &);
}
