// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include "approved_media.hpp"

namespace soundcurrent::daw {
// Stable report IDs, independent of language/display labels.
enum class WaveEncoding { Unsigned8=1, Signed16=2, Signed24=3, Signed32=4, Float32=5, Float64=6 };
struct WaveValidationLimits {
    // Trusted control policy, not values read from the source header.
    std::uint64_t maximumFrames = 1000000000;
    std::uint64_t maximumBytesRead = 8ULL * 1024 * 1024 * 1024;
    std::uint64_t maximumIoOperations = 1000000;
    std::uint32_t maximumChunks = 4096;
    std::uint32_t maximumChannels = 1024;
    std::uint32_t blockFrames = 256;
};
struct WaveValidation {
    std::uint64_t sourceBytes=0,frames=0,decodedFrames=0,bytesRead=0,ioOperations=0;
    std::uint32_t rate=0,channels=0,bitsPerSample=0,channelMask=0;
    WaveEncoding encoding=WaveEncoding::Signed16;
    bool bigEndian=false,extensible=false;
    double peak=0;
    std::array<char,64> sourceSha256{};
};
// Serialized control/I/O only. RIFF/RIFX uncompressed PCM/IEEE float WAVE;
// WAVEX with the same PCM/float subtype and full container precision.
// Other formats/precision/containers return UnsupportedSchema; they remain
// required follow-up work, not silently interpreted as empty/missing audio.
// Reads through the pinned object only. Third-party decoder allocations and
// time are not hard bounded: the future GUI must use a child with a deadline.
// Observer receives borrowed, normalized interleaved doubles for one block;
// float source headroom is preserved. It must not retain the span or publish a
// completed asset until successful return; a later block/hash may still fail.
// beforeRead runs on this I/O owner before decoder operations/read/hash chunks.
// Exceptions cross the C decoder boundary via a stored exception and rethrow.
WaveValidation validateApprovedWave(ApprovedMediaFile &, WaveValidationLimits = {},
    std::stop_token = {}, const std::function<void(std::uint64_t,std::span<const double>)> &observer = {},
    const std::function<void()> &beforeRead = {});
} // namespace soundcurrent::daw
