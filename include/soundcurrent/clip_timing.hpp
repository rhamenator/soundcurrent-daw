// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include "session.hpp"

namespace soundcurrent::daw {
struct SourcePosition {
    Frame frame = 0;
    std::uint32_t fraction = 0, denominator = 1;
    bool operator==(const SourcePosition &) const = default;
};
// Exact constant-rate coordinates, independent of GUI/backend/DSP. Control or
// serialized read-ahead owner only. A project frame need not be a source frame.
// Fractional origins survive splitting; integer-only seeks cannot reproduce them.
class SourceFrameMap {
  public:
    SourceFrameMap(std::uint32_t sourceRate, std::uint32_t projectRate,
                   Frame sourceOrigin = 0);
    SourcePosition at(Frame projectOffset) const;
    SourceFrameMap advanced(Frame projectOffset) const;
    Frame projectFramesForSource(Frame sourceFrames) const;
    std::uint32_t numerator() const noexcept { return numerator_; }
    std::uint32_t denominator() const noexcept { return denominator_; }
  private:
    std::uint32_t numerator_, denominator_;
    SourcePosition origin_;
};
} // namespace soundcurrent::daw
