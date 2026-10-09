// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include "session.hpp"

namespace soundcurrent::daw {
// Exact constant-rate coordinates, independent of GUI/backend/DSP. Control or
// serialized read-ahead owner only. A project frame need not be a source frame.
// Fractional origins survive splitting; integer-only seeks cannot reproduce them.
class SourceFrameMap {
  public:
    SourceFrameMap(std::uint32_t sourceRate, std::uint32_t projectRate,
                   Frame sourceOrigin = 0);
    SourceFrameMap(std::uint32_t sourceRate, std::uint32_t projectRate, SourcePosition);
    SourcePosition at(Frame projectOffset) const;
    SourceFrameMap advanced(Frame projectOffset) const;
    SourceFrameMap translated(Frame signedProjectOffset) const;
    Frame projectFramesForSource(Frame sourceFrames) const;
    std::uint32_t numerator() const noexcept { return numerator_; }
    std::uint32_t denominator() const noexcept { return denominator_; }
  private:
    std::uint32_t numerator_, denominator_;
    SourcePosition origin_;
};
SourceFrameMap clipSourceMap(const Clip &, std::uint32_t sourceRate, std::uint32_t projectRate);
} // namespace soundcurrent::daw
