# ADR 195: prepare protected core sampling in original exact coordinates

Status: accepted for the isolated component, 2026-10-15.

## Context

Protected rendering must eventually support fractional anchors, changed output
sample rates and crops through protected spans. Rounding the semantic geometry or
using an assembled output as the raw source would change the intended protection.
The current worker retains its integer-clock admission until full integration.

## Decision

Prepare owned direct-core windows using exact SourcePosition/SourceFrameMap values.
Use monotone binary search to select destination sample indices; retain rational
semantic boundaries and map each admitted sample back to original raw coordinates.
Use the existing positioned resampler and its declared FIR/edge policy. Own marker
IDs and a resource lease; admit buffers outside this object. Cancellation is checked
during preparation, and processing blocks are bounded to 512 output frames.

Reject unrepresentable arithmetic instead of approximating anchors. Caller must
verify immutable original media. This component does no file I/O or publication.
No new library or license obligation is introduced.

## Consequences and alternatives

A float-coordinate or rounded-anchor implementation is unsuitable for precise crop
phase and large positions. A common-denominator product would introduce avoidable
overflow. Replacing the pinned kernel would mix geometry and quality changes.

This is only the direct-core part: nonunity gaps, halo ownership and blending,
versioned worker binding, history/adoption, and live/export/UI verification still
need implementation. Independent processing quality remains open. See
[design and evidence](../308-protected-core-sampling.md).
