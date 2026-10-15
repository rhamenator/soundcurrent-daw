// SPDX-License-Identifier: GPL-3.0-only
#include <soundcurrent/waveform_protocol.hpp>
#include <algorithm>
#include <bit>
#include <cmath>
#include <limits>

namespace soundcurrent::daw {
namespace {
static_assert(sizeof(double) == 8 && std::numeric_limits<double>::is_iec559);
void check(bool ok, const char *message, ErrorCode code = ErrorCode::InvalidState) {
    if (!ok) throw ProjectError(code, message);
}
void poll(std::stop_token stop) {
    check(!stop.stop_requested(), "Waveform protocol canceled", ErrorCode::Canceled);
}
struct Layout {
    std::array<WaveformLevel, 64> levels{};
    std::size_t count = 0, samples = 0, payload = 0, wire = 0;
};
Layout plan(const WaveformTicket &t, const WaveformLimits &limits) {
    check(t.operation.str().size() == 36 && limits.baseFramesPerBin && limits.maximumBaseBins && limits.maximumPayloadBytes &&
        t.baseFramesPerBin == limits.baseFramesPerBin && t.source.rate && t.source.channels &&
        t.source.channels <= limits.validation.maximumChannels &&
        t.source.frames <= limits.validation.maximumFrames &&
        std::all_of(t.source.sha256.begin(), t.source.sha256.end(), [](char c) {
            return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f');
        }), "Invalid waveform ticket or trusted policy", ErrorCode::InvalidParameter);
    Layout p;
    auto width = t.baseFramesPerBin;
    auto bins = t.source.frames / width + (t.source.frames % width != 0);
    check(bins <= limits.maximumBaseBins, "Waveform protocol bin limit", ErrorCode::ResourceLimit);
    PayloadCharge charge("Waveform reply model", limits.maximumPayloadBytes);
    charge.add(sizeof(Waveform));
    while (bins) {
        check(p.count < p.levels.size() && bins <= std::numeric_limits<std::size_t>::max() / t.source.channels,
              "Waveform reply layout overflow", ErrorCode::ResourceLimit);
        const auto n = std::size_t(bins) * t.source.channels;
        charge.add(n, sizeof(WaveformPeak));
        p.levels[p.count++] = {width, bins, p.samples};
        p.samples += n;
        if (bins == 1) break;
        bins = bins / 2 + bins % 2;
        width = width >= t.source.frames - width ? t.source.frames : width * 2;
    }
    p.payload = charge.bytes();
    check(p.samples <= (std::numeric_limits<std::size_t>::max() - waveformReplyHeaderBytes) / 16,
          "Waveform wire size overflow", ErrorCode::ResourceLimit);
    p.wire = waveformReplyHeaderBytes + p.samples * 16;
    return p;
}
struct Writer {
    char *at;
    void text(std::string_view s) { at = std::copy(s.begin(), s.end(), at); }
    void number(std::uint64_t v, unsigned n) {
        for (unsigned i = 0; i < n; ++i) *at++ = char((v >> (i * 8)) & 255);
    }
};
struct Reader {
    const char *at;
    void text(std::string_view s) {
        check(std::equal(s.begin(), s.end(), at), "Waveform reply identity/schema differs"); at += s.size();
    }
    std::uint64_t number(unsigned n) {
        std::uint64_t v = 0;
        for (unsigned i = 0; i < n; ++i) v |= std::uint64_t(static_cast<unsigned char>(*at++)) << (8 * i);
        return v;
    }
};
void header(Writer &w, const WaveformTicket &t, std::uint64_t pid) {
    w.text("SCWAVE01"); w.text(t.operation.str());
    w.number(t.projectEpoch, 8); w.number(t.generation, 8); w.number(pid, 8);
    w.text({t.source.sha256.data(), t.source.sha256.size()}); w.number(t.source.frames, 8);
    w.number(t.source.rate, 4); w.number(t.source.channels, 4); w.number(t.baseFramesPerBin, 8);
}
} // namespace
std::size_t waveformReplySize(const WaveformTicket &t, WaveformLimits limits) { return plan(t, limits).wire; }
WaveformReply encodeWaveformReply(const Waveform &wave, const WaveformTicket &t,
    std::uint64_t pid, ResourceLedger ledger, WaveformLimits limits, std::stop_token stop) {
    poll(stop); const auto p = plan(t, limits);
    check(pid && wave.source() == t.source && wave.levels().size() == p.count,
          "Waveform encoder source differs");
    WaveformReply reply;
    reply.lease_ = ledger.reserve(p.wire);
    reply.data_ = std::make_unique<char[]>(p.wire); reply.size_ = p.wire;
    Writer w{reply.data_.get()}; header(w, t, pid);
    for (std::size_t l = 0; l < p.count; ++l) {
        const auto &level = p.levels[l];
        check(wave.levels()[l].framesPerBin == level.framesPerBin && wave.levels()[l].bins == level.bins,
              "Waveform encoder resolution differs");
        for (std::uint64_t b = 0; b < level.bins; ++b) {
            if (b % 256 == 0) poll(stop);
            for (std::uint32_t c = 0; c < t.source.channels; ++c) {
                const auto &peak = wave.peak(l, b, c);
                w.number(std::bit_cast<std::uint64_t>(peak.minimum), 8);
                w.number(std::bit_cast<std::uint64_t>(peak.maximum), 8);
            }
        }
    }
    poll(stop); return reply;
}
Waveform decodeWaveformReply(std::span<const char> bytes, const WaveformTicket &t,
    std::uint64_t pid, ResourceLedger ledger, WaveformLimits limits, std::stop_token stop) {
    poll(stop); const auto p = plan(t, limits);
    check(pid && bytes.size() == p.wire, "Waveform reply length/PID differs");
    // Exact geometry has been admitted before pointer reads or model allocation.
    std::array<char, waveformReplyHeaderBytes> expected{};
    Writer writer{expected.data()}; header(writer, t, pid);
    Reader reader{bytes.data()}; reader.text({expected.data(), expected.size()});
    Waveform wave;
    wave.reservation_ = ledger.reserve(p.payload);
    wave.source_ = t.source; wave.levels_ = p.levels; wave.levelCount_ = p.count;
    wave.peaks_ = std::make_unique<WaveformPeak[]>(p.samples);
    for (std::size_t l = 0; l < p.count; ++l) {
        const auto &level = p.levels[l];
        for (std::uint64_t b = 0; b < level.bins; ++b) {
            if (b % 256 == 0) poll(stop);
            for (std::uint32_t c = 0; c < t.source.channels; ++c) {
                const WaveformPeak peak{std::bit_cast<double>(reader.number(8)),
                                        std::bit_cast<double>(reader.number(8))};
                check(std::isfinite(peak.minimum) && std::isfinite(peak.maximum) && peak.minimum <= peak.maximum,
                      "Invalid waveform extrema");
                if (l) {
                    auto reduced = wave.peak(l - 1, b * 2, c);
                    if (b * 2 + 1 < p.levels[l - 1].bins) {
                        const auto &right = wave.peak(l - 1, b * 2 + 1, c);
                        reduced.minimum = std::min(reduced.minimum, right.minimum);
                        reduced.maximum = std::max(reduced.maximum, right.maximum);
                    }
                    check(peak == reduced, "Inconsistent waveform coarse extrema");
                }
                wave.peaks_[level.sampleOffset + std::size_t(b) * t.source.channels + c] = peak;
            }
        }
    }
    poll(stop); return wave;
}
} // namespace soundcurrent::daw
