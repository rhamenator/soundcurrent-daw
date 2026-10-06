# Latency-aware prepared punch locators

Date: 2026-10-06. P004 / M2. Engine preparation and synthetic workflows;
full desktop/native punch recording remains incomplete.

## Timing contract

`DuplexRecordingOptions::musicalPunch` accepts half-open **project sample-frame**
locators. Beat/tempo conversion belongs upstream and is not implemented by this
option. The existing `punch` option remains an explicit shared raw-frame range;
selecting both is rejected. The control-only `prepareMusicalPunch` returns stable
track identities, declared input latencies, per-track raw windows and the required
playback end. It creates no capture pools, recording jobs or disk operations.

For desired timeline `[B,E)` and a track's declared input latency `L`:

- Capture the raw window `[B+L,E+L)` with its pipe starting at `B+L`.
- Preserve all `E-B` raw frames in the asset.
- Existing attachment subtracts `L`, placing the clip at `B` with source frame0.
- Playback must reach at least `max(E+L)` across the armed tracks.

For example:

| Track | Timeline window | Declared latency | Raw window |
| --- | --- | --- | --- |
| Mono | `[503,1291)` |13 frames | `[516,1304)` |
| Stereo | `[503,1291)` |300 frames | `[803,1591)` |

If requested playback ends at1291, preparation extends it to1591. Both resulting
788-frame clips start at503. `playbackEnd()` and `captureRange(index)` expose the
actual immutable prepared bounds to future controls; caller options/specs remain
unchanged. Admission rejects musical locators outside the original playback
range, invalid/duplicate identities, rates, negative/excessive latency and signed
frame overflow. The existing input-latency cap is60seconds, which also bounds
added postroll. Project/track/layout/input/payload admission still runs for every
lane before capture pools or jobs are created.

## Callback and object ownership

The bridge accepts separately prepared raw windows on `ArmedCapture`. Shared
bridge punch and explicit per-lane windows cannot be mixed. Each window must lie
inside playback and match its fresh pipe's start. The immutable lane state has
prepared pointer storage and bounded audio-owner intersection scratch fields.

Every callback validates native continuity and buffers, computes per-lane capture
intersections, and checks all newly starting lanes' timestamp offsets before
publishing any origin or raw sample. A later lane's overflowing timestamp rejects
the entire callback, even if another lane's timestamp would fit. Timestamp0 stays
unknown. Each pipe publishes the device position and integer timestamp of its
own first recorded sample. The bridge's aggregate `timingOrigin()` reports the
earliest captured sample across lanes, independent of binding order; it must not
be substituted for each lane's origin. Diagnostic driver delay is not alignment
compensation.

All raw copies precede aliased EQ/matrix output. Explicit Off/Post-EQ monitoring
and existing file playback still use the complete callback block, through preroll
and postroll. Each bounded capture pipe finishes at its own punch-out, so an early
track can complete while a delayed track has not started. Normal unwindowed
recording retains its previous run-completion end-reason policy. Completed pipes
remain alive until callback join; a later fault does not overwrite their completed
end reasons. Existing join/result, checkpoint/recovery, float headroom and journal
schema contracts remain.

No GUI, disk I/O, allocation/free, blocking lock, logging or new queue is added
to the callback. Preparation and retirement remain outside the audio owner.

## Acceptance scope

The expanded [punch fixture](../tests/punch_tests.cpp) supplies independently delayed
mono/stereo input signals. Exact recovered audio must match the **desired timeline**
signal, rather than a sample expectation copied from the prepared raw range.
It tests differing latency/order, one-sample and edge windows, five callback
partitions, separate origins, delayed capture in postroll, full-block aliased
monitoring, per-lane completion, nonfinite accounting, raw journals/media hashes,
aligned attachment, grouped undo/redo and Save/reopen. A real disk-worker owner
checks automatic postroll and joined receipts without mutating caller state.

Interruptions before both lanes, between their windows, inside the delayed lane
and after both windows retain exact independently recoverable prefixes. Recovery
keeps original audio/journal hashes, each origin and declared latency. Unstarted
lanes have no fabricated origin or recoverable audio. Limits cover INT64_MAX,
the60-second cap, duplicate/empty/excessive arm sets, conflicting ranges and
unknown/overflowing timestamps.

Build/test results and exact source/artifact hashes belong in
[the checkpoint receipt](../tests/results/M2/2026-10-06-latency-aware-punch.json).
Synthetic input latency is declared and deliberately known; it is not measured
physical roundtrip or native backend latency qualification.

Linux Debug29/29 groups pass in27.93s; serial ASan/UBSan/LSan29/29 pass in75.36s.
The optimized fixture passes11,079,850 checks,25 original raw-boundary workflows
and28 latency-aware workflows, with zero audited RT violations. Debug, sanitizer,
optimized and Windows headless core/test builds compile/link without warnings.
Windows execution, Qt workflows, native devices and installers remain unqualified.

After all owned build/test handles terminate, the unchanged plain owned-route
20-second native default-recording regression passes30,720,000raw/1,920,000output
samples exactly, origin/journal/hash/alignment/Save-reopen and existing finite
timing gates. Owner maximum3.129286ms wall/3.125028ms CPU, no observed composed
cycle overruns, maximum capture queue1, all33 worker phase-pair records complete.
Defaults remain unchanged and owned routes retire; zero pre-existing links means
populated prior-link preservation is not exercised. All64 launch-source and actual
executable/library pins are verified after exact95926 terminal exit0. This run
selects no punch and establishes no native punch, physical, sustained or Windows
qualification. The prior successful native executable is frozen before rebuilding;
all21 historical observations and original failure artifacts remain retained.

## Remaining full workflow

Persist desired locators as versioned project state, provide desktop controls,
coordinate record transport and expose the admitted postroll. Add native punch
workflows with per-lane origin checks, Windows runtime qualification, calibrated
backend latency, manual punch/Auto monitoring, configurable stop/continue modes,
loop segmentation, take lanes, comping and overlapping-take audition ownership.
Tempo-map conversion and beat-relative locator behavior need their own evidence.

All92 frozen acceptance/quality/reference/F/Q/C/N contracts remain unpromoted.
All21 historical observations remain independently retained; this change resolves
no sustained native cause. Physical/load/storage/performance, Windows, imports,
equipment profiles and all-Europe delivery remain required. No new dependency,
equalizer write, signing expense or publication is introduced.
