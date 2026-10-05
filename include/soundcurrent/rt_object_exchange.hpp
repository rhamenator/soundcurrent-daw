// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include "spsc_queue.hpp"
#include <memory>
#include <stdexcept>

namespace soundcurrent::daw {
// Control owns allocations, publication and destruction; one audio owner borrows
// tokens. The audio owner finishes only after its last use of the previous object.
// This provides lifetime transport, not graph compilation or worker epoch tracking.
template <class T> class RtObjectExchange {
    static_assert(std::is_nothrow_destructible_v<T>);

  public:
    explicit RtObjectExchange(std::unique_ptr<T> initial) {
        if (!initial)
            throw std::invalid_argument("Initial object is required");
        owners_[0] = std::move(initial);
        active_ = {owners_[0].get(), 0};
    }
    RtObjectExchange(const RtObjectExchange &) = delete;
    RtObjectExchange &operator=(const RtObjectExchange &) = delete;
    ~RtObjectExchange() = default; // Control side, after callback has stopped.

    // Control producer only. On backpressure the caller keeps its prepared object.
    bool publish(std::unique_ptr<T> &prepared) noexcept {
        if (!prepared || !ready_.producerHasSpace())
            return false;
        for (std::size_t slot = 0; slot < owners_.size(); ++slot)
            if (!owners_[slot]) {
                owners_[slot] = std::move(prepared);
                Token token{owners_[slot].get(), static_cast<std::uint32_t>(slot)};
                if (ready_.tryPush(token))
                    return true;
                prepared = std::move(owners_[slot]);
                return false;
            }
        return false;
    }
    // Control consumer only. No object may be destroyed by the audio owner.
    std::size_t collectRetired() noexcept {
        std::size_t count = 0;
        Token token{};
        while (retired_.tryPop(token)) {
            owners_[token.slot].reset();
            ++count;
        }
        return count;
    }
    // Audio owner only, at a block boundary. Reserve retirement capacity before
    // consuming a publication. Retain the active object when credits are exhausted.
    bool beginReplacement() noexcept {
        if (previous_.object || !retired_.producerHasSpace())
            return false;
        Token next{};
        if (!ready_.tryPop(next))
            return false;
        previous_ = active_;
        active_ = next;
        return true;
    }
    T &active() noexcept {
        return *active_.object;
    } // Audio owner only.
    T *previous() noexcept {
        return previous_.object;
    } // Crossfade/tail borrowing.
    bool finishReplacement() noexcept {
        if (!previous_.object || !retired_.tryPush(previous_))
            return false;
        previous_ = {};
        return true;
    }

  private:
    struct Token {
        T *object = nullptr;
        std::uint32_t slot = 0;
    };
    // 8 retired + active + previous + 2 ready. Only control accesses owners_.
    std::array<std::unique_ptr<T>, 12> owners_{};
    SpscQueue<Token, 2> ready_;
    SpscQueue<Token, 8> retired_;
    Token active_{}, previous_{};
};
} // namespace soundcurrent::daw
