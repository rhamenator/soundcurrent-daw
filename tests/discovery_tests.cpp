// SPDX-License-Identifier: GPL-3.0-only
#include <soundcurrent/recording.hpp>
#include <array>
#include <algorithm>
#include <fstream>
#include <iostream>
#ifndef _WIN32
#include <signal.h>
#include <sys/wait.h>
#include <unistd.h>
#endif
using namespace soundcurrent::daw;
namespace {
unsigned checks = 0;
void check(bool ok, const char *why) {
    ++checks;
    if (!ok)
        throw std::runtime_error(why);
}
template <class F> void rejects(F f) {
    try {
        f();
    } catch (const ProjectError &) {
        ++checks;
        return;
    }
    throw std::runtime_error("Expected discovery rejection");
}
struct Temp {
    std::filesystem::path root = std::filesystem::temp_directory_path() / Id::generate().str();
    Temp() {
        std::filesystem::create_directory(root);
    }
    ~Temp() {
        std::error_code e;
        std::filesystem::remove_all(root, e);
    }
};
RecordingSpec specFor(const Session &s) {
    RecordingSpec spec;
    spec.projectId = s.id;
    spec.trackId = s.tracks.front().id;
    spec.capture.slabFrames = 256;
    return spec;
}
void write(CaptureWriter &writer, CapturePipe &pipe, float sample = 1) {
    std::array<float, 512> data{};
    data.fill(sample);
    const std::array<const float *, 1> inputs{data.data()};
    check(pipe.push(inputs, 512, 0).acceptedFrames == 512, "Fixture capture failed");
    while (writer.drainOne(pipe)) {
    };
}
const RecordingJobEntry &find(const RecordingDiscovery &d, const std::filesystem::path &job) {
    for (const auto &e : d.entries)
        if (e.job == job)
            return e;
    throw std::runtime_error("Missing discovered job");
}
void inventory() {
    Temp t;
    auto s = makeOneTrackSession("Découverte — Ελλάδα", "Mic");
    ProjectStore(t.root).save(s);
    check(discoverRecordings(t.root, s).entries.empty() &&
              !std::filesystem::exists(t.root / "media"),
          "Empty discovery wrote media");
    auto spec = specFor(s);
    CapturePipe pipe(spec.capture);
    spec.capture = pipe.config();
    const auto job = t.root / "media" / ("capture-" + spec.assetId.str());
    {
        CaptureWriter writer(t.root, spec, {128, {}});
        write(writer, pipe);
        auto active = discoverRecordings(t.root, s);
        check(find(active, job).status == RecordingJobStatus::Active &&
                  !find(active, job).checkpoint,
              "Active writer was offered for recovery");
        rejects([&] { inspectRecording(job, {}, true); });
        rejects([&] { recoverRecording(t.root, job); });
    }
    const auto before = inspectRecording(job, {}, true);
    check(before.writerActivityConfirmed && before.committedFrames == 512 && !before.finalized,
          "Inactive checkpoint not verified");
    const auto journalHash = hashMediaFile(job / "journal.json"),
               audioHash = hashMediaFile(before.source);
    auto d = discoverRecordings(t.root, s);
    check(find(d, job).status == RecordingJobStatus::NeedsVerification,
          "Interrupted job not discovered");
    unsigned boundaries = 0;
    rejects([&] {
        inspectRecording(
            job,
            [&] {
                if (++boundaries == 2)
                    throw ProjectError(ErrorCode::Canceled, "Owned verification cancellation");
            },
            true);
    });
    check(boundaries == 2 && hashMediaFile(before.source) == audioHash,
          "Verification cancellation changed source");
    auto recovered = recoverRecording(t.root, job);
    attachRecording(s, recovered);
    ProjectStore(t.root).save(s);
    d = discoverRecordings(t.root, s);
    check(d.attached == 1 && d.entries.size() == 1 &&
              find(d, job).status == RecordingJobStatus::RecoveredSource,
          "Attached recovered copy was reoffered or original disappeared");
    check(hashMediaFile(job / "journal.json") == journalHash &&
              hashMediaFile(before.source) == audioHash,
          "Discovery/recovery changed original");
    auto foreignSpec = specFor(makeOneTrackSession("Foreign", "Mic"));
    CapturePipe foreignPipe(foreignSpec.capture);
    foreignSpec.capture = foreignPipe.config();
    std::filesystem::path foreign;
    {
        CaptureWriter writer(t.root, foreignSpec, {128, {}});
        foreign = writer.jobDirectory();
        write(writer, foreignPipe);
    }
    auto emptySpec = specFor(s);
    CapturePipe emptyPipe(emptySpec.capture);
    emptySpec.capture = emptyPipe.config();
    std::filesystem::path empty;
    {
        CaptureWriter writer(t.root, emptySpec);
        empty = writer.jobDirectory();
    }
    const auto invalid = t.root / "media" / ("capture-" + Id::generate().str());
    std::filesystem::create_directory(invalid);
    std::ofstream(invalid / "journal.json") << "bad";
    d = discoverRecordings(t.root, s);
    check(find(d, foreign).status == RecordingJobStatus::Foreign &&
              find(d, empty).status == RecordingJobStatus::Empty &&
              find(d, invalid).status == RecordingJobStatus::Invalid,
          "Foreign/empty/corrupt job classifications lost");
    std::filesystem::remove(job / "writer.lock");
    auto legacy = s;
    legacy.assets.clear();
    legacy.tracks.front().clips.clear();
    d = discoverRecordings(t.root, legacy);
    check(find(d, job).status == RecordingJobStatus::LegacyNeedsVerification &&
              !inspectRecording(job, {}, true).writerActivityConfirmed,
          "Legacy job activity falsely confirmed");
#ifndef _WIN32
    const auto linked = t.root / "media" / ("capture-" + Id::generate().str());
    std::filesystem::create_directory_symlink(job, linked);
    d = discoverRecordings(t.root, legacy);
    check(find(d, linked).status == RecordingJobStatus::Invalid, "Linked job traversed");
    std::filesystem::create_symlink(invalid / "journal.json", job / "writer.lock");
    d = discoverRecordings(t.root, legacy);
    check(find(d, job).status == RecordingJobStatus::Invalid, "Linked lease accepted");
    std::filesystem::remove(job / "writer.lock");
#endif
    RecordingDiscoveryOptions options;
    options.maximumJobs = 2;
    d = discoverRecordings(t.root, legacy, options);
    check(d.truncated && d.entries.size() <= 2, "Job inventory unbounded");
    options.maximumDirectoryEntries = 1;
    d = discoverRecordings(t.root, legacy, options);
    check(d.truncated && d.directoryEntries == 1, "Directory budget unbounded");
    options = {};
    options.maximumJobs = 0;
    rejects([&] { discoverRecordings(t.root, s, options); });
    options = {};
    boundaries = 0;
    options.boundary = [&] {
        if (++boundaries == 3)
            throw ProjectError(ErrorCode::Canceled, "Owned discovery cancellation");
    };
    rejects([&] { discoverRecordings(t.root, s, options); });
    check(boundaries == 3, "Discovery cancellation boundary missed");
    // Recovery of any matching track is discoverable, independent of track order.
    auto other = makeOneTrackSession("Other", "Other").tracks.front();
    s.tracks.push_back(other);
    std::reverse(s.tracks.begin(), s.tracks.end());
    check(find(discoverRecordings(t.root, s), job).status == RecordingJobStatus::RecoveredSource,
          "Stable track ID discovery lost through reorder");
    // An attached recovery of an unattached recovery copy still accounts for its
    // original source; matching content and timing are required at every link.
    auto chain = s;
    chain.assets.clear();
    for (auto &track : chain.tracks)
        track.clips.clear();
    const auto copyJob = t.root / utf8Path(recovered.asset.relativePath).parent_path();
    const auto secondCopy = recoverRecording(t.root, copyJob);
    attachRecording(chain, secondCopy);
    d = discoverRecordings(t.root, chain);
    check(find(d, copyJob).status == RecordingJobStatus::RecoveredSource &&
              find(d, job).status == RecordingJobStatus::RecoveredSource,
          "Transitive recovered source reoffered");
    auto mismatch = specFor(legacy);
    mismatch.recoveredFrom = spec.assetId;
    CapturePipe mismatchPipe(mismatch.capture);
    mismatch.capture = mismatchPipe.config();
    RecordingResult wrong;
    {
        CaptureWriter writer(t.root, mismatch, {128, {}});
        write(writer, mismatchPipe, 2);
        mismatchPipe.finish();
        wrong = writer.finalize(mismatchPipe);
    }
    auto unlinked = legacy;
    attachRecording(unlinked, wrong);
    check(find(discoverRecordings(t.root, unlinked), job).status ==
              RecordingJobStatus::LegacyNeedsVerification,
          "Mismatching attached samples suppressed original");
    std::filesystem::resize_file(before.source, 64);
    check(find(discoverRecordings(t.root, legacy), job).status ==
              RecordingJobStatus::LegacyNeedsVerification,
          "Metadata scan unexpectedly claimed audio verification");
    rejects([&] { inspectRecording(job, {}, true); });
    rejects([&] { recoverRecording(t.root, job); });
}
void copyCancellation() {
    for (const bool afterPublication : {false, true}) {
        Temp t;
        const auto s = makeOneTrackSession("Copy cancellation", "Mic");
        ProjectStore(t.root).save(s);
        auto spec = specFor(s);
        CapturePipe pipe(spec.capture);
        spec.capture = pipe.config();
        std::filesystem::path original;
        {
            CaptureWriter writer(t.root, spec, {128, {}});
            original = writer.jobDirectory();
            write(writer, pipe);
        }
        const auto source = inspectRecording(original);
        const auto sourceHash = hashMediaFile(source.source),
                   journalHash = hashMediaFile(original / "journal.json");
        bool reached = false;
        try {
            recoverRecording(t.root, original, [&] {
                for (const auto &item : std::filesystem::directory_iterator(t.root / "media")) {
                    if (item.path() == original)
                        continue;
                    std::optional<RecordingRecovery> progress;
                    try {
                        progress = inspectRecording(item.path());
                    } catch (const ProjectError &) {
                        continue;
                    }
                    if (progress->committedFrames == 512 &&
                        progress->finalized == afterPublication) {
                        reached = true;
                        throw ProjectError(ErrorCode::Canceled, "Owned copy cancellation");
                    }
                }
            });
            throw std::runtime_error("Recovery copy was not canceled");
        } catch (const ProjectError &e) {
            check(e.code() == ErrorCode::Canceled && reached, "Copy canceled at wrong boundary");
        }
        check(hashMediaFile(original / "journal.json") == journalHash &&
                  hashMediaFile(source.source) == sourceHash && ProjectStore(t.root).load() == s,
              "Canceled copy changed source/project");
        auto d = discoverRecordings(t.root, s);
        check(d.entries.size() == 2, "Canceled copy job lost from inventory");
        const auto copy = std::find_if(d.entries.begin(), d.entries.end(),
                                       [&](const auto &e) { return e.job != original; });
        check(copy != d.entries.end() && copy->status == RecordingJobStatus::NeedsVerification &&
                  copy->checkpoint->finalized == afterPublication &&
                  inspectRecording(copy->job, {}, true).committedFrames == 512,
              "Canceled copy checkpoint not recoverable/inactive");
    }
}

#ifndef _WIN32
void killedWriter() {
    Temp t;
    const auto s = makeOneTrackSession("Crash", "Mic");
    ProjectStore(t.root).save(s);
    auto spec = specFor(s);
    const auto job = t.root / "media" / ("capture-" + spec.assetId.str());
    int notify[2];
    check(::pipe(notify) == 0, "Cannot create owned crash notification pipe");
    const auto child = fork();
    check(child >= 0, "Cannot fork owned writer");
    if (child == 0) {
        ::close(notify[0]);
        try {
            CapturePipe pipe(spec.capture);
            spec.capture = pipe.config();
            CaptureWriter writer(t.root, spec, {128, {}});
            write(writer, pipe);
            char ready = 1;
            check(::write(notify[1], &ready, 1) == 1, "Cannot notify parent of durable crash prefix");
            for (;;)
                pause();
        } catch (...) {
            _exit(2);
        }
    }
    ::close(notify[1]);
    struct Child {
        pid_t pid;
        ~Child() {
            if (pid > 0) {
                kill(pid, SIGKILL);
                waitpid(pid, nullptr, 0);
            }
        }
    } cleanup{child};
    char ready = 0;
    const auto n = ::read(notify[0], &ready, 1);
    ::close(notify[0]);
    check(n == 1 && ready, "Owned child writer failed");
    check(find(discoverRecordings(t.root, s), job).status == RecordingJobStatus::Active,
          "Cross-process writer lock missed");
    check(kill(child, SIGKILL) == 0, "Cannot kill owned child writer");
    int status = 0;
    check(waitpid(child, &status, 0) == child && WIFSIGNALED(status),
          "Owned writer did not terminate");
    cleanup.pid = 0;
    check(find(discoverRecordings(t.root, s), job).status ==
                  RecordingJobStatus::NeedsVerification &&
              inspectRecording(job, {}, true).committedFrames == 512,
          "Killed writer did not release OS lease/preserve prefix");
    const auto restored = recoverRecording(t.root, job);
    check(restored.asset.frames == 512 && restored.spec.recoveredFrom == spec.assetId,
          "Killed writer recovery extent lost");
}
#endif
} // namespace
int main() {
    try {
        inventory();
        copyCancellation();
#ifndef _WIN32
        killedWriter();
#endif
        std::cout << checks << " discovery checks passed\n";
        return 0;
    } catch (const std::exception &e) {
        std::cerr << "FAIL: " << e.what() << '\n';
        return 1;
    }
}
