# Short uniform comp spans: backend feasibility

Date: 2026-10-15. Observed source: `9fbb99f`.

## Why this remains a product gap

A repeated uniform comp edit can request only 4,054 processing frames after a
speed change. The existing producer correctly refuses when that region is smaller
than the prepared backend analysis window. A useful editor should be able to use
available surrounding original audio for such an edit. Enlarging a test fixture
only demonstrates the existing supported case; it does not deliver that feature.

## Bounded experiment

[Probe source](../research/comp-uniform-context/window-probe.cpp),
[observations](../research/comp-uniform-context/window-probe.json) and
[commands and hashes](../research/comp-uniform-context/receipt.json) are retained.
The probe uses the repository's already-built vendored R3 library on Linux, with
one low-priority process, synthetic mono PCM and no physical audio.

The 36 combinations cover 44.1/48/96 kHz, time ratios 0.5/1/1.5 and pitch factors
0.5/1/1.5/2. Every prepared minimum exceeded 4,054 frames: observed windows were
4,096 or 8,192 frames. In particular, 48 kHz with pitch factor 1.5 required 4,096.
These observations are a sample of supported configurations, not a global maximum.

Rendering 16,384 actual source frames completed with finite PCM and the exact
expected output duration in every combination. There was no artificial silence,
minimum-window waiver, or change to the production producer. This establishes
backend feasibility of longer real context, not a finished context-selection policy.

The same 512-frame input schedule produced byte-identical output on a second run
in all 36 cases. Changing the maximum/input block size to 257 produced different
PCM in 13 cases; the largest absolute difference was 2.548022508621216 on a sine
with input peak 2.5. These are raw full-output comparisons, including boundaries;
no audibility, alignment or processing-quality conclusion follows from that number.
The experiment supports retaining an explicit, fixed 512-frame backend schedule in
the versioned producer contract. Output duration alone does not prove equivalence.

## Next implementation slice

Add a separately versioned uniform-context plan and renderer alongside the current
producer. It must choose and bind actual original source context before worker
publication, preserve the requested candidate's exact rational clock and pitch
history, render on a fixed schedule, and emit only the requested candidate frames.
The requested edit's size must not set the backend's whole analysis domain.

The plan must declare its full raw interval and processing/output grids, the exact
candidate crop/sampling map, original settings, baked pitch and sample clocks.
Validate uniformity at every retained map knot and prove candidate coordinates
against the full plan. Do not round fractional origins to create context. Determine
whether the original nominal domain is sufficient; extend only into available raw
media using a declared affine mapping. Refuse when real context cannot satisfy the
chosen strategy, rather than padding or changing algorithms after failure.

The renderer remains outside the real-time callback in an owned worker. Keep
bounded read/write slabs, resource leases, process limits, cancellation, explicit
completion and discard-on-failure publication. Full-context DSP can stream through
a bounded crop buffer; it must not require an allocation proportional to a long
original take. Verify full rendering and candidate extraction before integration
with paired adoption and the desktop workflow.

Use the actual previously failing short repeated-edit geometry for acceptance,
with fractional and source-edge cases. Preserve legacy producer identities and
outputs. Establish pitch/timing, outside-edit preservation and save/reopen behavior
independently of mere backend completion. This probe does not yet qualify any of
those application-level behaviors, Windows execution, stereo/multichannel behavior
or processing-quality parity.

## Concurrent qualification

Original CI [38722734318](https://github.com/rhamenator/soundcurrent-daw/actions/runs/38722734318)
was confirmed running at `9fbb99f` while this experiment ran. Its result must be
retained and audited before advancing PR 194. The experiment changes no production
source, installed application, audio route, VM or equalizer repository.
