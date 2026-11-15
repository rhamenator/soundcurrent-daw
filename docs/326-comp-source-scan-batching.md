# Bounded comp source scans

Checkpoint 326 investigates the slow installed render observed in
[checkpoint 325](325-installed-windows-portable-comp.md). It changes source
validation block sizes in development source. Existing installed previews remain
unchanged, and a native Windows performance improvement is not yet established.

## Measurement

A read-only diagnostic uses the actual Windows-created assembly
`a1a283c7-c9e5-4d5f-bc41-0edadd727ee7` and its retained Linux/Windows history.
It loads the project and trusted selection, verifies each child's history and
completed span, then verifies the assembly with full historical checks. It does
not render new audio, adopt edits or use physical audio endpoints.

An initial Linux trace recorded **524,722 system calls**, including 339,874
`fstat` and 163,081 `pread64` calls. Much of the work is repeated source scanning
in the existing integrity checks. This measurement covers verification phases;
it does not measure the original Windows render's complete elapsed time.

## Change and invariants

The two source-scanning call sites now choose up to 8,192 frames per block, with
the decoded double-precision slab limited to **64 KiB**. Stereo scans use 4,096
frames. The WAV validator separately charges its raw slab and decoder workspace
to the existing resource ledger. High channel counts reduce the frame count.

These calls have no sample-block observer, so they do not need the smaller slab
used to validate output samples. Output observers and their grants are unchanged.
No persistent verification cache is introduced. Every original scan still checks
all samples for finiteness, computes the full file digest and verifies the pinned
file's identity around reads. Per-block cancellation, worker policies, deadlines,
history traversal, and checks before and after production remain in place.

The change affects serialized disk/control work only. It adds no work to the
real-time callback and makes no project-format or saved-protocol change.

## Observations

With the change, the same trace records **185,614 system calls**, including
113,802 `fstat` and 50,045 `pread64` calls: about **65% fewer system calls**.
Three alternating, untraced runs of each binary give median wall times of
**3.326 seconds before** and **3.022 seconds after**, about **9% lower** on this
Linux host. These are local observations, not a universal performance guarantee.

Both versions compare all **16,256 assembly samples** successfully. All **225
project files** remain byte-for-byte unchanged. The earlier independent exported
mix comparison remains in checkpoint 325.

All four focused suites pass in **54.17 seconds**:

- Actual media review: 4,894 checks, including changed output, retained-source
  and companion-file refusals, plus physical and resampled sample comparisons.
- Paired history review: three cases and 114 checks, including stale sources,
  cross-operation corruption, cancellation and bounded resource refusals.
- Owned span worker: 33 worker observations and six successful spans, with
  malformed, canceled and altered-artifact cases retained.
- Approved WAVE content: 20,956 checks covering pinned reads, independent
  decoded samples, headroom, bounds and mutation detection.

Evidence, diagnostic source, exact commands, separate stdout/stderr, phase times,
system-call summaries and test results are retained under
`tests/results/M2/2026-10-15-comp-source-scan-batching/`. Large generated test
corpora stay local; their inventories bind the retained logs to exact files.
The 801,718-byte archive contains 754 entries, SHA-256
`d829f6dbba25b14edcf2c46646a76ac5d5db3bd0fc7d2636f670a07ef34ac0f3`.
The inventory covers 1,218 generated test files. No application binaries are included.

## Remaining work

Measure this change in native Windows and repeat the installed workflow with a
qualified updated package. Do not close the 30-second responsiveness finding on
Linux timing evidence alone. Capture native subprocess stdout/stderr directly
to avoid the PowerShell truncation that obscured checkpoint 324's initial
protected-warp failure. That earlier failure remains unresolved.
