// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include <pipewire/filter.h>
#include <spa/node/io.h>
#include <atomic>
#include <cstdint>
namespace soundcurrent::daw::native {
// Prepared port metadata lives until both native loops are joined. PipeWire's
// synchronous per-port IO area is valid until its serialized replacement/removal.
// This atomic publishes notification readiness; it is not a reclamation scheme.
struct Port {
    void *key = nullptr;
    bool input = false;
    std::atomic<spa_io_buffers *> io{nullptr};
};
static_assert(std::atomic<spa_io_buffers *>::is_always_lock_free);
enum class Acquisition { Unavailable, Ready, Invalid, Silence };
struct Buffer {
    pw_buffer *owned = nullptr;
    float *samples = nullptr;
    Acquisition status = Acquisition::Unavailable;
};
void ioChanged(Port &, std::uint32_t id, void *area, std::uint32_t bytes) noexcept;
} // namespace soundcurrent::daw::native
// Separate translation unit permits test-only linker observation of each logical
// channel attempt, independently of SDK dequeue/queue operations. No test hooks,
// clocks, waits, allocation, logging or GUI work belong in these functions.
extern "C" soundcurrent::daw::native::Buffer sc_pw_acquire_buffer(soundcurrent::daw::native::Port *,
                                                                  std::uint32_t frames) noexcept;
extern "C" bool sc_pw_release_buffer(soundcurrent::daw::native::Port *,
                                     soundcurrent::daw::native::Buffer *) noexcept;
