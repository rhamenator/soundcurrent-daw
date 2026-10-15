// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include "clip_timing.hpp"
namespace soundcurrent::daw {
// OFF-RT processing-only coordinates. Does not change project timing formats or
// SourceFrameMap admission. Wide reduced ratios use checked exact arithmetic.
class ExactProcessingGrid {
  public:
    ExactProcessingGrid(const SourceFrameMap &);
    static ExactProcessingGrid processingGrid(SourcePosition origin,SourcePosition span,Frame frames);
    SourcePosition at(Frame offset) const;
    ExactProcessingGrid advanced(Frame offset) const;
    std::uint64_t numerator() const noexcept {return numerator_;}
    std::uint64_t denominator() const noexcept {return denominator_;}
  private:
    std::uint64_t numerator_=1,denominator_=1;
    SourcePosition origin_;
};
} // namespace soundcurrent::daw
