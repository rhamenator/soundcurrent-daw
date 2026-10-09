// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include "resource_ledger.hpp"
#include <array>
#include <filesystem>
#include <functional>
#include <span>
#include <stop_token>

namespace soundcurrent::daw {
struct ApprovedMediaRootState;
struct ApprovedMediaFileState;
struct ApprovedMediaLimits {
    std::size_t maximumOpenFiles = 32;
};
// Control/I/O only. A root and its files have one serialized caller, including
// retirement. No Qt, audio callbacks, implicit root selection or foreign code.
// Files keep the approved directory handle/credit alive beyond its facade.
class ApprovedMediaFile {
  public:
    ApprovedMediaFile(ApprovedMediaFile &&) noexcept;
    ~ApprovedMediaFile();
    ApprovedMediaFile(const ApprovedMediaFile &) = delete;
    ApprovedMediaFile &operator=(const ApprovedMediaFile &) = delete;
    std::uint64_t size() const;
    // Caller-owned buffer, at most 64KiB. Exact positional read; no path reopen.
    void readAt(std::uint64_t offset, std::span<char>, std::stop_token = {});
    void verifyUnchanged() const;
    // Bounded streaming hash, not a full-file allocation. Callback runs before
    // each chunk off audio; cancellation is cooperative around OS I/O.
    std::array<char,64> digest(std::stop_token = {}, const std::function<void()> &beforeChunk = {});
    std::size_t chargedBytes() const;
    // Shared control/I/O admission scope for work performed on this owned file.
    ResourceLedger resourceLedger() const;
  private:
    friend class ApprovedMediaRoot;
    explicit ApprovedMediaFile(std::unique_ptr<ApprovedMediaFileState>);
    std::unique_ptr<ApprovedMediaFileState> state_;
};
class ApprovedMediaRoot {
  public:
    // Explicit absolute directory selected by the caller. The final component
    // must be a plain directory. Approval binds its identity, not its path name.
    ApprovedMediaRoot(const std::filesystem::path &, ResourceLedger, ApprovedMediaLimits = {});
    ApprovedMediaRoot(ApprovedMediaRoot &&) noexcept;
    ~ApprovedMediaRoot();
    ApprovedMediaRoot(const ApprovedMediaRoot &) = delete;
    ApprovedMediaRoot &operator=(const ApprovedMediaRoot &) = delete;
    // Canonical UTF-8 '/'-separated relative token; no normalization/expansion.
    // Foreign-platform/absolute tokens need an explicit mapping before this API.
    // Maximum size is trusted policy, never read from a foreign project.
    ApprovedMediaFile open(std::string_view relative, std::uint64_t maximumBytes,
                           std::stop_token = {}) const;
    std::size_t openFiles() const;
  private:
    std::shared_ptr<ApprovedMediaRootState> state_;
};
} // namespace soundcurrent::daw
