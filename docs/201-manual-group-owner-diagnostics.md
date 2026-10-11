# Original take-owner diagnostics and finalization evidence

2026-10-11. M2 progress; full frozen-reference F/Q/C/N, M2–M11, X004/X005,
installation, native audio and all-Europe localization remain incomplete.

## Original unresolved failure

Current-MAIN PR135 run38145862264 passed three required jobs but failed native
job114488944113. `manual-recording-control` timed out waiting for a recorded group
at caller808, take12/identity13, occupied1, status1, callback_failed0. File sizes
alone did not identify the condition that prevented delivery. Its original
all-four full logs/API/tested tree and19 reviewed blobs remain in
[Checkpoint200 failure evidence](../tests/results/M2/2026-10-11-render-context-current-main-failure/summary.json).
The earlier caller807 failure and interrupted-parent Incomplete-versus-Ready
recovery discrepancy remain unresolved. Subsequent greens do not establish a fix.

## Diagnostic ownership

`ManualRecordingRun::diagnoseLane(take,lane)` performs a bounded lookup on the
**serialized control owner**. Unknown, reclaimed/delivered takes and invalid lanes
return no record. The record carries the exact take/lane and canonical immutable
asset UUID, pending-start credit, ready/attempt/join/construction flags, worker
presence/completion/written extent, and original published take start/end/retired
extent/phase. The capture producer's EOF and status are read through their atomic
accessors. After bridge reclamation, phase and producer EOF become unavailable;
the method does not infer a fake EOF from worker completion.

Each acquire observation is individually valid; the record is not one atomic
transaction snapshot. It is not authority to release a take, join a worker, adopt
media or change recording behavior. It never reads the callback-only next-frame
cursor, disk-owned acquired slab/queue or mutable writer internals. The UUID uses
fixed36-byte storage. Valid queries perform no I/O, allocation/free or lock. Call
only from the existing serialized control owner, or after its join.

The fixture additionally records the **original existing disk/construction-only
writer observations**. One writer at a time publishes phase/written/committed
facts with a bounded atomic sequence check. Eight snapshot attempts are allowed;
busy/incoherent observations are explicitly marked. The record and its callbacks
outlive fixture teardown and worker joins. A failure formatter preserves the
original exception even if the output stream fails. Generated fixture file paths
now include the relative capture directory so the failed asset can be identified.

## Deterministic acceptance workflow

A real worker processes an original69-frame take, retires the producer and pauses
at the existing `BeforeAssetHashRead` disk boundary. No callback or production
pipeline is delayed by this hook outside the test. One thousand diagnostic queries
observe actual Retired/start31/end100/retired69, producer EOF, pending-start0,
attempted worker, not-yet-complete/joined worker and original JournalPublishEnd
observation. Group delivery remains unavailable while the disk worker is held.

After release, actual worker completion, verification and bridge reclamation
produce a ready immutable receipt with phase/producer EOF unavailable. Explicit
delivery then removes the diagnostic identity and preserves exact69-frame media
and both reliable punch receipts. Prepared, pending-start, retired, ready,
delivered, foreign/lane refusal and failed diagnostic-sink paths are covered.
This reproduces a known held-finalization condition for diagnostic validation;
it does **not** assert that this condition caused the original Windows timeout.

## Fixture race found and corrected

The first TSan run found a data race on the test's global `checks` counter. A
Unicode failure diagnostic increments it on the simulated audio owner while
control increments it. Both original reports/logs are retained. The counter now
uses an always-lock-free atomic unsigned. Recording behavior, deadlines,
conditions, assertions and Unicode-path failure coverage are unchanged. This is
a causal correction of **test accounting**, not an established correction of the
Windows take-group wait.

The initial test selector also omitted two intended suites; those original logs
and selector are retained. The final selector explicitly executes all six suites:
manual recording, manual punch, capture transport, recording recovery, writer
timing and combined monitor recording.

## Qualification

Frozen source `a143154f5d22371ba3f50e6a90d43299595d61bd`, tree
`f3aef770c26453cf396437668df6c7ebbc8cc767`, passed:

| Profile | Suites | Time | Observed manual-owner checks | Guarded allocation/free/lock |
| --- | ---: | ---: | ---: | ---: |
| Release | 6 | 1.67 s | 67,252 | 0 |
| ASan/UBSan | 6 | 8.22 s | 67,348 | 0 |
| TSan | 6 | 7.49 s | 67,259 | 0 |
| Independent Debug worktree | 1 | 1.59 s | recorded in full log | 0 |

Checks vary with owner polling; every profile includes1,000 held-finalization
queries and actual worker/producer/delivery boundaries. Maximum output difference
remains0 with floating-point headroom preserved. Final TSan reports no counter
race or other race in this cohort. Windows owner/fixture objects compile without
warnings; new Windows execution is pending.

[Qualification evidence](../tests/results/M2/2026-10-11-manual-group-owner-diagnostics/summary.json)
is a66,163-byte/23-member packet binding588 selected source/checkout inputs,
19 executable hashes, two Windows object hashes, full profile/Debug logs,
configurations, qualifier/retention scripts and the original counter-race failure.
No executable or media is included.

## Bounded Windows reproduction

The native CI job retains its original full CTest cohort and five-second fixture
acceptance deadline. An additional step runs the **same original concurrent
20-take workflow**, at most three times, with the new observations. It stops on
first failure. Each owned diagnostic process has a separate60-second outer
experiment limit and joined termination on overrun. That outer limit does not
increase the fixture's original five-second success condition. Both raw stdout
and stderr logs are preserved even if native stderr contains the failure.

A missing executable after an earlier failed build is explicitly unavailable;
that step's skip cannot qualify the diagnosis. Native qualification must inspect
the actual built/tested source and original logs, including all three completion
markers if they passed. No automatic retry-until-green or assertion weakening is
introduced. No local VM or physical audio was used.

## Preceding actual Windows startup execution

[Checkpoint200 Windows proof](../tests/results/M2/2026-10-11-monitor-startup-preroll-windows/summary.json)
retains actual MSVC114493056820/run38147238385:70 suites150.54s, new prepared
startup0.59s, with allfour original jobs successful. The52,171-byte/three-member
packet binds17 reviewed files/full native job/API/original merge
`d16d1f922e2432be417119a89e21b3348c3918b6`. It executes the **preceding prepared
startup**, not these new owner diagnostics, and qualifies neither a parent refresh
nor actual native SDK activation/installed audio. The original Windows root
failures remain open. Native startup still needs the explicit SDK warmup lease,
generation-ready handoff, durable disposition migration, native/installed checks,
short-take/Stop and professional latency/full EQ/mix/alignment gates.

## Next task

Inspect the new actual Windows failure, if reproduced. Bind it to the exact take,
asset, published phase/EOF, pending credit, worker completion and original writer
phase. Preserve original facts before changing production or fixture code. If the
bounded experiment passes, record limited non-reproduction and continue native
warmup integration; do not declare the intermittent root resolved.

See [ADR133](decisions/133-manual-group-owner-diagnostics.md).
