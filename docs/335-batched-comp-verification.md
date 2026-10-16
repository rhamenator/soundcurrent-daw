# Batched comp verification with measured work reduction

Checkpoint 335 changes complete assembly recomputation to verify the paired
sources once before and once after both selected outputs. It targets repeated
work identified in the [Windows diagnosis](334-windows-render-diagnostic.md).
Installed previews are unchanged; Windows improvement is not yet established.

## Profiling and decision

A read-only Linux diagnostic verifies the retained Windows-created assembly
`a1a283c7-c9e5-4d5f-bc41-0edadd727ee7`, including original processing history and
all 16,256 assembly samples. The unchanged baseline library sources are bound to
the earlier checkpoint-326 input inventory. No project files or audio endpoints
are modified.

A 199 Hz CPU profile collects 610 samples with none lost. Approximately **59.21%**
of sampled CPU weight is in the crypto library. A separate linker-wrapped
SHA-256 update counter records **729,204,805 bytes** hashed across 22,593 calls.
The retained project itself totals 12,204,541 bytes. Counter tables are fixed-size;
the wrapper delegates every original call to the real crypto function. It does
not replace hash computation or alter digest results. Raw profiling data stays
local; reports, commands and source are retained.

The initial profiling invocation used a relative project path and was refused by
the existing absolute-root requirement. That failure is retained separately. The
successful diagnostic uses the explicit absolute path. An initial build invocation
also named a nonexistent target; the corrected target list builds successfully.

## Implementation and integrity boundaries

[ADR 203](decisions/203-batched-comp-recomputation.md) defines the batch contract.
Both candidates and original nominal renders are fully checked before the first
callback and after the last. Every selected output is admitted before callbacks.
Reports are withheld until the whole batch passes. The existing single-fragment
API retains its before/after behavior through a one-sided batch.

Complete output verification keeps both admitted files pinned across the batch,
checks their identity afterward, and uses one decoded output cache at a time.
It still validates each WAV's complete hash, sample hash, geometry, peak and
encoding, recomputes and compares every sample bit for bit, and runs the separate
outside-edit media review. Source/history checks surrounding publication and
verification remain. There is no persistent digest cache or project-format change.

All work remains off audio and GUI threads. Slabs, pinned handles and reports use
bounded owner resources. A single cooperative deadline covers the batch without
increasing the configured value. Cancellation, stale ownership and late corruption
must prevent successful return even if tentative samples were emitted.

## Scoped Linux results

Four serial low-priority suites pass in **82.35 seconds**:

- Verified pair assembly, including new exact batch/single-stream comparisons,
  one-sided results, empty/under-budget admission refusals, cancellation before
  sibling output, late marker mutation and resource-credit checks.
- Owned assembly publication and existing altered-output/failure cases.
- Protected assembly publication and nominal/source proof checks.
- Paired history review, including its three cases and 114 checks.

Three alternating, uninstrumented runs of each diagnostic binary compare the same
16,256 samples. All 225 input project files match the earlier hash-pinned transport
archive before and after measurement.

| Observation | Before | Batched |
|---|---:|---:|
| Median complete diagnostic time | 2.9759 s | 1.8709 s |
| SHA-256 update calls, separate instrumented run | 22,593 | 12,817 |
| Bytes passed to SHA-256 updates | 729,204,805 | 425,881,047 |

The observed median is about **37% lower**, with about **42% fewer hashed bytes**.
The timing runs exclude counter instrumentation; the hash counts are from separate
runs. This supports a local verification-work reduction. It does not establish a
Windows whole-render speedup or close the installed responsiveness finding.

## Evidence and next task

The evidence directory is `tests/results/M2/2026-10-15-batched-comp-verification/`.
Its manifest binds the archive, source patch, diagnostic binaries, original input
files, generated test-file inventory and scoped test/profile logs. Large generated
media and binaries remain local. The 1,797-entry archive is 1,744,811 bytes with
SHA-256 `2e508c7fd49412d1424481afdb81ba817b98d38856f343c55689d661e8ecff36`.
CRC and every payload hash pass; the generated-file inventory covers 2,542 files.

Next qualify the change with native Windows binaries, preserving the exact fixture
and original failures, then refresh installed candidates only after the relevant
gates pass. Hosted CI at `dfa5887` predates this implementation and cannot qualify
it. Preserve that run's original terminal evidence before advancing the draft PR.
Full DAW, quality, hardware, localization and earlier Windows findings remain open.
