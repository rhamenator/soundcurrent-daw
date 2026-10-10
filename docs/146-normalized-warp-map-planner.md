# Exact warp-map planner and multi-anchor processor experiment

2026-10-10 UTC. Frozen SC-DAW-BASELINE-2026-10-05 remains unchanged. This
M2/M8 increment implements an original framework-independent C++20 planner,
behind the default-OFF `SC_BUILD_WARP_MAP_PROBE` flag. It is not installed or
connected to the application editor. [ADR105](decisions/105-normalized-warp-map-planner.md)
records the domain and admission decisions; [ADR104](decisions/104-local-warp-and-group-quality.md)
continues to govern audible timing/group quality.

## Implemented geometry

[WarpMap](../experiments/warp-map/warp_map.hpp) owns immutable normalized points:
implicit0→0 and input-end→output-end plus explicitly identified interior markers.
Source values are physical-frame offsets from a retained raw origin; output
values are derived-output frame offsets. Physical sample rate, available asset
extent, prepared input length and visible source interval remain explicit.
These are not project/tempo positions. Forward and inverse piecewise-linear
evaluation, absolute raw coordinates and mapped visible bounds use exact reduced
rationals and the existing portable integer helpers, including native-MSVC
compatible multiplication/division without `__int128`.

Unordered markers normalize by source coordinate, retaining stable IDs. Duplicate
IDs/source/output positions, reversed maps, noninterior markers, invalid integer
fields, unavailable source grids, invalid visible intervals, resource excess and
unrepresentable arithmetic refuse. A conservative uint64 segment-scale
representation can refuse some mathematically valid large mixed fractions;
it never repairs them with floating timing. Identity maps have an exact fast path.
Expanding that representation remains a future requirement when justified by
admitted workflows; this experiment does not redefine the product's final scope.

Payload credit is acquired before owned point/string/scratch allocation. Default
trusted admission is4096 interior markers /1 MiB payload. These are configurable
experiment limits, not a final product track/marker limit or an allocator/RSS
claim. Credits return after success and constructor/lookup refusals. All mutation,
normalization, vendor-map allocation and object destruction are control-side.
No realtime callback use is qualified.

The integer-only vendor adapter omits both implicit endpoints, especially the
zero first key that R3 divides by when initializing its ratio. Fractional markers
remain representable in the exact planner but explicitly refuse in this adapter.
The prepared fractional source grid retains the previous asset-edge convention:
its last sample must precede the asset end; its exclusive mathematical extent
can lie beyond that end by a fraction. `rawAt` returns grid coordinates, not an
authorization to read new media. Interior markers must have physical positions
inside the actual asset. No context, padding or source content is manufactured.

## Local acceptance and portability gate

The actual C++ geometry probe passed **23,083 independent Fraction-oracle checks**
over516 requests:489 accepted and27 refused. The seeded480 monotone fractional
maps include exact inverse round trips, canonical order/IDs, raw/visible domains
and fractional vendor refusal. Additional cases exercise INT64-scale endpoints,
near-UINT64 denominators, typed overflow, nil/duplicate IDs, domain reversals and
independent marker/payload/shared-ledger refusal. Refusal and post-release credit
are zero in the covered paths.

A fresh low-priority CMake Release build compiled ten steps for the existing
session support and new planner. After an explicit portable `<cstdio>` include,
the final two-step rebuild and repeatable one-test CTest workflow pass. Each run
creates an exclusive UUID evidence child, preserving prior observations.
Current native Windows geometry acceptance is a separate gate: CI explicitly
enables/builds the probe, adds its oracle to the native core tests and retains its
actual request/output/summary files. Existing Qt, hardware and installed receipts
retain their own source/scope; this Linux observation does not relabel them.

## Actual processor comparison

The standalone [render probe](../experiments/warp-map/render_probe.cpp) uses the
already pinned unchanged Rubber Band4.0.0, offline/no internal threading,
preserved formants and high-quality pitch option. It renders original16,384-frame
48 kHz float signals into24,576 frames: stereo impulses, attacks and two-tone
sustain, plus eight-channel attacks with polarity/gain and0,3,…,21-frame delays.

The three profiles are constant ratio with no map, the same uniform ratio with
four interior anchors, and this nonuniform map:

| Source frame | Output frame | Segment ratio from prior point |
| ---: | ---: | ---: |
| 0 implicit | 0 implicit | — |
| 2048 | 3072 | 3/2 |
| 6144 | 8192 | 5/4 |
| 10240 | 16384 | 2/1 |
| 14336 | 21504 | 5/4 |
| 16384 implicit | 24576 implicit | 3/2 |

Both R2/R3 and97/512 input partitions cover all four signal/channel combinations.
Four extra runs compare eight-channel together/apart policies and stereo pitch
+7.00007 semitones. All **52 actual processes** exited0, drained exactly and
emitted no warnings in **6.344 seconds**, under a60-second experiment deadline,
10-second child deadline and512 MiB inherited address-space ceiling. Complete
source and rendered WAVs, typed maps, hashes, code and outcomes are retained.
There was no output padding/truncation repair, native audio or application change.

Twenty of24 partition pairs are byte-identical. Four differ, with maximum sample
difference **1.1679286999**. The two eight-channel together/apart pairs also differ
(maximum0.1275595501 for R2,0.0542767327 for R3). Vendor option names are therefore
not a phase-preserving-group guarantee. A future adapter must bind its canonical
input-block policy; the application's existing512-frame preparation policy is
separate from arbitrary vendor input partition invariance.

## Timing/phase diagnostics and unresolved quality

Analysis is explicitly **post-run exploratory measurement**, not a predeclared
Q-STRETCH listening/acceptance pass. A32-frame causal rectangular RMS detector
selects the first10% of maximum inside each annotated event window. Original
impulse bias is0 frames and attack bias5. Raw-bias-corrected and map-bias-corrected
timing interpretations are both retained. Thirty-four of656 output observations
start at the measurement boundary and are censored/unresolved;622 remain window-
resolved. The initial uncensored analysis remains retained alongside its corrected
boundary flags. Even resolved observations have maximum diagnostic error1020
frames. Detector shape, pre-echo and event ambiguity prevent treating that single
measurement as a universal audible-onset oracle or quantifying full-reference
equivalence.

Measured interchannel onset-offset changes reach225 frames across selected
observations, including nonzero pitch and censored estimates. Eighty-eight stereo
sustain phase observations use2048-frame Hann projections at440/730 Hz, normalize
polarity and retain theoretical raw delay plus measured source/leakage bias.
Maximum selected output/source relative-phase change is1.128936173 degrees
(R2 maximum0.130417768, R3 maximum1.128936173). Stereo tones do not qualify an
eight-microphone group, arbitrary music or the no-added-phase-drift contract.

These outputs establish actual multi-anchor behavior and retained counterexamples.
They do not yet justify a shipping warp mode, phase-preserving group label,
segmented pitch/formant qualification or full F/Q/C/N promotion.

## Retention and next implementation

[The capture](../tests/results/M8/2026-10-10-warp-map-planner/README.md) retains both
geometry transcripts, all complete candidate audio and both analysis versions.
The first archive exceeded its16 MiB cap before publishing a manifest; its
failure/source and first failed inspection are preserved. Exact-byte content
addressing now shares identical WAVs while resolving every source/render hash;
no audio is omitted or reconstructed. The final archive is10,064,797 bytes.
Standard-library retained inspection passes46,909 checks, independently verifying
exact map/WAVE/partition evidence; onset/phase analysis is retained and source-bound,
not rerun or promoted by that inspection.

Next evaluate a pinned alternative or an original explicitly anchored processing
strategy against the retained nonuniform event/group cases, with measurement
definitions fixed before the next run. Preserve exact planner geometry, source
anchors, output duration, explicit edge policy and canonical processing chunks.
Only supported acoustic cases then advance to a versioned production adapter,
one editable raw-relative marker, supervised review/Apply, semantic Undo/reopen
and shared playback/export. Full P008/P011/P012, all other milestones, project
import/equipment, native installation/recovery and all-Europe delivery remain
required. No VM, equalizer change or product binary upload occurred; the complete
DAW goal remains active.
