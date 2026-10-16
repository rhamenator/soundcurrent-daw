// SPDX-License-Identifier: GPL-3.0-only
#include <soundcurrent/waveform.hpp>
#include <algorithm>
#include <bit>
#include <cmath>
#include <fstream>
#include <iostream>
#include <limits>
#include <vector>
using namespace soundcurrent::daw;
namespace {
unsigned checks = 0;
void check(bool value, const char *message) {
    ++checks;
    if (!value) throw std::runtime_error(message);
}
template<class F> void refused(F f, ErrorCode code) {
    bool caught = false;
    try { f(); } catch (const ProjectError &e) { caught = true; check(e.code() == code, "Wrong refusal code"); }
    check(caught, "Expected refusal missing");
}
void put(std::string &s, std::uint64_t v, unsigned bytes) {
    for (unsigned i = 0; i < bytes; ++i) s.push_back(char((v >> (i * 8)) & 255));
}
void writeWave(const std::filesystem::path &path, unsigned channels, const std::vector<double> &pcm) {
    std::string data;
    for (double v : pcm) put(data, std::bit_cast<std::uint64_t>(v), 8);
    std::string s = "RIFF"; put(s, 36 + data.size(), 4); s += "WAVEfmt "; put(s, 16, 4);
    put(s, 3, 2); put(s, channels, 2); put(s, 48000, 4); put(s, 48000 * channels * 8, 4);
    put(s, channels * 8, 2); put(s, 64, 2); s += "data"; put(s, data.size(), 4); s += data;
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    out.write(s.data(), std::streamsize(s.size())); out.close(); check(bool(out), "Owned WAV write failed");
}
WaveformIdentity identity(ApprovedMediaFile &file, std::uint64_t frames, unsigned channels) {
    return {file.digest(), frames, 48000, channels};
}
void sameUsage(const ResourceLedger &ledger, const ResourceUsage &prior) {
    const auto now = ledger.usage();
    check(now.reservedBytes == prior.reservedBytes && now.owners == prior.owners, "Waveform credit leaked");
}
void oracle(const Waveform &w, const std::vector<double> &pcm, std::uint64_t base,
            std::uint64_t offset = 0, std::uint64_t count = UINT64_MAX) {
    const auto frames = count == UINT64_MAX ? pcm.size() / w.source().channels : count;
    check(w.window() == WaveformWindow{offset, frames}, "Prepared source window differs");
    auto expectedWidth = base;
    for (std::size_t level = 0; level < w.levels().size(); ++level) {
        const auto &l = w.levels()[level];
        check(l.framesPerBin == expectedWidth, "Pyramid source coordinates differ");
        check(l.bins == frames / expectedWidth + (frames % expectedWidth != 0), "Pyramid bin count differs");
        for (std::uint64_t b = 0; b < l.bins; ++b) {
            const auto first = offset + b * expectedWidth, end = offset + std::min<std::uint64_t>(frames, b * expectedWidth + expectedWidth);
            check(w.sourceRange(level, b) == std::pair(first, end), "Tail/source interval changed");
            for (unsigned c = 0; c < w.source().channels; ++c) {
                double lo = std::numeric_limits<double>::infinity(), hi = -lo;
                for (auto f = first; f < end; ++f) {
                    lo = std::min(lo, pcm[std::size_t(f) * w.source().channels + c]);
                    hi = std::max(hi, pcm[std::size_t(f) * w.source().channels + c]);
                }
                check(w.peak(level, b, c) == WaveformPeak{lo, hi}, "Independent source extrema differ");
            }
        }
        expectedWidth = std::min<std::uint64_t>(frames, expectedWidth * 2);
    }
    check((frames == 0) == w.levels().empty(), "Empty waveform level count differs");
    if (frames) check(w.levels().back().bins == 1, "Root level is incomplete");
}
}
int main() {
    const auto rootPath = std::filesystem::temp_directory_path() / ("sc-waveform-" + Id::generate().str());
    try {
        std::filesystem::create_directory(rootPath);
        ResourceLedger memory(16 * 1024 * 1024, "Waveform acceptance");
        for (unsigned channels : {1U, 2U, 8U, 257U, 1024U}) {
            std::vector<double> pcm;
            for (unsigned f = 0; f < 19; ++f) for (unsigned c = 0; c < channels; ++c) {
                double v = (c % 2 ? -1. : 1.) * (1.25 + double((f * 13 + c * 7) % 31) / 8.);
                if (f == 7) v = (c % 2 ? 1. : -1.) * 19.25; // Isolated bipolar impulse and headroom.
                if (f == 18) v = std::nextafter(.25, 1.); // Preserve beyond-float32 precision in the tail.
                pcm.push_back(v);
            }
            writeWave(rootPath / "source.wav", channels, pcm);
            ApprovedMediaRoot root(rootPath, memory); auto file = root.open("source.wav", 1024 * 1024);
            const auto expected = identity(file, 19, channels); const auto prior = memory.usage();
            for (std::uint64_t width : {1ULL, 3ULL, 7ULL, 64ULL}) {
                {
                    WaveformLimits limits; limits.baseFramesPerBin = width; limits.validation.blockFrames = 3;
                    auto w = buildApprovedWaveform(file, expected, limits);
                    check(w.source() == expected, "Verified identity not retained"); oracle(w, pcm, width);
                    check(memory.usage().reservedBytes == prior.reservedBytes + w.chargedBytes(), "Retained peak credit differs");
                    refused([&]{w.peak(w.levels().size(), 0, 0);}, ErrorCode::InvalidParameter);
                    refused([&]{w.peak(0, w.levels()[0].bins, 0);}, ErrorCode::InvalidParameter);
                    refused([&]{w.peak(0, 0, channels);}, ErrorCode::InvalidParameter);
                    auto moved = std::move(w); check(w.levels().empty() && w.chargedBytes() == 0, "Moved result remains accessible");
                    refused([&]{w.peak(0, 0, 0);}, ErrorCode::InvalidParameter); oracle(moved, pcm, width);
                }
                sameUsage(memory, prior);
            }
            for (auto window : {WaveformWindow{5, 9}, WaveformWindow{7, 1}, WaveformWindow{18, 1}, WaveformWindow{19, 0}, WaveformWindow{0, 0}}) {
                for (std::uint64_t width : {1ULL, 3ULL, 64ULL}) {
                    { WaveformLimits limits; limits.window = window; limits.baseFramesPerBin = width; limits.validation.blockFrames = 3;
                      auto w = buildApprovedWaveform(file, expected, limits);
                      oracle(w, pcm, width, window.first, window.frames); }
                    sameUsage(memory, prior);
                }
            }
            for (auto window : {WaveformWindow{20, 0}, WaveformWindow{18, 2}, WaveformWindow{UINT64_MAX, 2}, WaveformWindow{1, UINT64_MAX}}) {
                WaveformLimits limits; limits.window = window;
                refused([&]{buildApprovedWaveform(file, expected, limits);}, ErrorCode::InvalidParameter); sameUsage(memory, prior);
            }
            auto wrong = expected; wrong.sha256[0] = wrong.sha256[0] == '0' ? '1' : '0';
            refused([&]{buildApprovedWaveform(file, wrong);}, ErrorCode::MediaMismatch); sameUsage(memory, prior);
            for (unsigned field = 0; field < 3; ++field) {
                wrong = expected;
                if (field == 0) ++wrong.frames; else if (field == 1) ++wrong.rate; else ++wrong.channels;
                refused([&]{buildApprovedWaveform(file, wrong);}, ErrorCode::MediaMismatch); sameUsage(memory, prior);
            }
            WaveformLimits limits; limits.baseFramesPerBin = 1; limits.maximumBaseBins = 18;
            refused([&]{buildApprovedWaveform(file, expected, limits);}, ErrorCode::ResourceLimit); sameUsage(memory, prior);
            limits = {}; limits.maximumPayloadBytes = sizeof(Waveform);
            refused([&]{buildApprovedWaveform(file, expected, limits);}, ErrorCode::ResourceLimit); sameUsage(memory, prior);
            limits = {}; limits.baseFramesPerBin = 0;
            refused([&]{buildApprovedWaveform(file, expected, limits);}, ErrorCode::InvalidParameter); sameUsage(memory, prior);
            memory.configure(prior.reservedBytes);
            refused([&]{buildApprovedWaveform(file, expected);}, ErrorCode::ResourceLimit); sameUsage(memory, prior);
            memory.configure(16 * 1024 * 1024);
            std::stop_source stop; stop.request_stop();
            refused([&]{buildApprovedWaveform(file, expected, {}, stop.get_token());}, ErrorCode::Canceled); sameUsage(memory, prior);
        }
        writeWave(rootPath / "empty.wav", 1, {});
        {
            ApprovedMediaRoot root(rootPath, memory); auto file = root.open("empty.wav", 1024);
            const auto expected = identity(file, 0, 1); const auto prior = memory.usage();
            {auto w = buildApprovedWaveform(file, expected); oracle(w, {}, 256);
             refused([&]{w.sourceRange(0, 0);}, ErrorCode::InvalidParameter);}
            sameUsage(memory, prior);
        }
        check(memory.usage().owners == 0, "Peak/file owners remain");
        // Long streamed source: cancellation after successful read callbacks, and
        // near-complete cancellation, must never expose a partially valid pyramid.
        std::vector<double> pcm(4099, .75); pcm.back() = -8.5;
        writeWave(rootPath / "stream.wav", 1, pcm);
        {
            ApprovedMediaRoot root(rootPath, memory); auto file = root.open("stream.wav", 1024 * 1024);
            const auto expected = identity(file, pcm.size(), 1); const auto prior = memory.usage();
            WaveformLimits limits; limits.validation.blockFrames = 3; limits.baseFramesPerBin = 3;
            unsigned calls = 0;
            {auto w = buildApprovedWaveform(file, expected, limits, {}, [&]{++calls;}); oracle(w, pcm, 3);}
            sameUsage(memory, prior); check(calls > 50, "No streamed read boundaries observed");
            limits.window = {1, 5}; // Cancellation after the retained window still cancels full-source validation.
            for (const auto at : {20U, calls}) {
                unsigned count = 0; std::stop_source stop;
                refused([&]{buildApprovedWaveform(file, expected, limits, stop.get_token(), [&]{if (++count == at) stop.request_stop();});}, ErrorCode::Canceled);
                sameUsage(memory, prior); check(count >= at, "Cancellation phase was not reached");
            }
            unsigned count = 0; bool threw = false;
            try {buildApprovedWaveform(file, expected, limits, {}, [&]{if (++count == 20) throw std::logic_error("Owned callback failure");});}
            catch (const std::logic_error &) {threw = true;}
            check(threw, "Worker callback error lost"); sameUsage(memory, prior);
#ifndef _WIN32
            count = 0;
            refused([&]{buildApprovedWaveform(file, expected, limits, {}, [&]{
                if (++count == calls) {std::ofstream out(rootPath / "stream.wav", std::ios::binary | std::ios::app); out.put('x');}
            });}, ErrorCode::MediaMismatch); sameUsage(memory, prior);
#endif
        }
        for (double invalid : {std::numeric_limits<double>::infinity(), std::numeric_limits<double>::quiet_NaN()}) {
            pcm.back() = invalid; writeWave(rootPath / "nonfinite.wav", 1, pcm);
            ApprovedMediaRoot root(rootPath, memory); auto file = root.open("nonfinite.wav", 1024 * 1024);
            const auto expected = identity(file, pcm.size(), 1); const auto prior = memory.usage();
            refused([&]{buildApprovedWaveform(file, expected);}, ErrorCode::InvalidParameter); sameUsage(memory, prior);
            WaveformLimits window; window.window = {1, 5};
            refused([&]{buildApprovedWaveform(file, expected, window);}, ErrorCode::InvalidParameter); sameUsage(memory, prior);
        }
        check(memory.usage().owners == 0 && memory.usage().reservedBytes == 0, "Final resource credit leaked");
        std::filesystem::remove_all(rootPath);
        std::cout << "Waveform source extrema, identity, multichannel, headroom, cancellation and resource checks: " << checks << "\n";
    } catch (const std::exception &e) {
        std::cerr << e.what() << "\nOwned fixture retained: " << rootPath << "\n"; return 1;
    }
}
