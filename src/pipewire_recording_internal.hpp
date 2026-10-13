// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include <soundcurrent/pipewire_recording_state.hpp>
#include "media_io.hpp"
namespace soundcurrent::daw::detail {
PipeWireRecordingPrefix inspectPipeWireRecordingHeld(const std::filesystem::path &,
    const media_io::JobLease &receipts,const media_io::JobLease &raw,
    const std::optional<PipeWireRecordingSpec> &,std::optional<ResourceLedger>,const std::function<void()> &);
PipeWireStoredRecording inspectPipeWireRecordingStateHeld(const std::filesystem::path &,
    const media_io::JobLease &state,const media_io::JobLease &receipts,const media_io::JobLease &raw,
    const std::optional<PipeWireRecordingSpec> &,std::optional<ResourceLedger>,const std::function<void()> &);
}
