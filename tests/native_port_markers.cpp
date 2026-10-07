// SPDX-License-Identifier: GPL-3.0-only
#include "native_port_markers.hpp"
#include <algorithm>
#include <stdexcept>
#ifdef SC_NATIVE_PORT_HANDOFF
#include "native_port_handoff.hpp"
#endif
namespace native_fixture {
thread_local PortMarkers *activePortMarkers = nullptr;
namespace {
std::size_t admittedRows(std::size_t count, std::uint32_t channels, std::uint32_t calls,
                         std::uint32_t maximum) {
    if (!count || count > 8192 || !channels || channels > 32 || calls < channels || calls > 64 ||
        !maximum || maximum > 65536)
        throw std::invalid_argument("Invalid native marker admission");
    return count;
}
} // namespace
PortMarkers::PortMarkers(std::size_t count, std::uint32_t channels, std::uint32_t calls,
                         std::uint32_t max, bool source)
    : rows_(admittedRows(count, channels, calls, max)), channels_(channels), expectedCalls_(calls),
      maximumFrames_(max), source_(source) {}
void PortMarkers::begin() noexcept {
    ++calls_;
    ordinal_ = 0;
    current_ = retained_ < rows_.size() ? &rows_[retained_++] : nullptr;
    if (!current_)
        ++dropped_;
    activePortMarkers = this;
}
void PortMarkers::clock(const soundcurrent::daw::DeviceBlockClock &c) noexcept {
    if (current_) {
        current_->clock = c;
        current_->clockKnown = true;
    }
}
void PortMarkers::samples(PortMarker &m, const float *p, std::uint32_t n) noexcept {
    m.frames = n;
    m.present = p != nullptr;
    // Never inspect a buffer of unknown/unsupported extent or before its clock.
    if (!p || !current_->clockKnown || !n || n > maximumFrames_ || n != current_->clock.duration)
        return;
    const auto head = std::min(n, 4U);
    for (unsigned i = 0; i < head; ++i) {
        m.bits[i] = std::bit_cast<std::uint32_t>(p[i]);
        m.bits[4 + i] = std::bit_cast<std::uint32_t>(p[n - head + i]);
    }
    m.sampled = true;
}
void PortMarkers::buffer(const float *p, std::uint32_t n) noexcept {
    const auto index = ordinal_++;
    if (!current_)
        return;
    ++current_->bufferCalls;
    if (index >= channels_)
        return; // Owner outputs must not be treated as inputs.
    auto &m = current_->ports[index];
    m.seen = true;
    m.present = p != nullptr;
    m.frames = n;
    if (p) {
        auto &ids = identities_[index];
        auto &count = identityCounts_[index];
        unsigned i = 0;
        while (i < count && ids[i] != p)
            ++i;
        if (i == count) {
            if (count == ids.size()) {
                ++identityOverflows_;
            } else {
                ids[count++] = p;
                m.bufferIdentity = i;
            }
        } else
            m.bufferIdentity = i;
    }
    if (!source_)
        samples(m, p, n);
}
void PortMarkers::generated(std::span<float *const> views, std::uint32_t n) noexcept {
    if (!current_ || !source_)
        return;
    ++current_->generatedCalls;
    for (unsigned c = 0; c < channels_ && c < views.size(); ++c)
        samples(current_->ports[c], views[c], n);
}
void PortMarkers::end(soundcurrent::daw::Frame before, soundcurrent::daw::Frame after) noexcept {
    if (current_) {
        current_->before = before;
        current_->after = after;
    }
    current_ = nullptr;
    if (activePortMarkers == this)
        activePortMarkers = nullptr;
}
void PortMarkers::bridge() noexcept {
    if (current_)
        ++current_->bridgeCalls;
}
void PortMarkers::write(std::ostream &o) const {
    o << "{\"test_only\":true,\"source\":" << (source_ ? "true" : "false")
      << ",\"channels\":" << channels_ << ",\"expected_buffer_calls\":" << expectedCalls_
      << ",\"admitted_rows\":" << rows_.size()
      << ",\"prepared_bytes\":" << rows_.size() * sizeof(PortMarkerRow) << ",\"calls\":" << calls_
      << ",\"retained\":" << retained_ << ",\"dropped\":" << dropped_
      << ",\"identity_overflows\":" << identityOverflows_ << ",\"rows\":[";
    for (std::size_t n = 0; n < retained_; ++n) {
        if (n)
            o << ',';
        const auto &r = rows_[n];
        const auto &c = r.clock;
        o << "{\"clock_known\":" << (r.clockKnown ? "true" : "false")
          << ",\"position\":" << c.position << ",\"duration\":" << c.duration
          << ",\"nsec\":" << c.monotonicNs << ",\"id\":" << c.id << ",\"cycle\":" << c.cycle
          << ",\"rate_numerator\":" << c.rateNumerator
          << ",\"rate_denominator\":" << c.rateDenominator << ",\"delay\":" << c.delay
          << ",\"before\":" << r.before << ",\"after\":" << r.after
          << ",\"buffer_calls\":" << r.bufferCalls << ",\"generated_calls\":" << r.generatedCalls
          << ",\"bridge_calls\":" << r.bridgeCalls << ",\"ports\":[";
        for (unsigned ch = 0; ch < channels_; ++ch) {
            if (ch)
                o << ',';
            const auto &m = r.ports[ch];
            o << "{\"channel\":" << ch << ",\"frames\":" << m.frames
              << ",\"buffer_identity\":" << m.bufferIdentity
              << ",\"seen\":" << (m.seen ? "true" : "false")
              << ",\"present\":" << (m.present ? "true" : "false")
              << ",\"sampled\":" << (m.sampled ? "true" : "false") << ",\"bits\":[";
            for (unsigned i = 0; i < m.bits.size(); ++i) {
                if (i)
                    o << ',';
                o << m.bits[i];
            }
            o << "]}";
        }
        o << "]}";
    }
    o << "]}\n";
}
} // namespace native_fixture
extern "C" void *__real_pw_filter_get_dsp_buffer(void *, std::uint32_t);
extern "C" void *__wrap_pw_filter_get_dsp_buffer(void *port, std::uint32_t n) {
#ifdef SC_NATIVE_PORT_HANDOFF
    native_fixture::handoffBeforeDsp(port, n);
#endif
#ifdef SC_NATIVE_STARTUP_GATE
    auto *result =
        native_fixture::handoffSuppressDsp() ? nullptr : __real_pw_filter_get_dsp_buffer(port, n);
#else
    auto *result = __real_pw_filter_get_dsp_buffer(port, n);
#endif
#ifdef SC_NATIVE_PORT_HANDOFF
    native_fixture::handoffAfterDsp(result);
#endif
    if (auto *trace = native_fixture::activePortMarkers)
        trace->buffer(static_cast<const float *>(result), n);
    return result;
}
