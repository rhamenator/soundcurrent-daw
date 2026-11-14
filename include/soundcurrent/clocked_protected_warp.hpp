// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include "protected_warp_render.hpp"
#include "positioned_resampling.hpp"
namespace soundcurrent::daw {
// New algorithm identity: the legacy integer/keyframed renderer is unchanged.
inline constexpr std::string_view clockedProtectedWarpAlgorithmId="soundcurrent.protected-linear-gap-clocked-v1";
struct ClockedProtectedPiece {
    bool stretched=false;
    SourcePosition begin,end,gapBegin,gapEnd,rawGapBegin,rawGapEnd;
    Frame first=0,last=0,inputFrames=0,renderFrames=0;
    // Destination samples address original raw PCM (direct piece) or the
    // intrinsic rendered gap grid (stretched piece). No rounded semantic knots.
    ExactProcessingGrid samples{SourceFrameMap(48000,48000)},input{SourceFrameMap(48000,48000)};
    SourcePosition renderStep{1,0,1};
};
// OFF-RT immutable preparation. Owns all geometry and memory credit; no borrowed
// source plan, GUI state, or audio. Full gap grids are independent of output crop.
class PreparedClockedProtectedWarp {
  public:
    PreparedClockedProtectedWarp(const ProtectedWarpPlan &,SourceFrameMap outputClock,
        Frame outputFrames,std::uint32_t channels,ResourceLedger,
        const std::function<void()> &poll,ClipWarpLimits={});
    PreparedClockedProtectedWarp(const PreparedClockedProtectedWarp &)=delete;
    PreparedClockedProtectedWarp &operator=(const PreparedClockedProtectedWarp &)=delete;
    PreparedClockedProtectedWarp(PreparedClockedProtectedWarp &&) noexcept=default;
    PreparedClockedProtectedWarp &operator=(PreparedClockedProtectedWarp &&)=delete;
    std::span<const ClockedProtectedPiece> pieces() const noexcept {return pieces_;}
    const SourceFrameMap &clock() const noexcept {return clock_;}
    Frame outputFrames() const noexcept {return outputFrames_;}
    Frame sourceFrames() const noexcept {return sourceFrames_;}
    std::uint32_t channels() const noexcept {return channels_;}
    std::uint32_t rate() const noexcept {return rate_;}
  private:
    ResourceLease lease_;
    std::vector<ClockedProtectedPiece> pieces_;
    SourceFrameMap clock_;
    Frame outputFrames_,sourceFrames_;
    std::uint32_t channels_,rate_;
};
struct ClockedProtectedReport {
    Frame writtenFrames=0,generatedGapFrames=0;
    std::size_t processedGaps=0,maximumBufferedFrames=0;
    double floatPeak=0;
};
// Reads ABSOLUTE original-asset frames in <=512-frame calls, including FIR
// context. Caller binds immutable media and discards partial output on failure.
// Slabs are admitted before allocation; the isolated worker must additionally
// bound the vendor backend's allocations and CPU. Not a real-time callback API.
ClockedProtectedReport renderClockedProtectedWarp(const PreparedClockedProtectedWarp &,
    const WarpRead &,const WarpWrite &,ResourceLedger,const std::function<void()> &poll);
} // namespace soundcurrent::daw
