// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include "session.hpp"

namespace soundcurrent::daw {
// Original framework-independent immutable preparation. No IDs, allocation,
// file I/O, locks, logging, stateful cursor, or retirement in gainAt(). Shared
// read-ahead/offline evaluation before overlapping voices are summed.
class PreparedClipProcessing {
  public:
    explicit PreparedClipProcessing(ClipProcessing = {});
    double gainAt(Frame clipRelativeFrame) const noexcept;
    bool unity() const noexcept { return unity_; }
  private:
    ClipProcessing settings_;
    double gain_ = 1;
    bool unity_ = true;
};
} // namespace soundcurrent::daw
