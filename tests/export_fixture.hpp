// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include <soundcurrent/export.hpp>
#include <soundcurrent/recording.hpp>
#include <QTest>
#include <QThread>
#include <atomic>
#include <array>
#include <chrono>
#include <cmath>
#include <fstream>
#include <source_location>
#include <stdexcept>
namespace export_fixture {
using namespace soundcurrent::daw;
inline int checks = 0;
inline void check(bool okay, const char *message) {
    ++checks;
    if (!okay)
        throw std::runtime_error(message);
}
template <class F>
void await(F predicate, std::source_location where = std::source_location::current()) {
    const auto end = std::chrono::steady_clock::now() + std::chrono::seconds(15);
    while (!predicate()) {
        if (std::chrono::steady_clock::now() >= end)
            throw std::runtime_error("Export fixture timed out at line " +
                                     std::to_string(where.line()));
        QTest::qWait(2);
    }
}
struct Gate {
    std::atomic<bool> entered{false}, released{false};
    void wait() {
        entered = true;
        while (!released.load())
            QThread::msleep(1);
    }
};
struct Release {
    Gate &gate;
    ~Release() {
        gate.released = true;
    }
};
inline std::string bytes(const std::filesystem::path &p) {
    std::ifstream s(p, std::ios::binary);
    return {std::istreambuf_iterator<char>(s), std::istreambuf_iterator<char>()};
}
inline void write(const std::filesystem::path &p, std::string_view value) {
    std::ofstream s(p, std::ios::binary | std::ios::trunc);
    s.write(value.data(), static_cast<std::streamsize>(value.size()));
    check(bool(s), "Cannot write isolated export file");
}
inline bool noTemporary(const std::filesystem::path &p) {
    for (const auto &file : std::filesystem::directory_iterator(p))
        if (file.path().extension() == ".partial")
            return false;
    return true;
}
inline Session project(const std::filesystem::path &root) {
    std::filesystem::create_directory(root);
    auto s = makeOneTrackSession("Séance export Δ", "Piste Ελληνικά");
    s.tracks.front().layout = {LayoutKind::Mono, 1};
    s.tracks.front().eq.bands.front().frequencyHz = 1000;
    RecordingSpec spec;
    spec.projectId = s.id;
    spec.trackId = s.tracks.front().id;
    spec.capture.layout = s.tracks.front().layout;
    spec.capture.slabFrames = 256;
    CapturePipe pipe(spec.capture);
    spec.capture = pipe.config();
    CaptureWriter writer(root, spec);
    std::array<float, 256> signal{};
    std::array<const float *, 1> pointers{signal.data()};
    constexpr Frame count = 96000;
    for (Frame frame = 0; frame < count;) {
        auto n = static_cast<std::uint32_t>(std::min<Frame>(256, count - frame));
        for (std::uint32_t f = 0; f < n; ++f)
            signal[f] = static_cast<float>(1.4 * std::sin(double(frame + f) * .1308996939));
        check(pipe.push(pointers, n, frame).acceptedFrames == n, "Fixture raw capture failed");
        while (writer.drainOne(pipe)) {
        }
        frame += n;
    }
    pipe.finish();
    while (writer.drainOne(pipe)) {
    }
    attachRecording(s, writer.finalize(pipe));
    s.exportStartFrame = 113;
    s.exportEndFrame = 12345;
    ProjectStore(root).save(s);
    return s;
}
} // namespace export_fixture
