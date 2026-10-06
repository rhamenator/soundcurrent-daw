// SPDX-License-Identifier: GPL-3.0-only
// Headless developer playback check; no device, project write or export.
#include <soundcurrent/playback_reader.hpp>
#include <algorithm>
#include <array>
#include <chrono>
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
#ifdef _WIN32
int wmain(int argc, wchar_t **argv) {
#else
int main(int argc, char **argv) {
#endif
    try {
        if (argc != 3) {
            std::cerr << "Usage: sc-play-tool verify PROJECT_DIRECTORY\n";
            return 2;
        }
#ifdef _WIN32
        const auto command = std::filesystem::path(argv[1]);
        const auto root = std::filesystem::path(argv[2]);
#else
        const auto command = utf8Path(argv[1]);
        const auto root = utf8Path(argv[2]);
#endif
        if (command != "verify")
            return 2;
        const auto session = ProjectStore(root).load();
        if (session.tracks.empty())
            throw ProjectError(ErrorCode::InvalidState, "Project has no track to play");
        PlaybackConfig config;
        config.sampleRate = session.sampleRate;
        config.layout = session.tracks.front().layout;
        config.startFrame = session.exportStartFrame;
        config.endFrame = session.exportEndFrame;
        if (config.endFrame <= config.startFrame) {
            config.startFrame = session.playheadFrame;
            config.endFrame = 0;
            for (const auto &clip : session.tracks.front().clips)
                config.endFrame = std::max(config.endFrame, clip.startFrame + clip.lengthFrames);
        }
        if (config.endFrame <= config.startFrame)
            throw ProjectError(ErrorCode::InvalidState,
                               "First track has no media beyond the playhead");
        PlaybackRun run(root, session, session.tracks.front().id, config);
        std::vector<float> samples(std::size_t(config.layout.channels) * 512);
        std::array<float *, 256> pointers{};
        for (std::uint32_t c = 0; c < config.layout.channels; ++c)
            pointers[c] = samples.data() + std::size_t(c) * 512;
        double peak = 0;
        Frame frames = 0;
        while (run.position() < config.endFrame) {
            const auto n =
                static_cast<std::uint32_t>(std::min<Frame>(512, config.endFrame - run.position()));
            const auto r = run.process({pointers.data(), config.layout.channels}, n);
            if (r.status != PlaybackStatus::Running && r.status != PlaybackStatus::Underflow &&
                r.status != PlaybackStatus::Complete) {
                run.cancelReader();
                run.waitReader();
                throw ProjectError(ErrorCode::Io, "Playback stopped before range completion");
            }
            frames += r.timelineFrames;
            peak = std::max(peak, r.peak);
#ifdef _WIN32
            Sleep(1);
#else
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
#endif
        }
        run.waitReader();
        std::cout << "{\"played_frames\":" << frames
                  << ",\"missing_frames\":" << run.missingFrames() << ",\"peak\":" << peak
                  << ",\"audio_device\":false}\n";
        return run.missingFrames() ? 1 : 0;
    } catch (const std::exception &e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
