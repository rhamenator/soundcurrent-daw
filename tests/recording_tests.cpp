// SPDX-License-Identifier: GPL-3.0-only
#include <sndfile.h>
#include "rt_audit.hpp"
#include <soundcurrent/recording.hpp>
#include <array>
#include <atomic>
#include <chrono>
#include <cmath>
#include <exception>
#include <fstream>
#include <iostream>
#include <nlohmann/json.hpp>
#include <thread>
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <csignal>
#include <sys/resource.h>
#include <sys/wait.h>
#include <unistd.h>
#endif

using namespace soundcurrent::daw;
namespace {
std::uint64_t checks = 0;
void check(bool ok, const char *text) {
    ++checks;
    if (!ok)
        throw std::runtime_error(text);
}
template <class F> void rejects(F f) {
    bool caught = false;
    try {
        f();
    } catch (const ProjectError &) {
        caught = true;
    }
    check(caught, "Invalid recording operation accepted");
}
struct Temp {
    std::filesystem::path root =
        std::filesystem::temp_directory_path() / ("sc-record-" + Id::generate().str());
    Temp() {
        std::filesystem::create_directory(root);
    }
    ~Temp() {
        std::error_code e;
        std::filesystem::remove_all(root, e);
    }
};
float signal(Frame f, std::uint32_t c) {
    return float((double(f % 101) - 50) * .04 + double(c) * .125);
}
struct Source {
    std::uint32_t channels, quantum;
    std::vector<float> planar;
    std::vector<const float *> pointers;
    Source(std::uint32_t c, std::uint32_t q)
        : channels(c), quantum(q), planar(std::size_t(c) * q), pointers(c) {
        for (std::uint32_t ch = 0; ch < c; ++ch)
            pointers[ch] = planar.data() + std::size_t(ch) * q;
    }
    void fill(Frame frame, std::uint32_t n) {
        for (std::uint32_t c = 0; c < channels; ++c)
            for (std::uint32_t f = 0; f < n; ++f)
                planar[std::size_t(c) * quantum + f] = signal(frame + f, c);
    }
    CaptureReport push(CapturePipe &pipe, Frame frame, std::uint32_t n) {
        fill(frame, n);
        rt_audit::Guard guard;
        return pipe.push(pointers, n, frame);
    }
};
RecordingSpec specFor(const Session &s, std::uint32_t slab = 1024) {
    RecordingSpec spec;
    spec.projectId = s.id;
    spec.trackId = s.tracks.front().id;
    spec.capture.sampleRate = s.sampleRate;
    spec.capture.layout = s.tracks.front().layout;
    spec.capture.maximumCallbackFrames = 2048;
    spec.capture.slabFrames = slab;
    spec.capture = prepareCaptureConfig(spec.capture);
    return spec;
}
void verifyAudio(const std::filesystem::path &file, Frame count, std::uint32_t channels,
                 Frame start = 0) {
    SF_INFO info{};
    auto *reader = sf_open(file.string().c_str(), SFM_READ, &info);
    check(reader && info.frames == count && info.channels == static_cast<int>(channels) &&
              info.samplerate == 48000 && (info.format & SF_FORMAT_TYPEMASK) == SF_FORMAT_RF64 &&
              (info.format & SF_FORMAT_SUBMASK) == SF_FORMAT_FLOAT,
          "Published recording header mismatch");
    std::vector<float> samples(std::size_t(1024) * channels);
    bool correct = true;
    for (Frame f = 0; f < count;) {
        const auto n = std::min<Frame>(1024, count - f);
        if (sf_readf_float(reader, samples.data(), n) != n) {
            correct = false;
            break;
        }
        for (Frame i = 0; i < n; ++i)
            for (std::uint32_t c = 0; c < channels; ++c)
                if (samples[std::size_t(i) * channels + c] != signal(start + f + i, c))
                    correct = false;
        f += n;
    }
    check(sf_close(reader) == 0 && correct, "Raw recording samples differ or overs clipped");
}
void concurrentTake() {
    Temp temp;
    auto session = makeOneTrackSession("Enregistrement – Aufnahme", "Prise / Aufnahme");
    const auto spec = specFor(session);
    CapturePipe pipe(spec.capture);
    RecordingWorker worker(pipe, temp.root, spec);
    Source source(1, 127);
    constexpr Frame total = 480000;
    bool correct = true;
    for (Frame f = 0; f < total;) {
        const auto n = static_cast<std::uint32_t>(std::min<Frame>(source.quantum, total - f));
        const auto r = source.push(pipe, f, n);
        if (r.acceptedFrames != n) {
            correct = false;
            break;
        }
        f += n;
        // Synthetic producer pacing outside the marked callback. This is not
        // a physical clock/latency benchmark or native PipeWire qualification.
#ifdef _WIN32
        Sleep(1);
#else
        std::this_thread::sleep_for(std::chrono::microseconds(100));
#endif
    }
    {
        rt_audit::Guard guard;
        pipe.finish();
    }
    const auto result = worker.wait();
    check(correct && worker.complete() && result.asset.frames == total &&
              worker.writtenFrames() == total,
          "Concurrent ten-second capture incomplete");
    verifyAudio(temp.root / utf8Path(result.asset.relativePath), total, 1);
    const auto recovery = inspectRecording(worker.jobDirectory());
    check(recovery.finalized && recovery.committedFrames == total &&
              recovery.captureStatus == CaptureStatus::Stopped,
          "Finalized journal extent/status differs");
    attachRecording(session, result);
    ProjectStore store(temp.root);
    store.save(session);
    check(store.load() == session, "Recorded project save/reopen differs");
    const auto moved = temp.root.parent_path() / ("sc-relocated-" + Id::generate().str());
    std::filesystem::rename(temp.root, moved);
    temp.root = moved;
    check(ProjectStore(moved).load() == session, "Moved recorded project lost media");
    auto unchanged = session;
    rejects([&] { attachRecording(session, result); });
    check(session == unchanged, "Failed attach altered model");
}
void layoutsAndAlignment() {
    for (const auto channels : {2u, 8u, 32u, 256u}) {
        Temp temp;
        auto session = makeOneTrackSession("Layouts", "Raw");
        session.tracks.front().layout = {LayoutKind::Discrete, channels};
        auto spec = specFor(session, 256);
        spec.capture.startFrame = 72;
        spec.inputLatencyFrames = 100;
        CapturePipe pipe(spec.capture);
        CaptureWriter writer(temp.root, spec, {256, {}});
        Source source(channels, 127);
        for (Frame f = 0; f < 1003;) {
            const auto n = static_cast<std::uint32_t>(std::min<Frame>(127, 1003 - f));
            check(source.push(pipe, 72 + f, n).acceptedFrames == n,
                  "Multichannel capture rejected");
            while (writer.drainOne(pipe)) {
            }
            f += n;
        }
        pipe.finish();
        while (writer.drainOne(pipe)) {
        }
        const auto result = writer.finalize(pipe);
        verifyAudio(temp.root / utf8Path(result.asset.relativePath), 1003, channels, 72);
        attachRecording(session, result);
        check(session.assets.front().frames == 1003 &&
                  session.tracks.front().clips.front().sourceFrame == 28 &&
                  session.tracks.front().clips.front().startFrame == 0 &&
                  session.tracks.front().clips.front().lengthFrames == 975,
              "Non-destructive recording alignment incorrect");
        check(inspectRecording(writer.jobDirectory()).committedFrames == 1003,
              "Multichannel prefix verification failed");
    }
}
void writerObservations() {
    Temp temp;
    const auto spec = specFor(makeOneTrackSession("Diagnostics", "Raw"), 256);
    CapturePipe pipe(spec.capture);
    struct Observations {
        std::array<RecordingWriterObservation, 64> records{};
        unsigned count = 0;
        bool onAudio = false, overflow = false;
        static void receive(void *p, const RecordingWriterObservation &o) noexcept {
            auto &s = *static_cast<Observations *>(p);
            s.onAudio |= rt_audit::active;
            if (s.count < s.records.size())
                s.records[s.count++] = o;
            else
                s.overflow = true;
        }
    } observations;
    RecordingOptions options;
    options.checkpointFrames = 512;
    options.instrumentation = {&observations, Observations::receive};
    CaptureWriter writer(temp.root, spec, options);
    Source source(1, 512);
    check(source.push(pipe, 0, 512).acceptedFrames == 512, "Diagnostic capture rejected");
    check(writer.drainOne(pipe) && writer.drainOne(pipe), "Diagnostic slabs not written");
    check(source.push(pipe, 512, 37).acceptedFrames == 37, "Diagnostic partial rejected");
    {
        rt_audit::Guard guard;
        pipe.finish();
    }
    check(writer.drainOne(pipe) && !writer.drainOne(pipe), "Diagnostic partial not drained");
    const auto result = writer.finalize(pipe);
    check(!observations.onAudio && !observations.overflow && observations.count == 20,
          "Writer observations ran on audio, overflowed or lost phase events");
    for (unsigned n = 0; n < 4; ++n)
        check(!observations.records[n].hasBacklog, "Construction invents producer backlog");
    const auto first = observations.records[4];
    check(first.phase == RecordingWriterPhase::WriteHashBegin && first.writtenFrames == 0 &&
              first.committedFrames == 0 && first.backlog.readySlabs == 1 &&
              first.backlog.acquiredFrames == 256 && first.backlog.queuedFrameUpperBound == 512,
          "Write phase queue/owned slab observation differs");
    const auto flush = observations.records[8];
    check(flush.phase == RecordingWriterPhase::AudioFlushBegin && flush.writtenFrames == 512 &&
              flush.committedFrames == 0 && !flush.backlog.readySlabs &&
              !flush.backlog.acquiredFrames,
          "Audio flush occurs before slab return or has wrong durable cursor");
    const auto last = observations.records[19];
    check(last.phase == RecordingWriterPhase::JournalPublishEnd && last.writtenFrames == 549 &&
              last.committedFrames == 549 && last.hasBacklog && !last.backlog.queuedFrameUpperBound,
          "Final journal observation precedes commit or retains drained backlog");
    verifyAudio(temp.root / utf8Path(result.asset.relativePath), 549, 1);
    check(inspectRecording(writer.jobDirectory(), {}, true).committedFrames == 549,
          "Instrumented writer changed durable prefix");
}
void workerCancellation() {
    Temp temp;
    auto spec = specFor(makeOneTrackSession("Cancel", "Raw"), 256);
    CapturePipe pipe(spec.capture);
    std::filesystem::path job;
    {
        RecordingWorker worker(pipe, temp.root, spec, {256, {}});
        job = worker.jobDirectory();
        Source source(1, 256);
        source.push(pipe, 0, 256);
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
        while (worker.writtenFrames() < 256 && !worker.complete() &&
               std::chrono::steady_clock::now() < deadline) {
#ifdef _WIN32
            Sleep(1);
#else
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
#endif
        }
        const bool committed = worker.writtenFrames() == 256;
        worker.cancel();
        pipe.finish();
        bool canceled = false;
        try {
            worker.wait();
        } catch (const ProjectError &e) {
            canceled = e.code() == ErrorCode::Canceled;
        }
        check(committed && canceled && worker.complete() &&
                  pipe.status() == CaptureStatus::WriterFailed,
              "Worker cancellation did not surface or join");
    }
    check(inspectRecording(job).committedFrames == 256 &&
              !std::filesystem::exists(job / "take.wav"),
          "Cancel published a take or lost checkpoint");
}
void queuedGap() {
    Temp temp;
    const auto spec = specFor(makeOneTrackSession("Gap", "Raw"), 256);
    CapturePipe pipe(spec.capture);
    CaptureWriter writer(temp.root, spec, {256, {}});
    Source source(1, 256);
    for (Frame f = 0; f < 8192; f += 256)
        check(source.push(pipe, f, 256).acceptedFrames == 256, "Queue filled too early");
    const auto gap = source.push(pipe, 8192, 32);
    check(gap.status == CaptureStatus::QueueFull && gap.rejectedFrames == 32,
          "Queue gap not reported");
    pipe.finish();
    while (writer.drainOne(pipe)) {
    }
    const auto result = writer.finalize(pipe);
    const auto recovered = inspectRecording(writer.jobDirectory());
    check(result.captureStatus == CaptureStatus::QueueFull && result.asset.frames == 8192 &&
              recovered.rejectedFrames == 32 && recovered.captureStatus == CaptureStatus::QueueFull,
          "Truncated take lost persistent gap/status");
    verifyAudio(temp.root / utf8Path(result.asset.relativePath), 8192, 1);
}
void sanitizedJournal() {
    Temp temp;
    const auto spec = specFor(makeOneTrackSession("Invalid samples", "Raw"), 256);
    CapturePipe pipe(spec.capture);
    CaptureWriter writer(temp.root, spec, {256, {}});
    Source source(1, 256);
    source.fill(0, 256);
    source.planar[5] = std::numeric_limits<float>::infinity();
    source.planar[7] = std::numeric_limits<float>::quiet_NaN();
    CaptureReport report;
    {
        rt_audit::Guard guard;
        report = pipe.push(source.pointers, 256, 0);
        pipe.finish();
    }
    while (writer.drainOne(pipe)) {
    }
    const auto result = writer.finalize(pipe);
    const auto recovery = inspectRecording(writer.jobDirectory());
    check(report.invalidInputSamples == 2 && recovery.observedInvalidInputSamples == 2 &&
              result.asset.frames == 256,
          "Invalid-input diagnostic was not persisted");
}
void interruptedBoundary(RecordingBoundary failure, bool duringFinalize) {
    Temp temp;
    auto session = makeOneTrackSession("Interruption", "Prefix");
    auto spec = specFor(session, 256);
    std::filesystem::path job;
    Frame expected = duringFinalize ? 512 : 256;
    {
        CapturePipe pipe(spec.capture);
        bool armed = false;
        RecordingOptions options{256, [&](RecordingBoundary b, Frame) {
                                     if (armed && b == failure)
                                         throw ProjectError(ErrorCode::Io,
                                                            "Injected worker I/O interruption");
                                 }};
        CaptureWriter writer(temp.root, spec, options);
        job = writer.jobDirectory();
        Source source(1, 256);
        source.push(pipe, 0, 256);
        check(writer.drainOne(pipe) && writer.checkpointFrames() == 256,
              "Initial checkpoint failed");
        armed = !duringFinalize;
        source.push(pipe, 256, 256);
        if (duringFinalize) {
            check(writer.drainOne(pipe), "Second checkpoint failed");
            pipe.finish();
            armed = true;
            rejects([&] { writer.finalize(pipe); });
        } else {
            rejects([&] { writer.drainOne(pipe); });
            const auto r = source.push(pipe, 512, 256);
            check(r.status == CaptureStatus::WriterFailed && r.acceptedFrames == 0,
                  "Disk failure not reported to audio owner");
            pipe.finish();
        }
    }
    const auto before = inspectRecording(job);
    check(before.committedFrames == expected && !before.finalized,
          "Failed publication changed committed prefix");
    const auto originalHash = hashMediaFile(before.source);
    const auto originalJournal = hashMediaFile(job / "journal.json");
    auto result = recoverRecording(temp.root, job);
    check(result.asset.id != spec.assetId && result.spec.recoveredFrom == spec.assetId &&
              result.asset.frames == expected,
          "Recovery overwrote identity or wrong extent");
    verifyAudio(temp.root / utf8Path(result.asset.relativePath), expected, 1);
    check(hashMediaFile(before.source) == originalHash &&
              hashMediaFile(job / "journal.json") == originalJournal,
          "Recovery changed original evidence");
}
void invalidRecoveryAndNoOverwrite() {
    Temp temp;
    auto session = makeOneTrackSession("Validation", "Raw");
    auto spec = specFor(session, 256);
    std::filesystem::path job;
    {
        CapturePipe pipe(spec.capture);
        CaptureWriter writer(temp.root, spec, {256, {}});
        job = writer.jobDirectory();
        rejects([&] { CaptureWriter collision(temp.root, spec); });
        Source source(1, 256);
        source.push(pipe, 0, 256);
        writer.drainOne(pipe);
        pipe.finish();
        std::ofstream(job / "take.wav", std::ios::binary) << "existing file";
        const auto existingHash = hashMediaFile(job / "take.wav");
        rejects([&] { writer.finalize(pipe); });
        check(hashMediaFile(job / "take.wav") == existingHash,
              "Finalize overwrote existing destination");
        rejects([&] { inspectRecording(job); });
        std::filesystem::remove(job / "take.wav"); // Test owns this collision fixture.
    }
    auto original = inspectRecording(job);
    std::ifstream stream(job / "journal.json");
    auto j = nlohmann::json::parse(stream);
    auto write = [&](const std::string &bytes) {
        std::ofstream(job / "journal.json", std::ios::binary | std::ios::trunc) << bytes;
    };
    const auto good = j.dump();
    j["schemaMinor"] = 0;
    j.erase("timingOrigin");
    j.erase("endReason");
    write(j.dump());
    check(inspectRecording(job).committedFrames == 256 && !inspectRecording(job).timingOrigin,
          "Legacy 1.0 journal recovery unsupported");
    j = nlohmann::json::parse(good);
    j["timingOrigin"] = {{"backend", 2},
                         {"devicePosition", 10000000000ULL},
                         {"monotonicNs", 20000000000ULL},
                         {"generation", 7},
                         {"clockId", 35},
                         {"cycle", UINT32_MAX},
                         {"rateNumerator", 1},
                         {"rateDenominator", 48000},
                         {"driverDelay", -17}};
    j["endReason"] = static_cast<std::uint32_t>(CaptureEndReason::DeviceLost);
    write(j.dump());
    const auto nativeOrigin = inspectRecording(job);
    check(nativeOrigin.timingOrigin &&
              nativeOrigin.timingOrigin->devicePosition == 10000000000ULL &&
              nativeOrigin.timingOrigin->driverDelay == -17 &&
              nativeOrigin.endReason == CaptureEndReason::DeviceLost,
          "Versioned native clock origin differs");
    const auto recovered = recoverRecording(temp.root, job);
    const auto recoveredInfo =
        inspectRecording(temp.root / utf8Path(recovered.asset.relativePath).parent_path());
    check(recoveredInfo.timingOrigin == nativeOrigin.timingOrigin &&
              recoveredInfo.endReason == CaptureEndReason::RecoveredCheckpoint,
          "Recovered prefix lost its original clock metadata");
    j["timingOrigin"]["rateNumerator"] = 1.5;
    write(j.dump());
    rejects([&] { inspectRecording(job); });
    j = nlohmann::json::parse(good);
    j["committedFrames"] = 1000000;
    write(j.dump());
    rejects([&] { inspectRecording(job); });
    j = nlohmann::json::parse(good);
    j["schemaMajor"] = 2;
    write(j.dump());
    rejects([&] { inspectRecording(job); });
    write("{\"format\":\"x\",\"format\":\"y\"}");
    rejects([&] { inspectRecording(job); });
    write(std::string(16385, ' '));
    rejects([&] { inspectRecording(job); });
    write(good);
    {
        std::fstream audio(original.source, std::ios::binary | std::ios::in | std::ios::out);
        // Known fixture file payload ends at EOF. Corrupt one actual float.
        audio.seekp(-4, std::ios::end);
        const float wrong = .123f;
        audio.write(reinterpret_cast<const char *>(&wrong), sizeof(wrong));
    }
    rejects([&] { inspectRecording(job); });
}
#ifndef _WIN32
void processKill() {
    Temp temp;
    auto spec = specFor(makeOneTrackSession("Kill", "Raw"), 256);
    const auto job = temp.root / "media" / ("capture-" + spec.assetId.str());
    const auto pid = fork();
    check(pid >= 0, "Cannot fork kill fixture");
    if (pid == 0) {
        try {
            CapturePipe pipe(spec.capture);
            CaptureWriter writer(temp.root, spec, {256, {}});
            Source source(1, 256);
            source.push(pipe, 0, 256);
            writer.drainOne(pipe);
            source.push(pipe, 256, 256); // Queued but not durable.
            kill(getpid(), SIGKILL);
            _exit(92);
        } catch (...) {
            _exit(93);
        }
    }
    int status = 0;
    check(waitpid(pid, &status, 0) == pid && WIFSIGNALED(status) && WTERMSIG(status) == SIGKILL,
          "Kill fixture did not terminate at checkpoint");
    const auto r = inspectRecording(job);
    check(r.committedFrames == 256 && !r.finalized, "SIGKILL recovery prefix incorrect");
    const auto recovered = recoverRecording(temp.root, job);
    verifyAudio(temp.root / utf8Path(recovered.asset.relativePath), 256, 1);
}
void writeLimitFailure() {
    Temp temp;
    const auto spec = specFor(makeOneTrackSession("Write limit", "Raw"), 256);
    const auto job = temp.root / "media" / ("capture-" + spec.assetId.str());
    const auto pid = fork();
    check(pid >= 0, "Cannot fork write fault fixture");
    if (pid == 0) {
        try {
            CapturePipe pipe(spec.capture);
            CaptureWriter writer(temp.root, spec, {256, {}});
            Source source(1, 256);
            source.push(pipe, 0, 256);
            writer.drainOne(pipe);
            // Kernel-enforced short write/EFBIG; not a physical ENOSPC test.
            ::signal(SIGXFSZ, SIG_IGN);
            const auto size = std::filesystem::file_size(job / "audio.partial.rf64");
            const rlimit limit{static_cast<rlim_t>(size + 128), static_cast<rlim_t>(size + 128)};
            if (setrlimit(RLIMIT_FSIZE, &limit) != 0)
                _exit(94);
            source.push(pipe, 256, 256);
            bool failed = false;
            try {
                writer.drainOne(pipe);
            } catch (const ProjectError &) {
                failed = true;
            }
            const auto r = source.push(pipe, 512, 256);
            _exit(failed && r.status == CaptureStatus::WriterFailed ? 0 : 95);
        } catch (...) {
            _exit(96);
        }
    }
    int status = 0;
    check(waitpid(pid, &status, 0) == pid && WIFEXITED(status) && WEXITSTATUS(status) == 0,
          "Kernel write-limit failure not detected");
    check(inspectRecording(job).committedFrames == 256,
          "Short write invalidated durable checkpoint");
}
#endif
} // namespace
int main() {
    try {
        rt_audit::reset();
        concurrentTake();
        layoutsAndAlignment();
        writerObservations();
        workerCancellation();
        queuedGap();
        sanitizedJournal();
        for (auto b : {RecordingBoundary::BeforeAudioWrite, RecordingBoundary::AfterAudioFlush,
                       RecordingBoundary::BeforeJournalPublish})
            interruptedBoundary(b, false);
        for (auto b : {RecordingBoundary::BeforeMediaPublish, RecordingBoundary::AfterMediaPublish})
            interruptedBoundary(b, true);
        invalidRecoveryAndNoOverwrite();
#ifndef _WIN32
        processKill();
        writeLimitFailure();
#endif
        const auto a = rt_audit::counts;
        check(!a.cppAllocate && !a.cppFree && !a.cAllocate && !a.cFree && !a.blockingLock,
              "RT producer allocated, freed or locked");
        std::cout << "{\"checks\":" << checks
                  << ",\"synthetic_recorded_frames\":480000,\"rate_hz\":48000,"
                     "\"concurrent_disk_worker\":true,\"rt_allocations\":0,\"rt_frees\":0,\"rt_"
                     "blocking_locks\":0,"
                     "\"raw_overs_preserved\":true,\"prefix_recovery\":true,\"native_audio_"
                     "device\":false}\n";
        return 0;
    } catch (const std::exception &e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
