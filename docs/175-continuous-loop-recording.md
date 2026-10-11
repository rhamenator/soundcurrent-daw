# Continuous audio loop capture and retained source windows

This checkpoint supplies the framework-independent capture/retention foundation
for P005. Desktop automatic loop recording and native Windows duplex recording
remain open. Saved loop playback from checkpoints173/174 is a separate workflow.

## Capture ownership and clocks

`prepareLoopRecording` freezes project identity, sample rate, graph generation,
stream start, project loop and every armed track's input delay. The existing
`DuplexRecordingRun` plays wrapped backing while recording one continuous raw
file per input. Recording and native clocks never wrap. Finite requested passes
use checked stream arithmetic and latency postroll. Until-Stop mode uses the
representable clock limit, with no prepared-pass or command-slot ceiling.

No worker, pool or native owner is restarted at a wrap. Even a one-frame project
loop with input delays longer than a whole pass uses the same capture workers.
The existing callback checks, reader, DSP and bounded pools remain authoritative.
The planner, metadata queries and session edits run outside real-time callbacks.

## Original intent and recovery

After `startWriters` has created every raw job and before any audio producer is
activated, the control owner calls `persistLoopRecordingContext`. Each job gets
the same version1.0 `loop-context.json`: original loop/stream intent plus every
raw asset/track/layout/input-delay binding. Runtime pool sizes are not serialized.
Actual native origins remain in the original recording journals.

The codec rejects duplicate/unknown/missing fields, future versions, invalid
identities/layouts, boolean or overflowing integer fields, excess nesting and
payloads over128KiB. At most256 input channels are admitted. Publication uses
exclusive temporary files, file flush and no-overwrite publication; Linux also
flushes the containing directory. Windows retains the existing file-flush
durability classification. Exact repeated context publication is idempotent.
All arms are preflighted before the first publication. A later filesystem failure
can leave some copies published: there is no multi-directory atomic transaction.
Refuse activation, surface the error and preserve all original jobs in that case.

Inspection is metadata only. `validateLoopRecordingCheckpoint` binds a separately
verified original journal to its source context. Changing the current project
loop cannot reinterpret an old capture. Interrupted-prefix recovery preserves
original audio/context, but integrating recovered asset copies with loop windows,
discovery/recovery UI and portable project-copy sidecars is still required.

## Explicit retained views

`loopRecordingExtent` computes the common verified raw prefix across all arms;
missing inputs cannot become a complete group. Full and partial passes are
reported separately. Each requested window keeps its original raw clock origin
and addresses the full asset with a source-frame offset. No shortened recording
receipt or fabricated native origin is constructed.

`withLoopTakeLanes` stages a memory-budgeted page of inactive views. Each pass
shares one group identity across arms. Raw media is registered once and never
rewritten. Partial passes require an explicit retention policy. Existing grouped
comp edits, semantic Undo/Redo, save/reopen and WAV export then address these views.
There is no automatic comp or Save. Complete manual-group admission is unchanged.

## Acceptance and remaining work

The synthetic test drives actual capture writers and the duplex callback with
17-frame native packets, two inputs delayed7/29 frames and13 recorded passes for
loop periods1/2/7/53 frames. It compares every backing/raw/export sample, checks
original journal origins, retains pages of5+8 passes, comps pass10 and verifies
Undo/Redo/save/reopen and unchanged media. Until-Stop, explicit partial retention,
interrupted verified-prefix recovery,256-input codec bounds, malformed/conflicting
context and late-publication refusals are also exercised. Callback allocation,
free and blocking-lock counts must be zero.

These are synthetic core workflows, not native endpoint or installed application
qualification. Windows MSVC CI builds/runs the test independently; only a finished
current-source CI observation can establish its result. No physical endpoint or
VM is needed for this gate. No dependency/license changes or release uploads.

Next: integrate prepared intent and its publication barrier into the desktop
recording owner, offer finite-pass/until-Stop recording and paged complete/partial
take retention with verified journal admission and structural history guards.
Then qualify real owned Linux duplex and add the missing native Windows duplex
owner. Take audition/crossfades, musical automation/MIDI loops and full P005/M2,
processing-quality, content, native-project and localization parity remain open.
