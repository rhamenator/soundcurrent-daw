# ADR 147: Observe failures before cleanup and retain their actual owned files

Status: accepted, 2026-10-13. Qualification: [Checkpoint 248](../248-native-controller-wait-evidence.md).

## Decision

Keep existing five-second controller conditions and60-second manual aggregate.
At controller expiry report the operation and one immutable snapshot before unwind.
Bound fake-SDK tuple reads with a single-writer sequence; label independent worker
and timing observations separately. Never invent native-time coherence.

Scan CTest output for standard owned-project failure markers, including the native
controller namespace and Windows8.3 temp-parent spelling. Preserve bounded project
counts/entries/bytes, reparse refusal, no overwrite and exact per-file hash checks.
Capture post-owner cleanup files and retain the earlier observation separately;
do not describe those files as a pre-cleanup or atomic project snapshot.

Give newly added reserve cases their own test envelope. Retain all their capacity,
exact-prefix and callback checks and the original concurrency workflow. Increasing
an existing deadline is not a diagnosis of the retained Windows failures.

## Limits

New-source Windows aliases/retention remain independently unqualified until CI
runs them. A preceding green run does not recover missing historical bytes or
prove an intermittent timing failure cannot recur. No product schema, dependency,
GUI control, real-time callback behavior or physical audio claim changes here.
