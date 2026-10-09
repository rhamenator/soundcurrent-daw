# Independent pitch/time stretch: candidate feasibility

Date2026-10-09. Frozen baseline SC-DAW-BASELINE-2026-10-05. Candidate only;
not adopted in the DAW and no full functional/quality/native parity promotion.

## Pin and build

Official Rubber Band4.0.0 release archive, SHA256
`af050313ee63bc18b35b2e064e5dce05b276aaf6d1aa2b8a82ced1fe2f8028e9`.
Tag v4.0.0 commit1d95888bec3ae0a17c0c4af791810d5a63f6bc35.
Unmodified upstream single/RubberBandSingle.cpp, builtin FFT/BQ resampler,
NO_THREADING/NO_TIMING/NO_THREAD_CHECKS. GPL-2.0-or-later offers laterGPL3 option
for this GPL3-only application; original COPYING/notices remain required.
Release source62 actual compiled inputs hashed. Linux GCC15.2 and Windows
MinGW13 cross-build pass. No Windows runtime or redistribution qualification.
Official sources:
- https://breakfastquay.com/rubberband/license.html
- https://breakfastquay.com/rubberband/integration.html
- https://github.com/breakfastquay/rubberband/tree/v4.0.0
- https://github.com/breakfastquay/rubberband/blob/v4.0.0/rubberband/RubberBandStretcher.h
- https://github.com/breakfastquay/rubberband/blob/v4.0.0/single/RubberBandSingle.cpp

## Actual selected experiment

Offline R3, channels together, fixed max512 frames, expected input duration,
study/process/retrieve drain, identical generated0.2-amplitude1kHz tone on all
channels.15 configurations:8k/48k/192k rates times1/2/3/8/32 channels;
duration1.5x independently from pitch2x.97/512 input partitions produce identical
output for these points. Central2kHz amplitudes0.198684–0.20009; original1kHz
amplitude <=6.21487e-6. All output finite; identical-channel differences0.
These are selected steady-state points, not transient/image/formant/layout tests.
The whole experiment used50.86s wall,50.68s CPU, peak210944KiB RSS,
zero swaps and zero filesystem input.60s bounded process budget. This is no
per-instance memory bound, deadline or realtime qualification.

Duration counterexample:4816 source frames at4/3 produces6421 frames, nearest
integer; exact canonical ceil is6422.4817 produces6423 for both. Do not silently
use a floating processor ratio as the authoritative project duration.

## Required integration decisions before adoption

1. All calls on serialized worker. Constructor/process/drain may allocate/log;
   callback consumes admitted immutable slabs only. Max input block alone does
   not bound all vendor allocations. Audit workspace growth and overlap against
   ResourceLedger before allocation; refuse/cancel without publishing partial state.
2. Official constructor supports8k–192k; current DAW supports384k. No unsupported
   high-rate call or silent bandwidth loss. A qualified high-rate route or another
   implementation is required; keeping the gap visible does not remove the feature.
3. Canonical source/time domains, integer exact duration and fractional crop origins
   must be explicit. Vendor nearest-round output must match authoritative duration
   or be refused before publication; no unreported trimming/padding. Very long
   frames beyond precise double integer range require a segmented/exact design.
4. Stateful reset at seek/split/crop changes waveform. Evaluate source-anchored
   cancellable derived render caches shared by live/export. Define immutable keys
   (asset/hash/rate/layout/source anchors/engine/options/pitch/stretch), bounded
   disk/RAM admission, atomic completion, cancellation, recovery and stale-cache
   invalidation. No unbounded prefix replay in an audio callback or hidden prepare.
5. ChannelsTogether documents stereo handling. Identical32-channel tones do not
   establish discrete/surround/Ambisonics semantics. Qualify distinct channels,
   impulses, spatial image and allowed grouping/layout policies independently.
6. R3 study source currently accumulates duration rather than a transient analysis
   pass. Do not infer analysis quality from two-pass API naming. Establish chosen
   R2/R3 modes and reference-relative transient/phase/pitch/formant acceptance.
7. Record processor latency, padding, tails/final drain, exact dry bypass,
   automation/tempo-map scheduling and portable versioned state. Dynamic warps,
   vocal/note pitch editing and reference-aligned quality remain required.
8. Preserve source/licenses, packaging inventory and Windows native execution;
   release binary and corresponding source remain separate authorization gates.

## Concrete next implementation task

Define and test the independent stretch job's resource/lifecycle and exact timing
contract before adding a desktop control. Implement a bounded source-anchored
worker feasibility job over owned WAV media, including completion/cancel/refusal,
exact duration, cache retirement, split/crop/seek consistency and shared live/export
reads. Vendor dependency adoption follows this contract and pinned notice audit.

### Cache scope to evaluate

A complete source asset can be much longer than the clip being edited. Do not
require a full-asset render implicitly. A versioned processing anchor can identify
an explicitly admitted source span and processing settings; splits/crops retain
that anchor and address the same immutable rendered span. Random seeking then
reads a prepared artifact rather than restarting vendor phase state. Changing
pitch/stretch creates a new cancellable anchor job; old/new overlap is charged,
raw media stays immutable and GUI reports preparation progress. Full-asset,
region-anchored and streamed approaches need compared waveform/context tests
before selecting one. No hidden truncated context or spontaneous partial playback.
