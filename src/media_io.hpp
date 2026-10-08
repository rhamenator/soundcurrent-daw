// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include <soundcurrent/project_store.hpp>
#include <filesystem>
#include <memory>
#include <span>

namespace soundcurrent::daw::media_io {
void require(bool, const char *);
void plainDirectory(const std::filesystem::path &);
void plainFile(const std::filesystem::path &);
class File {
  public:
    // Exclusive creation or read-only open. No sharing a live writer handle.
    // Active checkpoint inspection alone may coexist with an existing Windows
    // writer. Normal media admission/hash/cache readers still deny mutations.
    File(const std::filesystem::path &, bool create, bool inspectActiveWriter = false);
    ~File();
    File(const File &) = delete;
    File &operator=(const File &) = delete;
    int descriptor() const noexcept {
        return fd_;
    }
    void flush();
    void close();
    void write(std::string_view);
    std::string digest(const std::function<void()> &beforeRead = {}); // Same owned descriptor.

  private:
    int fd_ = -1;
};
Durability flushDirectory(const std::filesystem::path &);
// No-overwrite publication preserves an existing destination on failure.
void publishMedia(const std::filesystem::path &, const std::filesystem::path &);
Durability publishJournal(const std::filesystem::path &, std::string_view);
std::string readJournal(const std::filesystem::path &);
enum class LeaseStatus { Held, Busy, Absent };
// Worker-side cooperative lifetime lock. Readers never create or modify a job.
class JobLease {
  public:
    JobLease(const std::filesystem::path &job, bool writer, bool exclusiveExisting = false);
    ~JobLease();
    LeaseStatus status() const noexcept;

  private:
    struct State;
    std::unique_ptr<State> state_;
};
class SampleHash {
  public:
    SampleHash();
    ~SampleHash();
    void update(std::span<const float>);
    void updateBytes(std::span<const std::byte>);
    std::string digest() const;

  private:
    struct State;
    std::unique_ptr<State> state_;
};
} // namespace soundcurrent::daw::media_io
