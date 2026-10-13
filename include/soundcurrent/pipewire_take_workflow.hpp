// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include "pipewire_recording_state.hpp"
#include "native_original_export.hpp"
namespace soundcurrent::daw {
struct PipeWireOriginalExportResult {
    std::filesystem::path destination;
    std::optional<PipeWireStoredDisposition> disposition;
    std::uint64_t bytes=0,frames=0;
    std::uint32_t sampleRate=0;
    ChannelLayout layout;
    std::string fileSha256,sampleSha256,publicationWarning;
    Durability durability=Durability::FileFlushed;
};
PipeWireStoredRecording inspectProjectPipeWireRecording(const std::filesystem::path &root,
    const std::filesystem::path &job,const Id &project,std::optional<ResourceLedger> resources={},
    const std::function<void()> &check={});
PipeWireOriginalExportResult exportPipeWireOriginalWav(const std::filesystem::path &root,
    const std::filesystem::path &job,const PipeWireRecordingSpec &expected,
    const std::filesystem::path &destination,const NativeOriginalExportOptions &options={});
}
