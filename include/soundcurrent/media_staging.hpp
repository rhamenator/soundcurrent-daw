// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include "approved_media.hpp"

namespace soundcurrent::daw {
struct StagedMediaState;
class MediaProvenance;
struct CheckedMediaBytes {
    std::uint64_t bytes = 0;
    std::array<char,64> sha256{};
};
enum class StageBoundary {
    BeforeDirectory, BeforeFile, BeforeWrite, BeforeFileFlush,
    BeforeReadback, BeforeFinalSourceCheck, BeforeDirectoryFlush,
    BeforeIntentWrite, BeforeIntentFlush, BeforeAssetRename, BeforeReceiptWrite,
    BeforeReceiptFlush, BeforeCommit, AfterCommitBeforeDirectoryFlush
};
// Trusted control/test seam only. No callback runs on the audio thread. A
// callback may throw to exercise failure retirement; foreign input cannot
// select hooks. Cancellation is cooperative around blocking native I/O.
using StageObserver = std::function<void(StageBoundary,std::uint64_t)>;
enum class StageDurability { FileFlushed=1, FileAndDirectoriesFlushed=2 };
struct MediaCommitOutcome {
    bool published=false,postCommitFlushFailed=false;
    StageDurability durability=StageDurability::FileFlushed;
};
class StagedMedia {
  public:
    StagedMedia(StagedMedia &&) noexcept;
    ~StagedMedia();
    StagedMedia(const StagedMedia &) = delete;
    StagedMedia &operator=(const StagedMedia &) = delete;
    const Id &operation() const;
    // Generated portable path below the selected destination parent. The
    // .partial suffix remains until a bound owner commits it to media.wav.
    // Neither state attaches a Session asset or commits a project transaction.
    const std::string &relativePath() const;
    CheckedMediaBytes checkedBytes() const;
    StageDurability durability() const;
    std::size_t chargedBytes() const;
  private:
    friend StagedMedia stageVerifiedMedia(ApprovedMediaFile &,const std::filesystem::path &,
        CheckedMediaBytes,std::uint64_t,Id,std::stop_token,const StageObserver &);
    friend StagedMedia stageBoundMedia(ApprovedMediaFile &,const std::filesystem::path &,
        const MediaProvenance &,std::uint64_t,std::stop_token,const StageObserver &);
    friend MediaCommitOutcome commitStagedMedia(StagedMedia &,const MediaProvenance &,
        std::stop_token,const StageObserver &);
    friend StagedMedia stageMediaImpl(ApprovedMediaFile &,const std::filesystem::path &,
        CheckedMediaBytes,std::uint64_t,Id,std::stop_token,const StageObserver &,const MediaProvenance *);
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
// Writes/flushed planned intent before media I/O. The receipt supplies exact
// expected bytes/hash and operation ID; no saved reference grants source access.
StagedMedia stageBoundMedia(ApprovedMediaFile &,const std::filesystem::path &,
    const MediaProvenance &,std::uint64_t maximumBytes,std::stop_token = {},const StageObserver & = {});
// One attempt per live owner. Both records and media are reverified; publication
// is an exclusive receipt rename. Cancellation/throws before that point publish
// no verified receipt. After publication, no exception/cancellation is reported
// as an unpublished failure: directory-flush errors are returned explicitly.
// Does not attach a Session asset or commit a multi-file/project transaction.
MediaCommitOutcome commitStagedMedia(StagedMedia &,const MediaProvenance &,
    std::stop_token = {},const StageObserver & = {});
} // namespace soundcurrent::daw
