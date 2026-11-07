# Project-owned comp assembly recovery

Checkpoint 281, 2026-10-14. Full frozen DAW scope remains active.

## State and file ownership

`CompAssemblyReference` binds a new operation and two optional output IDs to an
existing paired selection and the canonical request's SHA-256. Conditional schema
1.22 uses strict fields/types and the existing bounded parser, project payload
and Undo budgets. Projects without these references continue to use the previous
formats. Missing recovery files do not prevent opening the rest of a project.

`AddCompAssembly` and `RemoveCompAssembly` support atomic history. Re-registering
the same value is a no-op; changed digests/IDs conflict. A referenced parent cannot
be removed alone. Removing a child and its parent in one edit is supported, with
one Undo restoring both. Reservations participate in the existing global UUID
checks; retained take group IDs are checked separately. Removal preserves files.

`persistCompAssemblyRequest` validates the proposed registration before disk
publication, uses an exclusive cooperative writer lease, flushes immutable request
bytes and publishes without replacement. The canonical owner then registers and
saves the returned reference. `loadReferencedCompAssemblyRequest` validates the
saved model, selects its independent expected digest, reads through an approved
media root, rederives the request and compares operation/output reservations.
Completion cannot authorize a changed request. Publication and model save remain
separate transactions; unreferenced orphan sidecars never become trusted jobs.

## Acceptance

Source-bound commit `a41d260352947cf81bc1910b4a3c650c2dc921ae` passes five
Linux suites in 31.17 seconds: new assembly project state, existing paired state,
owned assembly publication, session state and history resources. All selected
source/library/executable hashes remain unchanged before and after execution.
[Acceptance index](../tests/results/M2/2026-10-14-comp-assembly-state/index.json)
binds the 15,694,400-byte, 608-entry archive to the original process receipts,
project generations, request sidecars, complete/partial files and logs.

The state corpus tests 13 occupied identities in operation and both output slots,
strict field/type/count/digest checks, old schema preservation, 128 saved jobs,
quota refusal, Undo/Redo/conflicts, parent/child atomic removal, canceled saves,
previous generations and Unicode-path relocation. Missing sidecars leave the
project readable while refusing recovery. Three actual publication fixtures
persist/register/save/reopen requests, refuse changed digests/reservations/files,
then copy each project and recover the complete pair under fresh context.

Twelve real nominal/span workers, three publication fixtures and one Linux image
replacement probe retire successfully. Three original jobs complete, six retain
injected interruptions and three retain cancellation. All 202,716 complete output
samples match recomputation; deliberately repaired hashes do not hide the changed
within-blend sample. These are controlled exception/cancellation fixtures, not
power-loss or physical audio tests. No canonical audio adoption occurs.

The initial build failure (missing `<array>`) and initial state fixture failure
(missing referenced audio) are retained in
[development logs](../tests/results/M2/2026-10-14-comp-assembly-state/development-logs.zip).
The failed test run still passed the other four suites. The corrected fixture
writes a real silent PCM16 WAV with its actual hash; media validation was preserved.
The source tree and corrected test were rebuilt before the final run. Rebuilding
unchanged mixer units also emitted existing-code diagnostics for optional resource
initialization and an unused local; this checkpoint does not claim a warning-free
whole-project build. Final fixture compilation emits no warning.

## Original prior CI

Original run [38621212838](https://github.com/rhamenator/soundcurrent-daw/actions/runs/38621212838)
at `08145896d5cc64d89ab158d7cc93796684f95c67` passed all four jobs: Linux 181 tests,
Windows native 12 comp tests plus 93 existing tests, Windows desktop 23 tests,
and Windows cross-build. Original terminal APIs, full logs, all five artifact ZIPs
and PNGs are retained locally before advancing PR 194. Public
[review evidence](../tests/results/M2/2026-10-14-comp-assembly-state/pr194-nominal-ci/index.json)
retains original reports, markers, process receipts and logs.

Both original Linux and native Windows assembly workflows reaped 12 nominal/span
workers and verified six nominal WAV hashes. Each compared 202,620 unchanged
samples and 96 blend samples against the curve oracle. The independent bundle
audit checked 14 payloads, 1,616 source files against Windows checkout filters,
all collection hashes and two media builds against the reviewed pin. This run
predates checkpoints 280/281 and does not qualify their Windows execution.

## Remaining work

Connect queued desktop assembly publication/recovery and asynchronous close to
the existing resource ledger and fresh model/root/epoch guards. Implement explicit
project-rate overlays shared by live/offline playback, versioned state and paired
atomic adoption/Undo; preserve raw takes and original nominal anchors. Do not
reuse the older same-rate handle window for post-SRC output. Nonuniform/protected
span production, the three spectral-spur findings, mixed-join quality, broad
layouts/channels, native installed workflows and the full M2–M11/F/Q/C/N/X004/X005
and European localization gates remain open. See [ADR 169](decisions/169-project-owned-assembly-requests.md).
