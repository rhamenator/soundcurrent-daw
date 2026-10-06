// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include "session.hpp"
#include <filesystem>
#include <functional>
#include <string>

namespace soundcurrent::daw {
inline constexpr std::size_t maxProjectBytes = 4 * 1024 * 1024;
std::string encodeProject(const Session &);
Session decodeProject(std::string_view);
std::filesystem::path utf8Path(std::string_view);
std::string hashMediaFile(const std::filesystem::path &,
                          const std::function<void()> &beforeRead = {});
enum class Durability { FileAndDirectoryFlushed, FileFlushed };
struct SaveResult {
    bool previousSnapshot;
    Durability durability;
};
struct SaveOptions {
    // Cooperative cancellation/progress boundary, invoked off RT after temp flush.
    // Throwing here leaves the current project unchanged.
    std::function<void()> beforePublish;
};
class ProjectStore {
  public:
    explicit ProjectStore(std::filesystem::path root) : root_(std::move(root)) {}
    SaveResult save(const Session &, const SaveOptions &options = {}) const;
    Session load() const;
    // Control/I/O only: verify owned media without publishing a project generation.
    void verifyMedia(const Session &, const std::function<void()> &beforeRead = {}) const;
    Session loadPrevious() const; // Explicit recovery; never silently substitutes.
    const std::filesystem::path &root() const noexcept {
        return root_;
    }

  private:
    std::filesystem::path root_;
    Session loadFile(const std::filesystem::path &) const;
};
} // namespace soundcurrent::daw
