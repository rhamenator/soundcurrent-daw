// SPDX-License-Identifier: GPL-3.0-only
#include <soundcurrent/wasapi_render_trace.hpp>
#include <cstring>
#include <algorithm>
namespace soundcurrent::daw {
namespace {
void add(std::uint64_t &counter, std::uint64_t value) noexcept {
    counter += std::min(value, std::numeric_limits<std::uint64_t>::max()-counter);
}
}
WasapiRenderTrace::WasapiRenderTrace(std::uint32_t channels, std::uint32_t maximumFrames,
        std::uint32_t sampleFrames, std::size_t rows, ResourceLedger resources)
    : channels_(channels), maximumFrames_(maximumFrames) {
    if (!channels || channels > 256 || !maximumFrames || maximumFrames > 65536 ||
        !sampleFrames || std::uint64_t(sampleFrames)*channels > maximumSampleValues ||
        !rows || rows > maximumRows)
        throw ProjectError(ErrorCode::InvalidState, "Invalid render trace admission");
    const auto values = std::size_t(sampleFrames)*channels;
    lease_ = resources.reserve(sizeof(*this)+rows*sizeof(WasapiRenderLeaseObservation)+values*sizeof(float));
    rows_.resize(rows); samples_.resize(values);
}
bool WasapiRenderTrace::prepare(std::uint32_t channels, std::uint32_t maximumFrames) noexcept {
    if (prepared_ || sealed_ || channels != channels_ || maximumFrames != maximumFrames_) return false;
    prepared_ = true; return true;
}
std::size_t WasapiRenderTrace::append(const WasapiRenderClock &clock, std::uint32_t frames) noexcept {
    if (!prepared_ || sealed_ || pending_ != invalidToken ||
        next_ == std::numeric_limits<std::uint64_t>::max()) {
        malformed_ = true; return invalidToken;
    }
    const auto sequence = next_++;
    if (usedRows_ == rows_.size()) { add(lostRows_,1); return invalidToken; }
    const auto token = usedRows_++;
    auto &row = rows_[token]; row.clock = clock; row.sequence = sequence;
    row.requestedFrames = frames; row.sampleOffsetValues = usedValues_;
    return token;
}
std::size_t WasapiRenderTrace::beforeRelease(const WasapiRenderClock &clock, std::uint32_t frames,
        WasapiRenderAction action, std::span<const float> data) noexcept {
    const auto token = append(clock,frames);
    if (token == invalidToken) {
        if (action != WasapiRenderAction::Abort) add(lostSampleFrames_,frames);
        return token;
    }
    auto &row = rows_[token]; row.action = action; row.acquired = true; pending_ = token;
    if (!frames || frames > maximumFrames_) { malformed_ = true; return token; }
    if (action == WasapiRenderAction::Abort) return token; // No unused SDK memory read.
    const auto values = std::size_t(frames)*channels_;
    if (data.size() < values || !data.data()) { malformed_ = true; return token; }
    if (values > samples_.size()-usedValues_) { add(lostSampleFrames_,frames); return token; }
    std::memcpy(samples_.data()+usedValues_,data.data(),values*sizeof(float));
    usedValues_ += values; row.copiedFrames = frames; row.samplesComplete = true;
    return token;
}
void WasapiRenderTrace::released(std::size_t token, std::int32_t hresult, std::uint32_t frames) noexcept {
    if (token == invalidToken) return; // A recorded loss, never an invented release.
    if (sealed_ || token != pending_ || token >= usedRows_) { malformed_ = true; return; }
    auto &row = rows_[token]; row.releaseHresult = hresult; row.releasedFrames = frames;
    row.releaseObserved = true; pending_ = invalidToken;
    if (frames != (row.action == WasapiRenderAction::Abort ? 0 : row.requestedFrames)) malformed_ = true;
}
void WasapiRenderTrace::acquireFailed(const WasapiRenderClock &clock, std::uint32_t frames,
                                    std::int32_t hresult) noexcept {
    const auto token = append(clock,frames);
    if (token != invalidToken) rows_[token].acquireHresult = hresult;
    if (hresult >= 0) malformed_ = true;
}
void WasapiRenderTrace::sealAfterJoin() noexcept {
    if (pending_ != invalidToken) malformed_ = true;
    sealed_ = true;
}
void WasapiRenderTrace::requireSealed() const {
    if (!sealed_) throw ProjectError(ErrorCode::InvalidState, "Render trace requires joined producer");
}
std::span<const WasapiRenderLeaseObservation> WasapiRenderTrace::observations() const {
    requireSealed(); return {rows_.data(),usedRows_};
}
std::span<const float> WasapiRenderTrace::samples() const {
    requireSealed(); return {samples_.data(),usedValues_};
}
} // namespace soundcurrent::daw
