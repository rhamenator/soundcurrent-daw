# ADR 104: Local warp timing and recording-group quality contracts

2026-10-10 UTC. Status: accepted gate and next experiment; no processor change.
Frozen SC-DAW-BASELINE-2026-10-05 and existing quality tolerances are unchanged.

## Evidence and decision

[Checkpoint145](../145-stretch-region-quality.md) records63 actual helper renders.
Finite exact-duration derivatives and matching live/export output are necessary,
but25 context/whole non-unity crop differences and inconsistent impulse/window
landmarks prevent automatic context or full stretch/group-quality qualification.
Preserve the explicit processing-region workflow and existing algorithm IDs.
Do not change legacy output, silently add padding or label discrete-apart output
as phase-preserving microphone processing.

Separate these contracts before introducing an editable warp marker:

1. **Exact edit geometry.** A versioned monotone piecewise map retains raw source
   coordinates, reference rate, output frame domain and stable marker IDs.
   Physical source, processing-region, derived-output, project and tempo domains
   remain explicit. Rationals canonicalize, overflow/reversal/duplicate or
   out-of-domain markers refuse. Implicit endpoints are represented by the
   planner; the R3 adapter omits a0→0 API entry because its initial ratio divides
   by the first source key. Marker state is not an assertion of audible alignment.
   Processor/cache keys bind the complete normalized map, channel/group policy,
   edge policy and algorithm version. Preparation/publication remains off callback.
2. **Marked event timing.** P011 still requires transient drift **<1 ms** and
   Q-STRETCH listening. At48 kHz this is strictly <48 frames. Define event starts
   in the owned source before rendering. Measure onset/envelope displacement and
   pre-echo/tail windows independently of the largest sample; use isolated attacks
   with known starts, several carriers, repeated hits and difficult short/edge
   spans. Detector bias must be characterized with original/unity fixtures first.
   Pitch tests use envelope/event timing independently of carrier phase. A test
   with no reliable landmark is unresolved, not a pass. Context variants and
   input partitions must each satisfy the timing gate, not merely match a full
   render which could itself be displaced. Listening and real-music gates remain.
3. **Linked editing.** P008 group membership, selection, markers, splits and moves
   use one exact reference map and one Undo transaction. Exact original physical
   offsets remain stored; flattening cannot silently change them. Linked edit
   state alone does not qualify the acoustic phase-preserving group mode.
4. **Phase-preserving recording groups.** The P008 exact-relative-offset/no-added-
   drift requirement remains. Shared reference attacks must retain each channel's
   physical arrival offset around those attacks, including polarity and fractional
   origins. Uniformly scaling a microphone's propagation delay is not a substitute
   for that requirement. Independently measure interchannel attack offsets,
   cross-spectral relative phase on sustained probes and cancellation/mono fold
   on coincident/inverted copies; record windows and estimator uncertainty.
   Preserve physical offset metadata exactly. Any acoustic tolerance needed by
   an estimator must be predeclared and cannot weaken the parity contract.
   Nonzero pitch additionally distinguishes carrier-frequency change from
   propagation delay. Correlation of a short ringing impulse alone is insufficient.
   Do not infer this mode from a vendor option called channels-together.
5. **Persistence and rendering.** Raw bytes/anchors survive render, Apply, Undo,
   save/reopen, split/crop, flatten/restore and recovery. Independently validated
   shared artifact playback/export must agree, retain float headroom, explicit
   latency/tails and bounded memory/deadlines. Existing source-asset FIR tap
   extension is distinct from inventing stretch context at an edge. Algorithms
   must specify any new edge/padding/crossfade policy before it can be adopted.

## Bounded next slice and dependencies

First implement the original planner plus an isolated candidate adapter probe,
not a production marker UI or new dependency. Use the already pinned
Rubber Band4.0.0 R3/R2 interfaces with multiple interior anchors and nonuniform
segments, separate overall duration, varied input partitions and retained stderr.
The earlier single-anchor probe is a counterexample, not proof that the API
universally ignores maps: the pinned R3 source stores and consumes key-frame maps
in `setKeyFrameMap`/`updateRatioFromMap`.

Official [integration notes](https://breakfastquay.com/rubberband/integration.html)
describe local rates varying with detected features and two-pass offline use.
The pinned [key-frame API](https://github.com/breakfastquay/rubberband/blob/v4.0.0/rubberband/RubberBandStretcher.h)
requires setting maps before processing and setting overall ratio separately.
The same header describes synchronization tradeoffs for together/apart, with
stereo-specific together behavior. These APIs justify investigation, not acceptance.

[The inventory](../03-dependencies.md#local-warp-candidate-review-2026-10-10)
also evaluates a source-pinned alternative without adoption. Existing R3 IDs,
schema1.14/protocol4 and current packaging stay unchanged. Exit criteria for the
planner/probe are independent rational-map/refusal checks, terminal processes,
full PCM/warning retention and explicit pass/fail/unresolved observations for
each declared event/group contract. Only demonstrated supported cases advance
to an original versioned production adapter and one editable anchor workflow.
Broader tempo warp, segmented pitch, grouped comp, Windows/audio/installers,
all-Europe delivery and all frozen F/Q/C/N workflows remain required.
