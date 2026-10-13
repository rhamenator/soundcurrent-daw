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
    std::uint32_t maximumRawSlots=4096,maximumReceiptSlots=4096;
    std::size_t memoryBudgetBytes=64*1024*1024;
};
struct RecordingStorageCapacity {
    std::uint32_t rawSlots=0,captureReceiptSlots=0,renderReceiptSlots=0;
    std::uint64_t retainedRawFrames=0;
    std::size_t rawSampleBytes=0;
    std::uint32_t declaredStallMilliseconds=0;
};
// Native disk consumer retains raw capture only; output callback size is irrelevant.
struct RawRecordingStoragePolicy {
    std::uint32_t stallMilliseconds=3000,minimumPacketFrames=0,maximumSlots=4096;
    std::size_t memoryBudgetBytes=64*1024*1024;
};
struct RawRecordingStorageDeclaration {
    std::uint32_t stallMilliseconds=0,minimumPacketFrames=1,slots=0;
    bool operator==(const RawRecordingStorageDeclaration &) const = default;
};
RecordingStorageCapacity prepareRawRecordingStorageCapacity(std::uint32_t rate,std::uint32_t channels,
    std::uint32_t maximumPacketFrames,const RawRecordingStoragePolicy &);
// No allocation/driver access. Refuses a requested duration which existing queue
// bounds or PCM budget cannot cover; finite capacity is never an unlimited guarantee.
RecordingStorageCapacity prepareRecordingStorageCapacity(const RecordingStorageBudget &);
}
