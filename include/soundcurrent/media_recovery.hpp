// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include "media_provenance.hpp"
namespace soundcurrent::daw {
enum class MediaRecoveryPhase { NoIntentOrReceipt=0, Planned=1, Verified=2 };
struct MediaRecovery {
    MediaRecoveryPhase phase=MediaRecoveryPhase::NoIntentOrReceipt;
    std::unique_ptr<MediaProvenance> provenance;
};
// Explicit selected owned parent/UUID, off audio and future GUI in a bounded
// child. No saved source roots are opened. Missing record -> no completed asset;
// valid planned intent -> inspectable partial, not automatically resumed/deleted.
// Committed receipt must match intent, operation and full pinned WAVE metadata /
// checksum before a verified result. No signature or historical fsync proof.
MediaRecovery recoverStagedMedia(const std::filesystem::path &selectedParent,const Id &operation,
    ResourceLedger,std::uint64_t maximumBytes,std::stop_token = {});
} // namespace soundcurrent::daw
