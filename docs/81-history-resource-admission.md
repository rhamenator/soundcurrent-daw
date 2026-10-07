# Configurable Undo resources

X006 checkpoint, 2026-10-07. Total project tracks have no fixed product or
license ceiling. This step makes Undo resources application-owned configuration
and publishes their declared usage. It does not qualify the complete aggregate
process memory envelope, sustained throughput or frozen-reference parity.

## Core policy and failure semantics

`HistoryBudget` controls retained payload bytes, operation workspace bytes and
retained command count. Defaults remain 32 MiB, 256 MiB and 256 commands; trusted
callers and the desktop can raise them. Project files cannot set these policies.
Undo data is control-thread state, not persistent project metadata or DSP state.
There is no project schema change or new dependency.

All payload additions and products use checked accounting. Dynamic identities,
names, routing ports, clips, EQ bands, object patches, order lists and master
matrices are charged by owned capacity. Each deque entry includes a declared
storage allowance. Cached per-entry charges stay conservative when a transfer
copies smaller-capacity vectors. Transfers preserve total retained charge.

Workspace checks include retained history, an active gesture, two candidate
charges and three times the larger canonical/proposed state payload-and-validation
charge. Begin reserves its future active/commit work and publishes its checked workspace
peak after success, as does update; the controller preflights
new parameters, routes, monitoring and structural/verified-take admission before
committing an unrelated gesture. Undo/Redo prepare and validate a candidate state,
then allocate the destination entry before moving canonical state and retiring
the source entry. Refusal preserves canonical values, both stacks and Cancel.

A new accepted command clears Redo and retires the oldest Undo entries when
necessary. Retirement is observable. Configuring a smaller budget refuses if
existing payload or command usage would exceed it; it never clears history.
An idle workspace reduction may admit the current retained/canonical footprint
while refusing a subsequent operation's larger workspace. Raising the limit
allows that operation to be retried without lost history.

The accounting bounds declared owned payload/work. It is **not** a measurement
of heap allocation, allocator/deque internal capacity or process RSS. Candidate
construction, validation and difference indices can allocate before the final
candidate charge is known. Existing state admission still applies. External
immutable snapshots, saved/IO copies, inspector projections, GUI indices, and
old/new prepared audio graphs require their own accounting and eventual combined
admission. No claim of pre-admission for every allocation is made.

## Desktop workflow

Edit → **Undo resources…** opens contextual, translatable controls for command
count, retained MiB and operation MiB. Locale-aware integer parsing checks
conversion and multiplication before submitting a bounded control-worker command.
Usage reports Undo/Redo counts, retained/active bytes, the historical peak of
accepted operations and retired commands. Limits take effect without dirtying or
revising the project, modifying media or touching the audio callback.

The initial controller snapshot is initialized synchronously from trusted options,
so opening the dialog before the worker's first publication preserves configured
limits, including byte-exact non-MiB-aligned values. Read-only edit preflights return
the declared charge; successful operations record it after acceptance, retaining
the pre-eviction workspace peak. Refusals leave existing resource counters intact.

Worker results use a separate request receipt. A rejected reduction leaves the
old policy and preferences intact and reports the reason in the dialog. Close is
disabled while applying so the owner can observe the result. Create/Open/Save or
attachment IO and an active gesture must finish before changing limits. Accepted
preferences are saved by the application through QSettings and loaded at startup.
A persistence failure is reported separately from an already accepted runtime
policy, and can be retried;
test windows supply their own callback and do not alter the owner's preferences.

See official [QSettings](https://doc.qt.io/qt-6/qsettings.html) and
[QLocale](https://doc.qt.io/qt-6/qlocale.html) documentation. The implementation
uses APIs available under the existing Qt >=6.4 dependency; no upgrade is needed.
Contextual strings and German numeric-input qualification do not constitute a
translated/reviewed German application or all-Europe language coverage.

## Acceptance and open gates

The core fixture uses 512 tracks and 400 retained structural commands, complete
Undo/Redo, exact stable identities and Save/reopen. Other cases cover reduced
byte/count policy refusal, candidate/workspace refusal, raising limits and retry,
active Cancel, oldest retirement, new-branch Redo clearing and checked overflow.
The real Qt fixture exercises menu/dialog input, locale parsing, preference
callback, 512-track selection, 300 Undo/Redo actions, rejected reduction/retry and
Save/Open. A worker fixture verifies refusal preserves an unrelated active gesture
and saved project. The final [review receipt](../tests/results/M2/2026-10-07-history-resource-review.json)
qualifies the startup and pre-eviction corrections and supersedes the initial
checkpoint's source qualification. Its originals retain both deterministic failures.
Results are recorded separately from these acceptance contracts.

Still required: full aggregate graph/snapshot/GUI/IO admission, memory/RSS and
sustained workload profiles, larger recording arms/adoption, remaining visualization,
Qt paging and native Windows desktop/runtime qualification. Copies and validation
occur outside audio callbacks, but large edits can take control-worker time.
No native audio tests or physical speaker changes are needed for this checkpoint.
