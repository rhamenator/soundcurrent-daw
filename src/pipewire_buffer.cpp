// SPDX-License-Identifier: GPL-3.0-only
#include "pipewire_buffer.hpp"
#include <cstddef>
#include <limits>
namespace soundcurrent::daw::native {
void ioChanged(Port &port, std::uint32_t id, void *area, std::uint32_t bytes) noexcept {
    if (id == SPA_IO_Buffers || id == SPA_IO_AsyncBuffers)
        port.io.store(id == SPA_IO_Buffers && bytes >= sizeof(spa_io_buffers)
                          ? static_cast<spa_io_buffers *>(area)
                          : nullptr,
                      std::memory_order_release);
}
} // namespace soundcurrent::daw::native
extern "C" soundcurrent::daw::native::Buffer
sc_pw_acquire_buffer(soundcurrent::daw::native::Port *port, std::uint32_t frames) noexcept {
    using namespace soundcurrent::daw::native;
    if (!port || !port->key || !frames ||
        frames > std::numeric_limits<std::uint32_t>::max() / sizeof(float))
        return {nullptr, nullptr, Acquisition::Invalid};
    const auto *io = port->io.load(std::memory_order_acquire);
    if (!io)
        return {};
    if (port->input ? io->status != SPA_STATUS_HAVE_DATA || io->buffer_id == SPA_ID_INVALID
                    : io->status != SPA_STATUS_OK && io->status != SPA_STATUS_NEED_DATA)
        return {};
    auto *buffer = pw_filter_dequeue_buffer(port->key);
    if (!buffer)
        return {};
    // Keep even invalid buffers owned for explicit return at callback completion.
    Buffer result{buffer, nullptr, Acquisition::Invalid};
    if (!buffer->buffer || buffer->buffer->n_datas != 1 || !buffer->buffer->datas)
        return result;
    auto &d = buffer->buffer->datas[0];
    const auto bytes = frames * std::uint32_t(sizeof(float));
    if (!d.data || !d.chunk || std::uintptr_t(d.data) % alignof(float) || d.maxsize < bytes ||
        !(d.flags & (port->input ? SPA_DATA_FLAG_READABLE : SPA_DATA_FLAG_WRITABLE)))
        return result;
    auto &chunk = *d.chunk;
    if (port->input) {
        // This adapter admits contiguous, non-wrapped mono F32 DSP chunks only.
        if (chunk.offset % sizeof(float) || chunk.offset > d.maxsize ||
            bytes > d.maxsize - chunk.offset || chunk.size > d.maxsize - chunk.offset ||
            chunk.stride != sizeof(float) ||
            (chunk.flags != SPA_CHUNK_FLAG_NONE && chunk.flags != SPA_CHUNK_FLAG_EMPTY))
            return result;
        if (chunk.flags == SPA_CHUNK_FLAG_EMPTY) {
            // SPA declares neutral media. Never expose stale backing bytes as
            // audio; the filter supplies an immutable preparation-owned zero plane.
            result.status = Acquisition::Silence;
            return result;
        }
        if (chunk.size < bytes)
            return result;
        result.samples = reinterpret_cast<float *>(static_cast<std::byte *>(d.data) + chunk.offset);
    } else {
        // Metadata changes follow extent certification, never precede it.
        chunk.offset = 0;
        chunk.size = bytes;
        chunk.stride = sizeof(float);
        chunk.flags = SPA_CHUNK_FLAG_NONE;
        result.samples = static_cast<float *>(d.data);
    }
    result.status = Acquisition::Ready;
    return result;
}
extern "C" bool sc_pw_release_buffer(soundcurrent::daw::native::Port *port,
                                     soundcurrent::daw::native::Buffer *lease) noexcept {
    using namespace soundcurrent::daw::native;
    if (!port || !lease)
        return false;
    if (!lease->owned)
        return true;
    auto *buffer = lease->owned;
    if (!port->input && lease->status == Acquisition::Invalid && buffer->buffer &&
        buffer->buffer->n_datas > 0 && buffer->buffer->datas && buffer->buffer->datas[0].chunk) {
        // Do not publish stale or over-capacity output metadata on refusal.
        auto &chunk = *buffer->buffer->datas[0].chunk;
        chunk.offset = 0;
        chunk.size = 0;
        chunk.stride = sizeof(float);
        chunk.flags = SPA_CHUNK_FLAG_EMPTY;
    }
    *lease = {};
    return pw_filter_queue_buffer(port->key, buffer) >= 0;
}
