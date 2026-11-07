// SPDX-License-Identifier: GPL-3.0-only
#include <soundcurrent/exact_processing_grid.hpp>
#include <soundcurrent/stretch.hpp>
namespace soundcurrent::daw {
namespace {void require(bool v,const char *m){if(!v)throw ProjectError(ErrorCode::InvalidParameter,m);}}
ExactProcessingGrid::ExactProcessingGrid(const SourceFrameMap &m):numerator_(m.numerator()),denominator_(m.denominator()),origin_(scaleSourcePosition(m.at(0),1,1)){}
ExactProcessingGrid ExactProcessingGrid::processingGrid(SourcePosition origin,SourcePosition span,Frame frames){
    require(frames>0 && frames<=1000000000,"Invalid exact processing extent");
    const auto ratio=sourceIntervalRatio(span,{frames,0,1});
    const auto step=scaleSourcePosition({1,0,1},ratio.numerator,ratio.denominator);
    require(!sourcePositionLess(step,{0,1,192}) && !sourcePositionLess({192,0,1},step),"Exact grid ratio exceeds kernel range");
    ExactProcessingGrid result(SourceFrameMap(48000,48000,origin));
    result.numerator_=ratio.numerator;result.denominator_=ratio.denominator;
    (void)result.at(frames);return result;
}
SourcePosition ExactProcessingGrid::at(Frame offset) const {
    require(offset>=0,"Negative exact processing offset");
    // Most sample indices have a directly representable product, even when
    // the reduced ratio needs 64 bits. Keep the checked wide fallback for the
    // full admitted domain instead of paying its bitwise division per sample.
    if(std::uint64_t(offset)<=UINT64_MAX/numerator_){
        const auto product=std::uint64_t(offset)*numerator_,whole=product/denominator_;
        require(whole<=std::uint64_t(INT64_MAX),"Exact processing coordinate overflow");
        return addSourcePosition(origin_,{Frame(whole),product%denominator_,denominator_});
    }
    return addSourcePosition(origin_,scaleSourcePosition({offset,0,1},numerator_,denominator_));
}
ExactProcessingGrid ExactProcessingGrid::advanced(Frame offset) const {auto result=*this;result.origin_=at(offset);return result;}
} // namespace soundcurrent::daw
