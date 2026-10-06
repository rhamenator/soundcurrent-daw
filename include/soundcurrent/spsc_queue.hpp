// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <type_traits>

namespace soundcurrent::daw {
// One producer and one consumer. Do not reset or destroy while either is active.
// Monotonic unsigned counters intentionally wrap; occupancy is at most Capacity.
template <class T, std::size_t Capacity> class SpscQueue {
    static_assert(std::is_trivially_copyable_v<T>);
    static_assert(Capacity > 0 && (Capacity & (Capacity - 1)) == 0);
    static_assert(Capacity < (std::uint64_t{1} << 31));
    static_assert(std::atomic<std::uint32_t>::is_always_lock_free);

  public:
    explicit SpscQueue(std::uint32_t initialSequence = 0) noexcept
        : write_(initialSequence), read_(initialSequence) {}
    bool tryPush(const T &value) noexcept {
        const auto w = write_.load(std::memory_order_relaxed);
        if (w - read_.load(std::memory_order_acquire) == Capacity)
            return false;
        data_[w & (Capacity - 1)] = value;
        write_.store(w + 1, std::memory_order_release);
        return true;
    }
    bool tryPeek(T &value) const noexcept {
        const auto r = read_.load(std::memory_order_relaxed);
        if (r == write_.load(std::memory_order_acquire))
            return false;
        value = data_[r & (Capacity - 1)];
        return true;
    }
    bool tryPop(T &value) noexcept {
        const auto r = read_.load(std::memory_order_relaxed);
        if (r == write_.load(std::memory_order_acquire))
            return false;
        value = data_[r & (Capacity - 1)];
        read_.store(r + 1, std::memory_order_release);
        return true;
    }
    bool producerHasSpace() const noexcept {
        return write_.load(std::memory_order_relaxed) - read_.load(std::memory_order_acquire) <
               Capacity;
    }
    // Consumer only: snapshot the published prefix before a bounded drain.
    // Subsequent producer publications do not enlarge that drain's budget.
    std::uint32_t consumerAvailable() const noexcept {
        const auto r = read_.load(std::memory_order_relaxed);
        return write_.load(std::memory_order_acquire) - r;
    }

  private:
    alignas(64) std::atomic<std::uint32_t> write_{0};
    alignas(64) std::atomic<std::uint32_t> read_{0};
    alignas(64) std::array<T, Capacity> data_{};
};
} // namespace soundcurrent::daw
