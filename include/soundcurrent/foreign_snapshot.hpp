// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include "resource_ledger.hpp"
#include <array>
#include <filesystem>
#include <stop_token>

namespace soundcurrent::daw {
class ForeignSnapshot {
  public:
    ForeignSnapshot(ForeignSnapshot &&) noexcept = default;
    ForeignSnapshot &operator=(ForeignSnapshot &&) = delete;
    ForeignSnapshot(const ForeignSnapshot &) = delete;
    ForeignSnapshot &operator=(const ForeignSnapshot &) = delete;
    std::string_view bytes() const noexcept { return bytes_; }
    std::size_t chargedBytes() const noexcept { return lease_.bytes(); }
  private:
    friend ForeignSnapshot readForeignSnapshot(const std::filesystem::path &, std::size_t,
                                                ResourceLedger, std::stop_token);
    explicit ForeignSnapshot(ResourceLease lease, std::size_t size)
        : lease_(std::move(lease)), bytes_(size, '\0') {}
    ResourceLease lease_;
    std::string bytes_;
};
// Off audio. One selected pinned plain-file handle, bounded read and metadata
// consistency checks. This does not parse or follow foreign dependency tokens.
ForeignSnapshot readForeignSnapshot(const std::filesystem::path &, std::size_t maximumBytes,
                                     ResourceLedger, std::stop_token = {});
std::array<char,64> hashForeignSnapshot(std::string_view, ResourceLedger, std::stop_token = {});
} // namespace soundcurrent::daw
