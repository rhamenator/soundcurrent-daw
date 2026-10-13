// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include <soundcurrent/original_prefix_copy.hpp>
#include "media_io.hpp"
namespace soundcurrent::daw::detail {
// Bound owned source file sizes before potentially lengthy prefix verification.
void admitOriginalPrefixCopySource(const std::filesystem::path &,const OriginalPrefixCopyOptions &);
// Caller has verified this exact common prefix under the same exclusive raw
// job lease, and retains paired native/state leases through publication.
OriginalPrefixCopyResult copyOriginalPrefixWavHeld(const std::filesystem::path &,
    const std::filesystem::path &,const OriginalRecordingPrefix &,const media_io::JobLease &,
    const std::filesystem::path &,const OriginalPrefixCopyOptions &);
}
