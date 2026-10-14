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

Qualification results are recorded alongside the source-bound archive after the
current build and tests complete. The corpus covers strict schema round trips,
global identity collisions, atomic Undo/Redo, missing or changed sidecars, original
project generations, canceled saves, and copied-project recovery with fresh owner
context. The final publication corpus retains actual nominal/span workers and
complete output recomputation. These are controlled fixtures, not power-loss or
physical audio tests.

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
