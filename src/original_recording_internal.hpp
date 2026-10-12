// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include <soundcurrent/original_recording.hpp>
#include "media_io.hpp"
namespace soundcurrent::daw::detail {
// Internal disk owner; caller holds this job's exclusive inactive lease across
// verification and any paired native-state read. No nested lease acquisition.
OriginalRecordingPrefix inspectOriginalRecordingHeld(const std::filesystem::path &,
    const media_io::JobLease &,const std::optional<OriginalRecordingSpec> &,
    const std::function<void()> &,std::optional<ResourceLedger>,std::optional<std::string_view> committedJournal = {});
CaptureBankToken decodeOriginalPacketRecord(std::span<const std::byte,80>,std::uint64_t offset);
}
