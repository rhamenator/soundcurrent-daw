# ADR098: constant clip playback speed

Date: 2026-10-09. Status: implemented, qualification recorded in checkpoint132.

A clip has an exact rational speed multiplier independent of physical source and
project sample rates. This is linked speed/pitch processing. Independent pitch
and pitch-preserving stretch remain separate required processors; no quality or
functional parity follows from this control.

## Representation and bounds

`ClipPlaybackRate` admits positive numerator/denominator components1..4000 and
speed1/4..4. The UI offers0.250..4.000 in0.001 increments and shows the stored
exact ratio. Unchanged Apply preserves that exact ratio despite display rounding.
Canonical refresh also handles coalesced Apply/Undo while the control has focus. Edits reduce fractions. Other valid rational state retains its exact
numeric state until explicitly edited. The effective source/project step is reduced
from physical rates multiplied by speed. Each product is at most1,536,000,000;
quotient/remainder products fit unsigned64. Fractional origins still require an
unsigned64 common denominator; non-representable state refuses.

Schema1.11 adds strict `playbackRate` with numerator, denominator and
`speed-pitch-linked-v1` mode. Versions1.0..1.10 migrate with1/1 speed; older
mixed physical rates retain the existing explicit refusal. Missing fields,
unknown mode, non-integral/boolean/string/negative values, oversized components
and out-of-range ratios refuse. Unknown stretch properties are not interpreted.

## Edit semantics

Keep timeline start and exact source origin. New project duration is the ceiling
of old length times old speed divided by new speed. Bound the final sample to the
complete owned asset; slower playback cannot invent samples beyond that asset.
The rounded source endpoint may move by less than one new source step. Repeated
rate changes are therefore not an exact inverse operation; Undo/Redo restores
the exact snapshot. Overflowing duration/fade arithmetic refuses atomically.

Rescale fade starts with signed floor and ends with signed ceiling. This preserves
an admitted nonzero fade window, with at most one frame of anchor rounding. Keep
curve, shape, clip gain, polarity and mute. Export range stays explicitly authored;
users can extend it for slower clips. Crops/splits and integer source trims use
the effective speed. A batch's failure leaves the session and history unchanged.

## Rendering and ownership

Reuse the pinned BSD positioned best-sinc kernel with the exact effective step.
Anti-alias scale, source context and admitted source buffers follow that step.
A unit effective step with integer origin copies exactly, including cancellation
between physical rate and clip speed. Split/crop retain full asset context.
Live playback and offline export use the same TrackReader implementation.

The immutable FIR, file access, validation and buffer preparation remain on the
serialized worker. Audio callbacks consume bounded prepared slabs. Resource
admission charges the effective maximum context before allocation/media reads.
No new dependency, equalizer checkout edit, private resampler ABI or mutable
callback state is introduced. Stable FIR ID remains unchanged: neutral/matching
old state must preserve its exact waveform. Rate automation, live input resampling,
processor PDC, independent pitch/stretch and reference-quality comparison remain
open. No built installer or physical-device deadline follows from synthetic tests.
