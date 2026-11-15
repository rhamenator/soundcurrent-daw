# ADR 196: intrinsic exact grids for clocked protected gaps

Status: accepted for isolated rendering, 2026-10-15.

## Context

Fractional anchors, changed sample clocks and crops through halos cannot be sent
to an integer-only keyframe API by rounding. Cropping the input analysis window
would also change the result of an otherwise identical surviving edit.

## Decision

Prepare each full nonunity gap with exact linear raw context and its own fixed
intrinsic processing/output grids. Compensate the backend's pitch for their step
ratio, then sample the requested destination clock with the positioned kernel.
Keep a bounded streaming context buffer. Blend against original unity raw audio
using full semantic halo distances, independent of crop boundaries. Verify unity
complement intervals across all original knots and bypass R3 for them.

Use a new algorithm identity and preserve the legacy worker path. Add a separate
64-bit ExactProcessingGrid for composed ratios that exceed SourceFrameMap's 32-bit
processing limit. Keep project timing formats and SourceFrameMap admission intact.
The kernel overload shares coefficient/tap arithmetic; checked wide extents avoid
multiplication overflow. All of this runs outside real-time callbacks.

## Alternatives and consequences

Rounding vendor keyframes changes semantic positions. Re-rendering only a cropped
window changes analysis context. Retaining a whole rendered gap in RAM scales with
duration. The selected design keeps exact coordinates, stable crop behavior and
bounded explicit storage, but can repeat full-gap computation for separate crops.
A future verified cache can reuse those intrinsic results.

Linear extrapolated context and continuous semantic ramps differ from the legacy
keyframed halo algorithm; versioned worker binding and independent quality evidence
are required before application adoption. Original media admission, backend heap/
CPU limits and publication remain responsibilities of the isolated worker.

See [design and acceptance](../309-clocked-protected-warp.md).
