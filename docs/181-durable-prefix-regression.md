# Interrupted loop capture: wait for disk publication

The refreshed current-MAIN clock-core cohort failed Windows native-unit
job114395475698 in run38114113389. The clock test passed; continuous-loop-recording
failed with `Interrupted test did not retain a verified prefix`. The complete
failed job log and its API status are preserved in the evidence below.

The fixture fed347 callback frames and immediately canceled both disk workers.
Callback progress does not guarantee that a disk owner has drained a256-frame
slab, flushed audio and published its journal. Cancellation intentionally preserves
the published prefix and need not drain queued audio. Changing that production
policy would conceal the fixture's missing precondition.

## Regression contract

The original interruption case now waits for both workers' release-published
written-frame counts to reach256. Each count is stored after `drainOne` returns;
this fixture's64-frame checkpoint interval guarantees journal publication before
that store. It is not a general promise that every written count is committed.
The five-second deadline fails with a disk-readiness diagnostic. No active journal
is concurrently inspected and no wait is added to an audio callback.

A second case deliberately holds both disk owners at their first audio-write
boundary until callback pumping is complete. It checks that neither has written,
releases both, waits for the durable prefix, and then cancels. Gate waits run only
on fixture disk owners and are independently bounded so failure cannot hang joins.
Both cases require non-finalized256-or-more-frame verified prefixes, original loop
intent despite subsequent project edits, recovery lineage/extent, and unchanged
original audio hashes/context. The existing callback allocation/free/lock audit
remains enforced. Production capture, writer, cancellation and recovery code is
unchanged.

## Evidence and scope

Frozen source `492673dd78e2211d7128e19bac166b6102e5d360` passed the Linux
Debug continuous-loop suite:5,034 checks, two deliberately delayed disk owners,
and zero callback audit counts. The exact CTest duration is retained in the log.
The [41,237-byte packet](../tests/results/M2/2026-10-11-durable-loop-prefix/summary.json)
contains five members: Linux build/CTest logs, failed Windows job log/status,
and nine selected source hashes plus the local executable hash. Selected inputs
are not a complete dependency/toolchain closure. Standalone compressed-byte and
member-hash validation passed. No executable or sample media is included.

This local regression does not qualify repaired Windows execution. PR119 requires
a fresh cohort on its new head against current MAIN, with all four protected
checks succeeding before ordinary merge. Prior frozen clock/bank/native evidence
retains its original source and scope. Native Windows duplex, installed and physical
workflows, M2 and the full frozen functional/quality/content/compatibility target
remain open.

Next implementation remains the causal monitor buffer and drift-resampling owner,
separate from original raw capture and durable clock/project mapping.
