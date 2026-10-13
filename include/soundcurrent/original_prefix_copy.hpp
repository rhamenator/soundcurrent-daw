// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include "original_recording.hpp"
#include "export.hpp"
namespace soundcurrent::daw {
struct OriginalPrefixCopyExpectation {
    std::uint64_t frames=0,packets=0,finiteFrames=0;
    std::string sampleSha256,stampSha256;
    bool finalized=false;
};
inline OriginalPrefixCopyExpectation prefixCopyExpectation(const OriginalRecordingPrefix &p) {
    return {p.committedFrames,p.committedPackets,p.finitePrefixFrames,p.sampleSha256,p.stampSha256,p.finalized};
}
struct OriginalPrefixCopyOptions {
    std::uint64_t maximumBytes = 1024ULL*1024*1024;
    ResourceLedger resources{16*1024*1024,"Original prefix copy"};
    std::function<bool()> canceled;
    std::optional<OriginalPrefixCopyExpectation> expectedPrefix; // Explicit reviewed checkpoint, checked before output creation.
    // Verified committed source bytes consumed, not native/project time.
    std::function<void(std::uint64_t,std::uint64_t)> progress;
};
struct OriginalPrefixCopyResult {
    OriginalRecordingPrefix original; // Its interrupted/finalized facts stay unchanged.
    std::filesystem::path destination;
    std::uint64_t bytes=0,frames=0;
    std::string fileSha256,sampleSha256,publicationWarning;
    Durability durability=Durability::FileFlushed;
};
// Inactive control/I/O only. Verify the common checkpoint and copy exactly its
// positive finite prefix into a new RF64 float32 WAV. Preserve sample bits,
// channel ordinals/rate/headroom; exclude uncommitted/nonfinite tails. No EQ,
// source mutation, timeline placement, overwrite or invented native completion.
OriginalPrefixCopyResult copyOriginalPrefixWav(const std::filesystem::path &projectRoot,
    const std::filesystem::path &job,const OriginalRecordingSpec &expected,
    const std::filesystem::path &destination,const OriginalPrefixCopyOptions & = {});
}
