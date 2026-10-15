// SPDX-License-Identifier: GPL-3.0-only
#include <soundcurrent/waveform_protocol.hpp>
#include <algorithm>
#include <bit>
#include <cmath>
#include <iostream>
#include <limits>
#include <vector>
using namespace soundcurrent::daw;
namespace {
unsigned checks = 0;
void check(bool v, const char *message) { ++checks; if (!v) throw std::runtime_error(message); }
template<class F> void refuse(F f, ErrorCode code = ErrorCode::InvalidState) {
    bool caught = false;
    try { f(); } catch (const ProjectError &e) { caught = true; check(e.code() == code, "Wrong refusal"); }
    check(caught, "Missing refusal");
}
void put(std::vector<char> &v, std::uint64_t n, unsigned bytes) {
    for (unsigned b = 0; b < bytes; ++b) v.push_back(char((n >> (b * 8)) & 255));
}
void text(std::vector<char> &v, std::string_view s) { v.insert(v.end(), s.begin(), s.end()); }
void replace(std::vector<char> &v, std::size_t offset, double d) {
    const auto n = std::bit_cast<std::uint64_t>(d);
    for (unsigned b = 0; b < 8; ++b) v.at(offset + b) = char((n >> (b * 8)) & 255);
}
// Independent wire fixture: explicit field order, direct scans of original PCM
// for EACH level. Neither production encoder nor pairwise reducer is used.
std::vector<char> fixture(const WaveformTicket &t, const std::vector<double> &pcm) {
    std::vector<char> v;
    text(v, "SCWAVE01"); text(v, t.operation.str()); put(v, t.projectEpoch, 8);
    put(v, t.generation, 8); put(v, 12345, 8); text(v, {t.source.sha256.data(), 64});
    put(v, t.source.frames, 8); put(v, t.source.rate, 4); put(v, t.source.channels, 4); put(v, t.baseFramesPerBin, 8);
    check(v.size() == 156, "Wire header size");
    auto width = t.baseFramesPerBin;
    for (std::size_t level = 0; t.source.frames; ++level) {
        const auto bins = (t.source.frames + width - 1) / width;
        for (std::uint64_t bin = 0; bin < bins; ++bin) {
            for (std::uint32_t c = 0; c < t.source.channels; ++c) {
                double lo = std::numeric_limits<double>::infinity(), hi = -lo;
                for (auto f = bin * width; f < std::min(t.source.frames, (bin + 1) * width); ++f) {
                    lo = std::min(lo, pcm.at(std::size_t(f) * t.source.channels + c));
                    hi = std::max(hi, pcm.at(std::size_t(f) * t.source.channels + c));
                }
                put(v, std::bit_cast<std::uint64_t>(lo), 8); put(v, std::bit_cast<std::uint64_t>(hi), 8);
            }
        }
        if (bins == 1) break;
        width = std::min(t.source.frames, width * 2);
        check(level < 64, "Fixture levels");
    }
    return v;
}
void sameUsage(ResourceLedger ledger, ResourceUsage before) {
    const auto now = ledger.usage();
    check(now.reservedBytes == before.reservedBytes && now.owners == before.owners, "Protocol resource leak");
}
}
int main() {
    try {
        ResourceLedger ledger(64 * 1024 * 1024);
        for (auto channels : {1U, 2U, 257U, 1024U}) for (auto frames : {0ULL, 1ULL, 17ULL}) {
            WaveformTicket ticket{Id("12345678-1234-4234-8234-123456789abc"), 9, 47, {{}, frames, 48000, channels}, 3};
            ticket.source.sha256.fill('a'); WaveformLimits limits; limits.baseFramesPerBin = 3;
            std::vector<double> pcm(std::size_t(frames) * channels);
            for (std::size_t i = 0; i < pcm.size(); ++i) pcm[i] = double(int(i % 29) - 12) / 2.0;
            if (!pcm.empty()) pcm.back() = std::nextafter(0.25, 1.0);
            auto wire = fixture(ticket, pcm);
            check(waveformReplySize(ticket, limits) == wire.size(), "Exact reply size");
            const auto baseline = ledger.usage();
            {
                auto wave = decodeWaveformReply(wire, ticket, 12345, ledger, limits);
                check(wave.source() == ticket.source, "Decoded identity");
                for (std::size_t l = 0; l < wave.levels().size(); ++l)
                    for (std::uint64_t b = 0; b < wave.levels()[l].bins; ++b)
                        for (std::uint32_t c = 0; c < channels; ++c) {
                            const auto [first, end] = wave.sourceRange(l, b);
                            double lo = std::numeric_limits<double>::infinity(), hi = -lo;
                            for (auto f = first; f < end; ++f) {
                                const auto sample = pcm.at(std::size_t(f) * channels + c);
                                lo = std::min(lo, sample); hi = std::max(hi, sample);
                            }
                            check(wave.peak(l, b, c) == WaveformPeak{lo, hi}, "Decoded source oracle");
                        }
                auto encoded = encodeWaveformReply(wave, ticket, 12345, ledger, limits);
                check(std::equal(wire.begin(), wire.end(), encoded.bytes().begin(), encoded.bytes().end()), "Independent wire oracle");
                const auto retained = ledger.usage(); auto moved = std::move(encoded);
                check(encoded.bytes().empty() && moved.bytes().size() == wire.size(), "Moved reply exposure");
                sameUsage(ledger, retained);
                auto bad = ticket; ++bad.generation;
                refuse([&] { decodeWaveformReply(wire, bad, 12345, ledger, limits); });
                ++bad.projectEpoch;
                refuse([&] { decodeWaveformReply(wire, bad, 12345, ledger, limits); });
                refuse([&] { decodeWaveformReply(wire, ticket, 67890, ledger, limits); });
                refuse([&] { decodeWaveformReply(wire, ticket, 0, ledger, limits); });
                for (std::size_t byte = 0; byte < 156; ++byte) {
                    auto corrupt = wire; corrupt[byte] ^= 1;
                    refuse([&] { decodeWaveformReply(corrupt, ticket, 12345, ledger, limits); });
                }
                for (const auto size : {std::size_t(0), std::size_t(8), std::size_t(155), wire.size() - 1})
                    refuse([&] { decodeWaveformReply(std::span(wire).first(size), ticket, 12345, ledger, limits); });
                auto trailing = wire; trailing.push_back(0);
                refuse([&] { decodeWaveformReply(trailing, ticket, 12345, ledger, limits); });
                if (frames) {
                    for (double value : {std::numeric_limits<double>::infinity(), -std::numeric_limits<double>::infinity(), std::numeric_limits<double>::quiet_NaN()}) {
                        auto corrupt = wire; replace(corrupt, wire.size() - 8, value);
                        refuse([&] { decodeWaveformReply(corrupt, ticket, 12345, ledger, limits); });
                    }
                    auto inverted = wire; replace(inverted, 156, 1e100);
                    refuse([&] { decodeWaveformReply(inverted, ticket, 12345, ledger, limits); });
                    if (frames > 3) {
                        auto wrongCoarse = wire; replace(wrongCoarse, wire.size() - 8, 1e100);
                        refuse([&] { decodeWaveformReply(wrongCoarse, ticket, 12345, ledger, limits); });
                    }
                }
                ResourceLedger full(1);
                refuse([&] { decodeWaveformReply(wire, ticket, 12345, full, limits); }, ErrorCode::ResourceLimit);
                refuse([&] { encodeWaveformReply(wave, ticket, 12345, full, limits); }, ErrorCode::ResourceLimit);
                check(full.usage().reservedBytes == 0, "Failed credit retained");
                std::stop_source stop; stop.request_stop();
                refuse([&] { decodeWaveformReply(wire, ticket, 12345, ledger, limits, stop.get_token()); }, ErrorCode::Canceled);
                refuse([&] { encodeWaveformReply(wave, ticket, 12345, ledger, limits, stop.get_token()); }, ErrorCode::Canceled);
                auto tiny = limits; tiny.maximumPayloadBytes = 1;
                refuse([&] { waveformReplySize(ticket, tiny); }, ErrorCode::ResourceLimit);
                auto invalid = ticket; invalid.baseFramesPerBin = 0;
                refuse([&] { waveformReplySize(invalid, limits); }, ErrorCode::InvalidParameter);
                invalid = ticket; invalid.source.frames = UINT64_MAX;
                refuse([&] { waveformReplySize(invalid, limits); }, ErrorCode::InvalidParameter);
                invalid = ticket; invalid.source.channels = UINT32_MAX;
                refuse([&] { waveformReplySize(invalid, limits); }, ErrorCode::InvalidParameter);
                invalid = ticket; invalid.source.frames = UINT64_MAX; invalid.source.channels = 1;
                invalid.baseFramesPerBin = 1;
                auto enormous = limits; enormous.baseFramesPerBin = 1;
                enormous.validation.maximumFrames = UINT64_MAX;
                enormous.maximumBaseBins = std::numeric_limits<std::size_t>::max();
                enormous.maximumPayloadBytes = std::numeric_limits<std::size_t>::max();
                refuse([&] { waveformReplySize(invalid, enormous); }, ErrorCode::ResourceLimit);
                sameUsage(ledger, retained);
            }
            sameUsage(ledger, baseline);
        }
        std::cout << checks << " waveform protocol checks passed\n"; return 0;
    } catch (const std::exception &e) { std::cerr << e.what() << '\n'; return 1; }
}
