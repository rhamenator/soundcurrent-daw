// SPDX-License-Identifier: GPL-3.0-only
// Worker-side CLI; no audio devices or project mutation. Windows paths use wide argv.
#include <soundcurrent/export.hpp>
#include <algorithm>
#include <charconv>
#include <csignal>
#include <iostream>
#include <iomanip>
using namespace soundcurrent::daw;
namespace {
volatile std::sig_atomic_t interrupted = 0;
void interrupt(int) {
    interrupted = 1;
}
Frame number(const std::string &text) {
    Frame value = 0;
    const auto result = std::from_chars(text.data(), text.data() + text.size(), value);
    if (result.ec != std::errc{} || result.ptr != text.data() + text.size() || value < 0)
        throw ProjectError(ErrorCode::InvalidState, "Expected a nonnegative frame number");
    return value;
}
std::string arg(const std::filesystem::path &p) {
    const auto bytes = p.u8string();
    return {reinterpret_cast<const char *>(bytes.data()), bytes.size()};
}
} // namespace
#ifdef _WIN32
int wmain(int argc, wchar_t **argv) {
#else
int main(int argc, char **argv) {
#endif
    try {
        if (argc == 3 && arg(std::filesystem::path(argv[1])) == "fingerprint") {
            std::cout << hashMediaFile(std::filesystem::path(argv[2])) << '\n';
            return 0;
        }
        if (argc < 4) {
            std::cerr
                << "Usage: sc-export-tool render PROJECT OUTPUT.wav [--start FRAME --end FRAME] "
                   "[--tail] [--rf64] [--replace-sha256 CONFIRMED_HASH]\n"
                   "       sc-export-tool render-mix PROJECT OUTPUT.wav [same options; matching "
                   "track layouts]\n"
                   "       sc-export-tool fingerprint FILE.wav\n";
            return 2;
        }
        const auto command = arg(std::filesystem::path(argv[1]));
        const bool mixMode = command == "render-mix";
        if (command != "render" && !mixMode)
            return 2;
        const auto root = std::filesystem::path(argv[2]);
        const auto session = ProjectStore(root).load();
        if (session.tracks.empty())
            throw ProjectError(ErrorCode::InvalidState, "Project has no track to export");
        ExportSpec spec{session.tracks.front().id};
        spec.startFrame = session.exportStartFrame;
        spec.endFrame = session.exportEndFrame;
        if (spec.endFrame <= spec.startFrame) {
            spec.startFrame = 0;
            for (std::size_t t = 0; t < (mixMode ? session.tracks.size() : 1); ++t) {
                if (mixMode && session.master &&
                    std::none_of(
                        session.master->plan.tracks.begin(), session.master->plan.tracks.end(),
                        [&](const auto &lane) { return lane.track == session.tracks[t].id; }))
                    continue;
                for (const auto &clip : session.tracks[t].clips)
                    spec.endFrame = std::max(spec.endFrame, clip.startFrame + clip.lengthFrames);
            }
        }
        spec.maximumTailFrames = Frame(session.sampleRate) * 10;
        spec.silentWindowFrames = (session.sampleRate + 9) / 10;
        spec.maximumProcessFrames = Frame(session.sampleRate) * 60 * 60 * 24;
        for (int i = 4; i < argc; ++i) {
            const auto option = arg(std::filesystem::path(argv[i]));
            if (option == "--tail")
                spec.tail = ExportTail::UntilSilent;
            else if (option == "--rf64")
                spec.forceRf64 = true;
            else if (option == "--start" || option == "--end" || option == "--replace-sha256") {
                if (++i == argc)
                    throw ProjectError(ErrorCode::InvalidState, "Export option needs a value");
                const auto value = arg(std::filesystem::path(argv[i]));
                if (option == "--start")
                    spec.startFrame = number(value);
                else if (option == "--end")
                    spec.endFrame = number(value);
                else
                    spec.replaceSha256 = value;
            } else
                throw ProjectError(ErrorCode::InvalidState, "Unknown export option");
        }
        std::signal(SIGINT, interrupt);
        std::signal(SIGTERM, interrupt);
        ExportOptions options;
        options.canceled = [] { return interrupted != 0; };
        ExportResult r;
        if (mixMode) {
            std::vector<Id> ids;
            for (const auto &t : session.tracks)
                ids.push_back(t.id);
            MixExportSpec mixed(session.master
                                    ? session.master->plan
                                    : identityMix(session, ids, session.tracks.front().layout));
            static_cast<ExportSettings &>(mixed) = spec;
            r = exportMixWav(root, session, std::filesystem::path(argv[3]), mixed, options);
        } else
            r = exportTrackWav(root, session, std::filesystem::path(argv[3]), spec, options);
        std::cout << std::setprecision(17) << "{\"frames\":" << r.frames
                  << ",\"tail_frames\":" << r.tailFrames << ",\"rate\":" << r.sampleRate
                  << ",\"channels\":" << r.layout.channels << ",\"peak\":" << r.peak
                  << ",\"over_full_scale_samples\":" << r.overFullScaleSamples
                  << ",\"tail_truncated\":" << (r.tailTruncated ? "true" : "false")
                  << ",\"rf64\":" << (r.rf64 ? "true" : "false")
                  << ",\"replaced\":" << (r.replaced ? "true" : "false") << ",\"sample_sha256\":\""
                  << r.sampleSha256 << "\",\"file_sha256\":\"" << r.fileSha256
                  << "\",\"directory_flushed\":"
                  << (r.durability == Durability::FileAndDirectoryFlushed ? "true" : "false")
                  << ",\"publication_warning\":"
                  << (r.publicationWarning.empty() ? "false" : "true")
                  << ",\"audio_device\":false}\n";
        if (!r.publicationWarning.empty())
            std::cerr << r.publicationWarning << '\n';
        return 0;
    } catch (const ProjectError &error) {
        std::cerr << error.what() << '\n';
        return error.code() == ErrorCode::Canceled ? 3 : 1;
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
