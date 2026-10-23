# Native Windows qualification of batched comp verification

Checkpoint 336 qualifies the [batched recomputation change](335-batched-comp-verification.md)
at `8bcc163aebd740f453b957cb007dec1224aac71c` on the independent Windows
development clone. The installed Linux and Windows previews remain unchanged.

## Fixed-history comparison

The read-only diagnostic uses the same Windows-created assembly and Unicode
project path as checkpoint 327. Its baseline executable is that checkpoint's
hash-pinned **after source-scan batching** binary. The new binary adds paired
recomputation batching. The current source inventory is checked against host
bytes; the existing compiler, dependency DLL and build cache are reused.

Three alternating uninstrumented runs of each binary verify all **16,256 output
samples** and the complete retained history. Native medians are **4.116 seconds
before** and **2.684 seconds after**, approximately **35% less elapsed time**.
This is one fixed fixture in a VM, not an installed render-dialog benchmark or a
general performance guarantee. The prior installed responsiveness finding stays
open. No audio endpoint is activated.

## Native integrity acceptance

All four suites pass serially in **172.60 seconds**, with their original deadlines:

| Suite | Elapsed | Deadline |
| --- | ---: | ---: |
| Verified pair assembly | 21.07 s | 120 s |
| Owned assembly publication | 42.16 s | 180 s |
| Protected assembly publication | 54.63 s | 600 s |
| Paired history review | 54.71 s | 180 s |

The pair suite includes exact batch/single-stream equivalence, selected-side
results, admission refusals, cancellation before sibling output, late mutation
and resource-credit checks. Publication and history suites retain their original
output-integrity and source-proof assertions. All **802 source/test/dependency
input files** match host bytes. All **225 original project files** remain
unchanged after the comparison and test runs. Retirement finds no owned diagnostic
processes; all local VMs are shut off after evidence collection.

## Preparation and retained evidence

The first source-admission check refuses five cached files with different bytes,
before compilation or any product test. That failed run is retained. A separate
continuation supplies exact host copies and requires zero inventory mismatches;
it does not weaken product assertions or time limits. The incremental native
build runs with one compiler worker and inherited below-normal priority.

The qualification archive retains scripts, source inputs, stdout/stderr, original
failure, test logs, structured fixture evidence and file digests. Large generated
PCM and application binaries remain local. Source and result inventories bind the
observations to this implementation; successful compilation alone is not the
acceptance criterion.

The [acceptance archive](../tests/results/M2/2026-10-15-windows-batched-comp/manifest.json)
contains 1,844 entries and passes its payload hash/ZIP checks. The preceding
[hosted CI run](https://github.com/rhamenator/soundcurrent-daw/actions/runs/38787562432)
at `dfa5887` passes all **15 jobs**. All full logs and **12 original artifact
archives** are retained with published size, SHA-256 and CRC verification before
the draft PR advances. This preceding run does not qualify the new optimization;
fresh hosted checks are required on the updated head.

## Remaining work

Refresh matching preview candidates after the required checks, then repeat the
installed rendering workflow and measure the complete operation. Keep the earlier
preview available for rollback. Sustained hardware qualification, the outstanding
interruption/protected-warp findings and the full DAW goal remain incomplete.
