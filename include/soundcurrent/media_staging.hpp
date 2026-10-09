// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include "approved_media.hpp"

namespace soundcurrent::daw {
struct StagedMediaState;
struct CheckedMediaBytes {
    std::uint64_t bytes = 0;
    std::array<char,64> sha256{};
};
enum class StageBoundary {
    BeforeDirectory, BeforeFile, BeforeWrite, BeforeFileFlush,
    BeforeReadback, BeforeFinalSourceCheck, BeforeDirectoryFlush
};
// Trusted control/test seam only. No callback runs on the audio thread. A
// callback may throw to exercise failure retirement; foreign input cannot
// select hooks. Cancellation is cooperative around blocking native I/O.
using StageObserver = std::function<void(StageBoundary,std::uint64_t)>;
enum class StageDurability { FileFlushed=1, FileAndDirectoriesFlushed=2 };
class StagedMedia {
  public:
    StagedMedia(StagedMedia &&) noexcept;
    ~StagedMedia();
    StagedMedia(const StagedMedia &) = delete;
    StagedMedia &operator=(const StagedMedia &) = delete;
    const Id &operation() const;
    // Generated portable path below the selected destination parent. The
    // .partial suffix remains even after verification; this is NOT a published
    // Session asset or recoverable project transaction.
    const std::string &relativePath() const;
    CheckedMediaBytes checkedBytes() const;
    StageDurability durability() const;
    std::size_t chargedBytes() const;
  private:
    friend StagedMedia stageVerifiedMedia(ApprovedMediaFile &,const std::filesystem::path &,
        CheckedMediaBytes,std::uint64_t,Id,std::stop_token,const StageObserver &);
    explicit StagedMedia(std::unique_ptr<StagedMediaState>);
    std::unique_ptr<StagedMediaState> state_;
};
// Serialized control/I/O only. No decoder/Qt/process launch. Caller separately
// qualifies the format and binds the checksum to its selected occurrence.
// Bytes are freshly hashed before destination mutation, during transfer,
// through the owned destination handle, and finally through the source handle.
// Explicit absolute destination parent: final component must be plain. The
// parent and its writers must be trusted; Linux mkdir/open is not an atomic
// inode creation capability against hostile same-UID namespace writers.
// Creates one exclusive operation UUID directory and media.partial inside it;
// NEVER overwrites existing names, removes partial files, or edits a project.
// Failure leaves inspectable partial staging; no successful result is returned.
// Success retains the native parent/directory/file handles and their admission
// until result retirement. No durable provenance/commit marker is claimed yet.
StagedMedia stageVerifiedMedia(ApprovedMediaFile &,const std::filesystem::path &destinationParent,
    CheckedMediaBytes,std::uint64_t maximumBytes,Id operation=Id::generate(),
    std::stop_token = {},const StageObserver & = {});
} // namespace soundcurrent::daw
