// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include "session.hpp"
#include <atomic>
#include <bit>
#include <memory>
#include <type_traits>
namespace soundcurrent::daw {
// One producer/consumer, allocation/value initialization only during preparation.
// No reset/resize; retire after both owners join. Counter wrap is deliberate.
template<class T>class PreparedSpscQueue {
    static_assert(std::is_trivially_copyable_v<T>);
    static_assert(std::atomic<std::uint32_t>::is_always_lock_free);
  public:
    static std::uint32_t storageCapacity(std::uint32_t requested){if(!requested || requested>4096)throw ProjectError(ErrorCode::InvalidParameter,"Prepared queue capacity outside bound");return std::bit_ceil(requested);}
    explicit PreparedSpscQueue(std::uint32_t requested,std::uint32_t initial=0):capacity_(storageCapacity(requested)),mask_(capacity_-1),data_(std::make_unique<T[]>(capacity_)),write_(initial),read_(initial){}
    bool tryPush(const T &v) noexcept{const auto w=write_.load(std::memory_order_relaxed);if(w-read_.load(std::memory_order_acquire)==capacity_)return false;data_[w&mask_]=v;write_.store(w+1,std::memory_order_release);return true;}
    bool tryPop(T &v) noexcept{const auto r=read_.load(std::memory_order_relaxed);if(r==write_.load(std::memory_order_acquire))return false;v=data_[r&mask_];read_.store(r+1,std::memory_order_release);return true;}
    std::uint32_t consumerAvailable() const noexcept{return write_.load(std::memory_order_acquire)-read_.load(std::memory_order_relaxed);}
  private:
    const std::uint32_t capacity_,mask_;std::unique_ptr<T[]> data_;
    alignas(64)std::atomic<std::uint32_t> write_,read_;
};
}
