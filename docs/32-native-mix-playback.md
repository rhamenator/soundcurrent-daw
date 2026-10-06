# M2c2: native shared-clock mix playback and desktop selection

## Usable workflow

On Linux, open a multitrack project, select the track whose layout/output intent
will anchor playback, enable **Mix all tracks (matching channel layouts)**,
prepare, explicitly choose every output channel, then Play. All prepared tracks
run on one project cursor with their own EQ and timestamped read-ahead. The
inspector may select any track to edit its EQ or Undo without changing that mix.
Stop joins callbacks and the reader before retiring their objects. Preparation
and shutdown stay on the desktop worker.

The matching-layout option deliberately describes its admitted plan. It uses an
explicit unity identity matrix, refuses a differing layout, and never drops a
track or guesses a downmix. The API accepts an explicit `MixPlan`, including
mixed source layouts; the desktop matrix editor remains required. The shared
output route uses the selected preparation anchor's existing saved output
intent. The status names that anchor. Changing inspector selection never writes
routes onto another track or reconnects playback. This is a concrete transitional
routing workflow; it is **not** a separately persisted master bus/mixer model.
The checkbox/plan are preparation choices, not new saved session fields.

Selected-track playback remains available when the option is off. Recording
still prepares one track and is mutually exclusive with playback. Desktop export
still selects one track; multitrack WAV export is available through the existing
`render-mix` CLI and explicit API. Monitoring/room correction is not inserted into
these plans or default exports.

## Native and control ownership

`PipeWirePlayback` now owns `MixPlaybackRun` for both its legacy single-track and
explicit-plan constructors. All verification/prefill, matrix compilation and
allocation precede publication of inactive native ports. All outputs must be
chosen before activation. The existing filter checks port direction, node
serial/name and count; no system default, rate, quantum or hardware is changed.

`PlaybackBridge` admits either existing `PlaybackRun` or `MixPlaybackRun` through
a preparation-fixed pointer variant. Its callback dispatch is a bounded branch
with no virtual endpoint calls, allocations, locks or object retirement. Both
paths use the same native clock/rate/quantum/buffer validation and sticky terminal
publication. A mix observation retains its full report; its compatibility report
aggregates missing/stale **track-frame** counts and the mixed peak. The maximum
per-block missing sum fits uint32: 256 admitted tracks × 65,536 frames. Cumulative
counts remain uint64. Silent priming, immutable timing origin, output slack and
terminal silence retain their existing contracts.

The owner exposes generation-scoped lane events/receipts while stable model
addresses remain UUIDs. The desktop worker resolves each prepared lane by UUID,
checks clip/layout/processor/asset compatibility and prepares scalar deltas.
Track reorder does not retarget a lane; removed tracks or changed media/structure
require another preparation. Queue-full suffixes remain pending for retry.

A model bundle records its last admitted event revision **for each affected
lane**. Receipts update independent lane watermarks. The whole model revision
becomes applied only once every required lane has acknowledged its prefix.
Observing a higher revision on a later lane cannot falsely acknowledge an earlier
lane whose receipt is missing. New generations reset all watermarks. Admitted
and applied state remain separate, including completion concurrent with changes.

## Qualification and open gates

[Evidence](../tests/results/M2/2026-10-06-native-mix-playback.json) distinguishes
backend-free clock/fault and per-lane receipt tests, actual Qt controls using a
synthetic endpoint, and owned native device-graph runs. Native fixtures compare
captured audio against separate per-track EQ instances and an independent
float64/source-coordinate sum. The GUI native fixture edits a non-first lane,
Undo, meters, explicitly selected output and asynchronous close. These tests do
not qualify physical devices, native Windows, long-duration/deadline/load or full
frozen-reference parity.

One initial native GUI mix attempt timed out after acknowledged Undo while
waiting for completion/downstream delivery; its terminal state was not captured.
The cause is unknown. The fixture now wakes on a sink gap and reports terminal
phase/position/counts; the next isolated mix run passed. A rerun does not establish
a timing guarantee or resolve the earlier failure. The evidence retains both.

Next persist a dedicated master layout/matrix/output intent and expose the matrix
editor, then use one native playback/capture callback for armed multitrack
recording and overdub alignment. Punch/loop/takes/comping/fades, general
buses/sends/sidechains/PDC/VCA, graph crossfade/state migration and all later
milestones remain required. Independent M1 physical/native Windows/filesystem/
module-unload/deadline gates, X004 native imports, X005 monitor/print/portable
profiles/rights and all-Europe localization remain open.
