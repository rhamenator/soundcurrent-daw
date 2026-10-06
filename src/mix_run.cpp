// SPDX-License-Identifier: GPL-3.0-only
#include <soundcurrent/mix_reader.hpp>
#include <exception>
#include <chrono>
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <thread>
#endif
namespace soundcurrent::daw {
struct MixPlaybackRun::State {
    MixPlayback mix;
    const std::uint32_t rate;
    std::unique_ptr<MixReader> reader;
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
    State(const Session &s, MixPlan p, MixPlaybackConfig c)
        : mix(s, std::move(p), c), rate(s.sampleRate) {}
    void run() noexcept {
        try {
            while (!mix.readerDone() && !canceled.load(std::memory_order_acquire)) {
                if (!reader->fillRound()) {
#ifdef _WIN32
                    Sleep(1);
#else
                    std::this_thread::sleep_for(std::chrono::milliseconds(1));
#endif
                }
            }
            if (!mix.readerDone())
                failReaders();
        } catch (...) {
            error = std::current_exception();
            failReaders();
        }
    }
    void failReaders() noexcept {
        for (std::size_t t = 0; t < mix.graph().plan().tracks.size(); ++t)
            mix.pipe(t).finishReader(true);
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
MixPlaybackRun::MixPlaybackRun(std::filesystem::path root, const Session &s, MixPlan p,
                               MixPlaybackConfig c, ReadAheadOptions o, std::uint32_t references)
    : state_(std::make_unique<State>(s, std::move(p), c)) {
    state_->reader =
        std::make_unique<MixReader>(state_->mix, std::move(root), s, std::move(o), references);
    while (state_->reader->fillRound()) {
    }
    if (!state_->mix.readerDone()) {
#ifdef _WIN32
        state_->thread = CreateThread(nullptr, 0, State::entry, state_.get(), 0, nullptr);
        if (!state_->thread)
            throw ProjectError(ErrorCode::Io, "Cannot start mix reader worker");
#else
        state_->thread = std::thread([p = state_.get()] { p->run(); });
#endif
    }
}
MixPlaybackRun::~MixPlaybackRun() {
    cancelReader();
    state_->join();
}
MixPlaybackReport MixPlaybackRun::process(std::span<float *const> out, std::uint32_t n,
                                          std::span<const LiveMixInput> live) noexcept {
    if (state_->stopRequested.load(std::memory_order_acquire))
        state_->mix.stop();
    auto r = state_->mix.process(out, n, live);
    if (r.status != PlaybackStatus::Running && r.status != PlaybackStatus::Underflow &&
        r.status != PlaybackStatus::InvalidBuffer)
        state_->canceled.store(1, std::memory_order_release);
    return r;
}
PreparedMixGraph &MixPlaybackRun::graph() noexcept {
    return state_->mix.graph();
}
Frame MixPlaybackRun::position() const noexcept {
    return state_->mix.position();
}
const MixPlaybackConfig &MixPlaybackRun::config() const noexcept {
    return state_->mix.config();
}
std::uint32_t MixPlaybackRun::sampleRate() const noexcept {
    return state_->rate;
}
bool MixPlaybackRun::readerDone() const noexcept {
    return state_->mix.readerDone();
}
std::uint64_t MixPlaybackRun::missingTrackFrames() const noexcept {
    return state_->mix.missingTrackFrames();
}
std::uint64_t MixPlaybackRun::sanitizedSamples() const noexcept {
    return state_->reader->sanitizedSamples();
}
void MixPlaybackRun::requestStop() noexcept {
    state_->stopRequested.store(1, std::memory_order_release);
}
void MixPlaybackRun::cancelReader() noexcept {
    state_->canceled.store(1, std::memory_order_release);
}
void MixPlaybackRun::waitReader() {
    state_->join();
    if (state_->error)
        std::rethrow_exception(state_->error);
}
} // namespace soundcurrent::daw
