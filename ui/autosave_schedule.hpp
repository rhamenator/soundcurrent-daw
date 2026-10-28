// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include <cstdint>

namespace soundcurrent::daw::ui {
// Control-owner only. No queued snapshot: the caller borrows its latest eligible
// revision when due. Attempts, including failures, restart the interval.
class AutosaveSchedule {
  public:
    bool due(std::uint64_t now, std::uint64_t interval, std::uint64_t epoch,
             bool eligible, bool dirty, std::uint64_t revision,
             std::uint64_t autosavedRevision) noexcept {
        if (!initialized_ || epoch_ != epoch || now < last_ || interval_ != interval) {
            initialized_ = true; epoch_ = epoch; last_ = now; interval_ = interval;
            return false;
        }
        if (!interval || !epoch || !eligible || !dirty || revision == autosavedRevision ||
            now - last_ < interval) return false;
        last_ = now;
        return true;
    }
  private:
    std::uint64_t epoch_ = 0, last_ = 0, interval_ = 0;
    bool initialized_ = false;
};
} // namespace soundcurrent::daw::ui
