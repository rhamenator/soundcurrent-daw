// SPDX-License-Identifier: GPL-3.0-only
#include <iostream>
#include <charconv>
#include <limits>
#include <soundcurrent/project_store.hpp>
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif
using namespace soundcurrent::daw;
namespace {
Frame frame(const std::string &s) {
    Frame value = 0;
    const auto [end, error] = std::from_chars(s.data(), s.data() + s.size(), value);
    if (error != std::errc{} || end != s.data() + s.size() || value < 0)
        throw ProjectError(ErrorCode::InvalidParameter, "Frame must be a nonnegative integer");
    return value;
}
std::optional<Id> anchor(const std::string &s) {
    return s == "-" ? std::nullopt : std::optional<Id>(Id(s));
}
} // namespace
int run(const std::vector<std::string> &args) {
    try {
        if (args.size() < 3) {
            std::cerr << "Usage: sc-project-tool new|inspect PROJECT_DIRECTORY\n"
                         "  add-track DIR NAME mono|stereo|discrete:N [BEFORE_UUID|-]\n"
                         "  rename-track DIR TRACK_UUID NAME\n"
                         "  remove-track DIR TRACK_UUID\n"
                         "  move-track DIR TRACK_UUID BEFORE_UUID|-\n"
                         "  add-clip DIR TRACK_UUID ASSET_UUID START SOURCE LENGTH\n"
                         "  remove-clip DIR TRACK_UUID CLIP_UUID\n"
                         "  trim-clip DIR TRACK_UUID CLIP_UUID START SOURCE LENGTH\n"
                         "  move-clip DIR FROM_TRACK TO_TRACK CLIP_UUID START\n"
                         "  split-clip DIR TRACK_UUID CLIP_UUID TIMELINE_FRAME\n";
            return 2;
        }
        ProjectStore store(utf8Path(args[2]));
        const auto &cmd = args[1];
        if (cmd == "new" && args.size() == 3) {
            if (std::filesystem::exists(store.root() / "project.json"))
                throw ProjectError(ErrorCode::Io, "Project already exists");
            store.save(makeOneTrackSession("Untitled", "Audio 1"));
        } else if (!(cmd == "inspect" && args.size() == 3)) {
            auto s = store.load();
            std::vector<SessionEdit> edits;
            if (cmd == "add-track" && (args.size() == 5 || args.size() == 6)) {
                ChannelLayout layout;
                if (args[4] == "stereo")
                    layout = {LayoutKind::Stereo, 2};
                else if (args[4].starts_with("discrete:")) {
                    const auto channels = frame(args[4].substr(9));
                    if (channels < 1 || channels > 256)
                        throw ProjectError(ErrorCode::InvalidParameter,
                                           "Channel count must be 1 to 256");
                    layout = {LayoutKind::Discrete, static_cast<std::uint32_t>(channels)};
                } else if (args[4] != "mono")
                    throw ProjectError(ErrorCode::InvalidParameter, "Unknown channel layout");
                edits.push_back(InsertTrack{makeAudioTrack(args[3], layout, s.sampleRate),
                                            args.size() == 6 ? anchor(args[5]) : std::nullopt});
            } else if (cmd == "rename-track" && args.size() == 5)
                edits.push_back(RenameTrack{Id(args[3]), args[4]});
            else if (cmd == "remove-track" && args.size() == 4)
                edits.push_back(RemoveTrack{Id(args[3])});
            else if (cmd == "move-track" && args.size() == 5)
                edits.push_back(MoveTrack{Id(args[3]), anchor(args[4])});
            else if (cmd == "add-clip" && args.size() == 8) {
                Clip c;
                c.assetId = Id(args[4]);
                c.startFrame = frame(args[5]);
                c.sourceFrame = frame(args[6]);
                c.lengthFrames = frame(args[7]);
                edits.push_back(InsertClip{Id(args[3]), std::move(c), {}});
            } else if (cmd == "remove-clip" && args.size() == 5)
                edits.push_back(RemoveClip{Id(args[3]), Id(args[4])});
            else if (cmd == "trim-clip" && args.size() == 8)
                edits.push_back(SetClipRange{Id(args[3]), Id(args[4]), frame(args[5]),
                                             frame(args[6]), frame(args[7])});
            else if (cmd == "move-clip" && args.size() == 7)
                edits.push_back(
                    MoveClip{Id(args[3]), Id(args[4]), Id(args[5]), frame(args[6]), {}});
            else if (cmd == "split-clip" && args.size() == 6)
                edits.push_back(
                    SplitClip{Id(args[3]), Id(args[4]), Id::generate(), frame(args[5])});
            else
                throw ProjectError(ErrorCode::InvalidParameter,
                                   "Unknown command or incorrect argument count");
            const auto old = s;
            applySessionEdits(s, edits);
            if (s != old)
                store.save(s);
        }
        std::cout << encodeProject(store.load());
        return 0;
    } catch (const std::exception &e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}

#ifdef _WIN32
int wmain(int argc, wchar_t **argv) {
    std::vector<std::string> args;
    for (int i = 0; i < argc; ++i) {
        const int n = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, argv[i], -1, nullptr, 0,
                                          nullptr, nullptr);
        if (n <= 0)
            return 2;
        std::string s(static_cast<std::size_t>(n), '\0');
        if (!WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, argv[i], -1, s.data(), n, nullptr,
                                 nullptr))
            return 2;
        s.pop_back();
        args.push_back(std::move(s));
    }
    return run(args);
}
#else
int main(int argc, char **argv) {
    return run({argv, argv + argc});
}
#endif
