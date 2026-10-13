// SPDX-License-Identifier: GPL-3.0-only
#include <soundcurrent/recording_storage_budget.hpp>
#include <limits>
namespace soundcurrent::daw {
RecordingStorageCapacity prepareRecordingStorageCapacity(const RecordingStorageBudget &b){
    if(b.captureRate<8000 || b.captureRate>384000 || b.renderRate<8000 || b.renderRate>384000 || !b.captureChannels || b.captureChannels>256 ||
       !b.minimumCaptureFrames || b.minimumCaptureFrames>b.maximumCaptureFrames || b.maximumCaptureFrames>65536 || !b.minimumRenderFrames || b.minimumRenderFrames>65536 ||
       !b.stallMilliseconds || b.stallMilliseconds>60000 || !b.headroomPackets || b.headroomPackets>64 || !b.maximumRawSlots || b.maximumRawSlots>256 || !b.maximumReceiptSlots || b.maximumReceiptSlots>64)
        throw ProjectError(ErrorCode::InvalidParameter,"Invalid recording storage service budget");
    const auto count=[&](std::uint32_t rate,std::uint32_t minimum){const auto denominator=std::uint64_t(minimum)*1000;
        return (std::uint64_t(rate)*b.stallMilliseconds+denominator-1)/denominator+b.headroomPackets;};
    const auto capture=count(b.captureRate,b.minimumCaptureFrames),render=count(b.renderRate,b.minimumRenderFrames);
    if(capture>b.maximumRawSlots || capture>b.maximumReceiptSlots || render>b.maximumReceiptSlots)
        throw ProjectError(ErrorCode::ResourceLimit,"Declared recording stall exceeds prepared raw/receipt queue capacity");
    const auto frames=capture*b.maximumCaptureFrames,bytes=frames*b.captureChannels*sizeof(float);
    if(bytes>SIZE_MAX || bytes>b.memoryBudgetBytes)throw ResourceLimitError("Recording storage PCM",std::size_t(std::min<std::uint64_t>(bytes,SIZE_MAX)),b.memoryBudgetBytes,bytes>SIZE_MAX);
    return {std::uint32_t(capture),std::uint32_t(capture),std::uint32_t(render),frames,std::size_t(bytes),b.stallMilliseconds};
}
}
