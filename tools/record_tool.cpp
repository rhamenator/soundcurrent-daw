// SPDX-License-Identifier: GPL-3.0-only
// Developer capture/recovery workflows. Synthetic generates audio, not a device.
#include <soundcurrent/recording.hpp>
#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <iostream>
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <thread>
#endif
using namespace soundcurrent::daw;
namespace {
int run(const std::vector<std::filesystem::path> &args) {
    if (args.size() == 3 && args[1] == "synthetic") {
        // Atomic directory creation ensures an existing project is untouched.
        if (!std::filesystem::create_directory(args[2]))
            throw ProjectError(ErrorCode::Io, "Supply a new project directory");
        auto session = makeOneTrackSession("Synthetic capture", "Raw generated source");
        ProjectStore store(args[2]);
        store.save(session);
        CapturePipe pipe({});
        RecordingSpec spec;
        spec.projectId = session.id;
        spec.trackId = session.tracks.front().id;
        spec.capture = pipe.config();
        RecordingWorker worker(pipe, args[2], spec);
        std::array<float, 512> samples{};
        const std::array<const float *, 1> pointers{samples.data()};
        constexpr Frame frames = 480000;
        bool correct = true;
        for (Frame f = 0; f < frames;) {
            const auto n = static_cast<std::uint32_t>(std::min<Frame>(samples.size(), frames - f));
            for (std::uint32_t i = 0; i < n; ++i)
                samples[i] = float((double((f + i) % 101) - 50) * .04);
            const auto r = pipe.push(pointers, n, f);
            if (r.acceptedFrames != n) {
                correct = false;
                break;
            }
            f += n;
#ifdef _WIN32
            Sleep(1);
#else
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
#endif
        }
        pipe.finish();
        auto result = worker.wait();
        if (!correct)
            throw ProjectError(ErrorCode::Io, "Synthetic capture stopped; checkpoint retained");
        attachRecording(session, result);
        store.save(session);
        std::cout << "Recorded and saved " << result.asset.frames << " raw frames at "
                  << result.asset.sampleRate << " Hz; SHA-256 " << result.asset.sha256 << '\n';
        return 0;
    }
    if (args.size() == 3 && args[1] == "inspect") {
        auto r = inspectRecording(args[2]);
        std::cout << "Project " << r.spec.projectId.str() << "; track " << r.spec.trackId.str()
                  << "; asset " << r.spec.assetId.str()
                  << "\nVerified checkpoint: " << r.committedFrames
                  << " frames; observed file: " << r.observedFrames << " frames; sample SHA-256 "
                  << r.sampleSha256 << "\nFinalized: " << (r.finalized ? "yes" : "no") << '\n';
        std::cout << "Rejected frames: " << r.rejectedFrames
                  << "; observed invalid samples: " << r.observedInvalidInputSamples << '\n';
        return 0;
    }
    if (args.size() == 4 && args[1] == "recover") {
        ProjectStore store(args[2]);
        auto session = store.load();
        auto preview = inspectRecording(args[3]);
        if (preview.spec.projectId != session.id ||
            std::none_of(session.tracks.begin(), session.tracks.end(),
                         [&](const auto &t) { return t.id == preview.spec.trackId; }))
            throw ProjectError(ErrorCode::InvalidState,
                               "Recovery belongs to another project or missing track");
        auto result = recoverRecording(args[2], args[3]);
        attachRecording(session, result);
        store.save(session);
        std::cout << "Recovered " << result.asset.frames << " frames into new asset "
                  << result.asset.id.str() << '\n';
        return 0;
    }
    std::cerr << "Usage: sc-record-tool synthetic NEW_PROJECT_DIRECTORY\n"
                 "       sc-record-tool inspect CAPTURE_JOB_DIRECTORY\n"
                 "       sc-record-tool recover PROJECT_DIRECTORY CAPTURE_JOB_DIRECTORY\n";
    return 2;
}
} // namespace
#ifdef _WIN32
int wmain(int argc, wchar_t **argv) {
#else
int main(int argc, char **argv) {
#endif
    try {
        std::vector<std::filesystem::path> args;
        for (int i = 0; i < argc; ++i) {
#ifdef _WIN32
            args.emplace_back(argv[i]);
#else
            args.emplace_back(utf8Path(argv[i]));
#endif
        }
        return run(args);
    } catch (const std::exception &e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
