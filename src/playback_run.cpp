// SPDX-License-Identifier: GPL-3.0-only
#include <soundcurrent/playback_reader.hpp>
#include <atomic>
#include <chrono>
#include <exception>
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <thread>
#endif
namespace soundcurrent::daw {
struct PlaybackRun::State {
    PlaybackPipe pipe;
    PlaybackProcessor processor;
    std::unique_ptr<TrackReader> reader;
    std::atomic<std::uint32_t> canceled{0}, stopRequested{0};
    std::exception_ptr error;
#ifdef _WIN32
    HANDLE thread = nullptr;
    static DWORD WINAPI entry(void *p) {
        static_cast<State *>(p)->run();
        return 0;
    }
#else
    std::thread thread;
#endif
    State(const Session &s, const Id &id, PlaybackConfig c) : pipe(c), processor(s, id, pipe) {}
    void run() noexcept {
        try {
            while (!pipe.readerDone() && !canceled.load(std::memory_order_acquire)) {
                if (!reader->fillOne()) {
#ifdef _WIN32
                    Sleep(1);
#else
                    std::this_thread::sleep_for(std::chrono::milliseconds(1));
#endif
                }
            }
            if (!pipe.readerDone())
                pipe.finishReader(true);
        } catch (...) {
            error = std::current_exception();
            pipe.finishReader(true);
        }
    }
    void join() noexcept {
#ifdef _WIN32
        if (thread) {
            WaitForSingleObject(thread, INFINITE);
            CloseHandle(thread);
            thread = nullptr;
        }
#else
        if (thread.joinable())
            thread.join();
#endif
    }
};
PlaybackRun::PlaybackRun(std::filesystem::path root, const Session &s, const Id &id,
                         PlaybackConfig config, ReadAheadOptions options)
    : state_(std::make_unique<State>(s, id, config)) {
    state_->reader =
        std::make_unique<TrackReader>(state_->pipe, std::move(root), s, id, std::move(options));
    // Bounded pool prefill on preparation owner, before callback publication.
    while (state_->reader->fillOne()) {
    }
    if (!state_->pipe.readerDone()) {
#ifdef _WIN32
        state_->thread = CreateThread(nullptr, 0, State::entry, state_.get(), 0, nullptr);
        if (!state_->thread)
            throw ProjectError(ErrorCode::Io, "Cannot start playback reader worker");
#else
        state_->thread = std::thread([p = state_.get()] { p->run(); });
#endif
    }
}
PlaybackRun::~PlaybackRun() {
    cancelReader();
    state_->join();
}
PlaybackReport PlaybackRun::process(std::span<float *const> out, std::uint32_t n) noexcept {
    if (state_->stopRequested.load(std::memory_order_acquire))
        state_->processor.stop();
    auto result = state_->processor.process(out, n);
    if (result.status != PlaybackStatus::Running && result.status != PlaybackStatus::Underflow &&
        result.status != PlaybackStatus::InvalidBuffer)
        state_->canceled.store(1, std::memory_order_release);
    return result;
}
PreparedEq &PlaybackRun::prepared() noexcept {
    return state_->processor.prepared();
}
SubmitStatus PlaybackRun::submit(const EqEvent &e) noexcept {
    return state_->processor.submit(e);
}
const PlaybackConfig &PlaybackRun::config() const noexcept {
    return state_->pipe.config();
}
Frame PlaybackRun::position() const noexcept {
    return state_->pipe.position();
}
std::uint64_t PlaybackRun::missingFrames() const noexcept {
    return state_->pipe.missingFrames();
}
std::uint64_t PlaybackRun::bufferedFrames() const noexcept {
    return state_->pipe.bufferedFrames();
}
bool PlaybackRun::readerDone() const noexcept {
    return state_->pipe.readerDone();
}
std::uint64_t PlaybackRun::sanitizedSamples() const noexcept {
    return state_->reader->sanitizedSamples();
}
void PlaybackRun::requestStop() noexcept {
    state_->stopRequested.store(1, std::memory_order_release);
}
void PlaybackRun::cancelReader() noexcept {
    state_->canceled.store(1, std::memory_order_release);
}
void PlaybackRun::waitReader() {
    state_->join();
    if (state_->error)
        std::rethrow_exception(state_->error);
}
} // namespace soundcurrent::daw
