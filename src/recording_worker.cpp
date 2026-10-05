// SPDX-License-Identifier: GPL-3.0-only
#include <soundcurrent/recording.hpp>
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
static_assert(std::atomic<Frame>::is_always_lock_free);
struct RecordingWorker::State {
    CapturePipe &pipe;
    std::atomic<std::uint32_t> canceled{0}, done{0};
    std::atomic<Frame> written{0};
    std::unique_ptr<CaptureWriter> writer;
    std::optional<RecordingResult> result;
    std::exception_ptr error;
#ifdef _WIN32
    HANDLE thread = nullptr;
    static DWORD WINAPI entry(void *s) {
        static_cast<State *>(s)->run();
        return 0;
    }
#else
    std::thread thread;
#endif
    explicit State(CapturePipe &p) : pipe(p) {}
    void checkCanceled() {
        if (canceled.load(std::memory_order_acquire))
            throw ProjectError(ErrorCode::Canceled, "Recording canceled; checkpoint retained");
    }
    void run() noexcept {
        try {
            while (!pipe.drained()) {
                checkCanceled();
                if (writer->drainOne(pipe))
                    written.store(writer->writtenFrames(), std::memory_order_release);
                else {
#ifdef _WIN32
                    Sleep(1);
#else
                    std::this_thread::sleep_for(std::chrono::milliseconds(1));
#endif
                }
            }
            checkCanceled();
            result = writer->finalize(pipe);
            written.store(result->asset.frames, std::memory_order_release);
        } catch (...) {
            pipe.writerFailed();
            error = std::current_exception();
        }
        done.store(1, std::memory_order_release);
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
RecordingWorker::RecordingWorker(CapturePipe &pipe, std::filesystem::path root, RecordingSpec spec,
                                 RecordingOptions options)
    : state_(std::make_unique<State>(pipe)) {
    if (pipe.config() != prepareCaptureConfig(spec.capture))
        throw ProjectError(ErrorCode::InvalidState, "Disk worker/capture configuration mismatch");
    auto callback = std::move(options.boundary);
    options.boundary = [s = state_.get(), callback = std::move(callback)](RecordingBoundary b,
                                                                          Frame f) {
        s->checkCanceled();
        if (callback)
            callback(b, f);
    };
    state_->writer =
        std::make_unique<CaptureWriter>(std::move(root), std::move(spec), std::move(options));
#ifdef _WIN32
    state_->thread = CreateThread(nullptr, 0, State::entry, state_.get(), 0, nullptr);
    if (!state_->thread) {
        pipe.writerFailed();
        throw ProjectError(ErrorCode::Io, "Cannot start recording disk worker");
    }
#else
    try {
        state_->thread = std::thread([s = state_.get()] { s->run(); });
    } catch (...) {
        pipe.writerFailed();
        throw;
    }
#endif
}
RecordingWorker::~RecordingWorker() {
    cancel();
    state_->join();
}
void RecordingWorker::cancel() noexcept {
    state_->canceled.store(1, std::memory_order_release);
}
bool RecordingWorker::complete() const noexcept {
    return state_->done.load(std::memory_order_acquire) != 0;
}
Frame RecordingWorker::writtenFrames() const noexcept {
    return state_->written.load(std::memory_order_acquire);
}
const std::filesystem::path &RecordingWorker::jobDirectory() const noexcept {
    return state_->writer->jobDirectory();
}
RecordingResult RecordingWorker::wait() {
    state_->join();
    if (state_->error)
        std::rethrow_exception(state_->error);
    if (!state_->result)
        throw ProjectError(ErrorCode::InvalidState, "Recording worker has no result");
    return *state_->result;
}
} // namespace soundcurrent::daw
