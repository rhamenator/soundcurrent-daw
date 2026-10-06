# Manual punch during continuous engine playback

Date: 2026-10-06. P004 / M2 engine workflow; desktop and native adapters remain open.

## Prepared ownership and commands

`ManualPunchBridge` retains the existing `MixPlaybackRun` and its EQ histories
across takes. Control prepares immutable arm mappings, declared input delays,
monitor policies and input/output scratch. Up to eight outstanding capture slots
are reserved globally with the run and bridge payload before pool allocation.
Each take has a monotonically assigned identity and fresh deferred capture pipes.
Slot count bounds outstanding work; joined retired slots can be released and
replenished while playback continues. It is not a limit of eight total recordings.

One serialized control owner submits at most 64 outstanding commands. Each
accepted command reserves a reply credit until control consumes its receipt.
Queue/reply pressure refuses submission before audio state changes; replies are
not lossy telemetry. Scheduled commands execute in FIFO order. An immediate
`frame=-1` command uses the next admitted audio boundary in that order; an old
explicit frame behind a future command receives `Late`, not reordered execution.
A higher-level scheduler/cancellation/quantization policy and desktop controls
remain required. Revision, prepared generation, take identity, requested and
applied frames and explicit rejection result travel in each receipt.

Audio checks generation, timeline bounds, slot state and every lane's raw-start
arithmetic before a start. Accepted logical B publishes raw B+L separately on
each prepared pipe. Control/disk can construct normal recording workers from
these resolved snapshots while audio fills the touched pool. Device origin is
published at the actual first raw sample, potentially in a later callback.
Stop, invalid clocks and transport faults reliably reject pending commands.
Whole-block device and monotonic timestamp overflow is refused before admission.

Punch-out at logical E changes Auto monitoring to file input at E. Each raw lane
continues to E+L. Closing takes can overlap a new recording's delayed raw window.
All raw publication precedes output writes, including aliased views. Processing
splits at admitted command boundaries while preserving the same mix graph and
EQ state; Post-EQ input stays live, Off uses files, and Auto follows the logical
recording window. Underlying file samples are consumed throughout.

The finite prepared playback end reserves maximum declared input delay. The
latest logical punch-out is end minus that delay; starts at/after that limit are
refused. An open take closes at the limit and completes raw postroll by the
prepared end. This is a finite-generation contract; indefinite transport,
loop/seek and broader recording scheduling remain required.

## Retirement and interruption

A take moves Prepared → Recording → Postroll → Retired. Earlier finished lanes
remain finished while later lanes complete postroll. Audio removes the last slot
reference before release-publishing Retired and never dereferences it afterward.
Control must consume outstanding start receipts, join every disk consumer, verify
results and perform grouped admission before reclaiming/replenishing a slot.
Construction, worker creation/join, project edits, deletion and logging stay off
audio. The bridge/run and all consumers must be stopped/joined before destruction.
The API's documented disk-join requirement remains a caller obligation; the next
production control owner must enforce it automatically.

Device, rate, quantum, clock, buffer, reader/processor and capture failures finish
active pipes and stop the run with the initiating reason. Per-lane durable raw
prefixes can differ and are never padded to look like full takes. A completed
retired take's subsequent disk error belongs to its disk/result owner; automatic
production fault propagation and desktop discovery still need integration.

## Acceptance scope

Tests compare three repeated-take workflows at 256/127/31-frame partitions against
an independent continuously running nonflat/flat mix oracle, including two punch
windows in one callback, input/output aliasing, 0/41/200/4097-frame input delays,
a scheduled EQ change, late disk startup and completed captures before worker
creation. They inspect every raw sample, exact device origin, journal identity and
alignment, grouped Undo/Redo, preserved saved original and Save/reopen. Twenty
additional takes are joined, reclaimed and replenished from control while a
separate audio thread keeps running. Instrumented callback allocations, frees and
blocking locks must remain zero on both test threads.

Other cases cover exact callback/prepared-end command receipts with zero and
41-frame postroll, a stopped delayed take with no captured sample or fabricated
origin (empty recovery is refused), 64 reliable replies/full refusal, stale generations, late and
duplicate/immediate commands, release before retirement, finite slot/aggregate
memory limits, timestamp/int64 overflow refusal and natural delayed postroll.
Nine interruption variants recover independently exact raw prefixes under Stop,
device loss, changed rate, excessive quantum, discontinuity, missing buffer,
writer failure, xrun and queue exhaustion. Recovery preserves original media and
saved project state. These are synthetic engine/disk workflows, not physical,
native timing, Windows runtime or desktop qualification.

The first oracle failure and the subsequent journal-fixture failure are retained
as observations 26 and 27. The original combined failures did not log individual
terms; debugger replays and source inspection are recorded separately. The first
revealed a mixed finished/postroll retirement check; the second compared runtime
pool settings with recovery settings that journals intentionally do not persist.
Earlier 25 observations and sustained deadline failures remain intact.

[Dated receipt](../tests/results/M2/2026-10-06-manual-punch-engine-owner.json).
Final Debug and ASan/UBSan/LSan suites pass 30/30 (27.74 s and 74.65 s).
The Windows media target, including this owner and its tests, cross-compiles;
it is not executed on Windows. The synthetic output oracle difference is zero
and float output peak is 2.14813, with zero instrumented callback allocate/free/
blocking-lock hits. These functional runs overlap compilation and are not native
performance evidence. No new dependency or
project/journal schema change is introduced. All 92 frozen contracts remain
unpromoted. Native adapters, production off-audio disk/group owner, desktop
controls, broad monitor/loop/take/comp workflows, independent Windows, imports,
profiles and all-Europe translation/review/UI qualification remain open.
