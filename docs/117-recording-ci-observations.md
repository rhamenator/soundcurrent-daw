# Synthetic recording CI: bounded writer observations

Date: 2026-10-09 UTC. Frozen baseline SC-DAW-BASELINE-2026-10-05 unchanged.

An actual hosted Windows recording-recovery failure at `939850c` reports queue
exhaustion and a finalized392192-frame prefix instead of its480000-frame target.
The original source used nominal127/48000-second pacing; the earlier26x workload
correction was already present. The failed ZIP/metadata are retained in
[checkpoint116](116-inspection-bundles.md). No writer phase timing was available
in that failing run, so its cause cannot be assigned to a disk flush, scheduler,
virus scanner or hardware from this evidence.

The concurrent synthetic fixture now connects the existing fixed-storage
`WriterTiming` observer to the production disk worker. Construction/disk owners
record steady-clock wall intervals for write/hash, audio flush, journal
publication and idle waits, with maximum queue occupancy and committed/written
cursors. Construction events without an attached producer are excluded.
Aggregates and overall producer duration print only after the worker joins.
No observer or clock/log call is added to the audio callback. A check ensures
the observer never runs within an audited audio region.

Pool dimensions, sample-rate pacing, checkpoint cadence, rejection handling,
exact480000-frame/raw-sample/journal/project/recovery and zero-RT-resource
assertions remain intact. No retry loop is added to the test. Wall intervals
include scheduling and are not filesystem service-time measurements. A passing
rerun cannot identify or erase the original failure.

The [local receipt](../tests/results/repository/2026-10-09-recording-ci-observation/local-receipt.json)
records exact source/header/binary hashes and separate actual test commands.
Linux Release recording-recovery passes277 checks and records480000 frames,
zero audited allocation/free/lock operations, one maximum ready slab and complete
phase pairs. Its producer takes10.422044995 seconds on this host. The separate
existing phase-pairing/occupancy helper test also passes. The first combined
selector matched recording-recovery only; the helper was run separately with
its correct name, and both logs are preserved. New Windows execution is pending.

The one unchanged native rerun passes all eleven selected tests, including255
Windows recording checks and the required480000-frame take. Its separate receipt
is retained; it does not identify the original failure cause. PR59 merged through
all required checks. The new instrumented fixture is a separate source change
with independent native execution still pending.

No dependency, schema, production recording policy, installer or parity status
changes. No local VM was started for this work. When another testing VM became
active, this chat's existing disk-reading comparator was paused until all VMs are
off; the other VM was left unchanged. Native Windows capture/Stop and sustained
physical recording gates remain open independently of this synthetic fixture.

Next: qualify the instrumented fixture on native Windows. If a failure recurs,
use its actual phase/backlog
measurements to select a concrete next experiment before changing policy.
Continue the exact-writer import corpus and semantic-IR task from checkpoint116.
