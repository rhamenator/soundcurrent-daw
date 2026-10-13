// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include "native_original_recording.hpp"
#include "export.hpp"
#include "original_prefix_copy.hpp"
namespace soundcurrent::daw {
struct NativeOriginalExportOptions {
    std::uint64_t maximumBytes = 1024ULL*1024*1024;
    ResourceLedger resources{16*1024*1024,"Native original export"};
    std::function<bool()> canceled;
    std::function<void(std::uint64_t,std::uint64_t)> progress;
};
struct NativeOriginalExportResult {
    std::filesystem::path destination;
    NativeOriginalRecordingStatus originalStatus;
    std::uint64_t bytes=0,frames=0;
    std::uint32_t sampleRate=0;
    ChannelLayout layout;
    std::string fileSha256,sampleSha256;
    std::string publicationWarning; // Published file remains valid when directory flush is unavailable.
    Durability durability=Durability::FileFlushed;
};
// Serialized inactive control/I/O only. Copies the verified complete RF64 WAV
// byte-for-byte, including signed zero/headroom, without EQ or a project-time
// anchor. Requires a finalized wholly finite original prefix; no overwrite.
NativeOriginalExportResult exportNativeOriginalWav(const std::filesystem::path &projectRoot,
    const std::filesystem::path &job,const NativeOriginalRecordingSpec &expected,
    const std::filesystem::path &destination,const NativeOriginalExportOptions & = {});
struct NativeOriginalPrefixCopyResult {
    NativeOriginalRecordingPrefix original;
    OriginalPrefixCopyResult copy;
};
// Recover a positive verified finite prefix, including inactive unfinalized
// takes. The raw/native checkpoint is verified and kept leased across copying.
// Native pending/interrupted/failure dispositions remain in the receipt.
NativeOriginalPrefixCopyResult copyNativeOriginalPrefixWav(const std::filesystem::path &root,
    const std::filesystem::path &job,const NativeOriginalRecordingSpec &expected,
    const std::filesystem::path &destination,const OriginalPrefixCopyOptions & = {});

}
