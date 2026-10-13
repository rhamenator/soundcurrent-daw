// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include <soundcurrent/native_original_recording.hpp>
#include "media_io.hpp"
namespace soundcurrent::daw::detail {
NativeOriginalRecordingPrefix inspectNativeOriginalRecordingHeld(const std::filesystem::path &,
    const media_io::JobLease &,const std::optional<NativeOriginalRecordingSpec> &,
    const std::function<void()> &,std::optional<ResourceLedger>);
}
