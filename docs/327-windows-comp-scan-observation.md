# Native Windows comp scan observation

Checkpoint 327 measures the [bounded source-scan change](326-comp-source-scan-batching.md)
with native MSVC binaries in the independent development clone. This is a
verification workload, not a complete render or installed-preview qualification.

## Source and workflow

The owned source checkout is `82bae0027581a99758245f90625590383a1b923d`. The baseline
build substitutes only the two changed source files with their exact predecessor
bytes from `2fdeaf9`; those files are restored before the second build. Both
binaries use the same compiler, build options, libraries and diagnostic source.
The diagnostic reads its Unicode project path through UTF-8 JSON on stdin.

The host independently verifies 444 source inventory entries: 442 match exactly,
and two license files match Git's Windows CRLF checkout filtering. No compiled
source differs through that filtering. All 225 input project files match the
retained Windows project from checkpoint 325.

Three alternating runs per binary successfully verify the same historical
assemblies, owned spans and complete 16,256-sample assembly. All project files
remain unchanged. Each run exits zero with no stderr. Native audio was not
activated, and no installed preview was replaced.

| Observation | Before | After |
|---|---:|---:|
| Median complete diagnostic time | 5.841 s | 4.132 s |
| Individual times | 6.605, 5.796, 5.841 s | 4.385, 4.132, 4.091 s |

The observed median reduction is about **29%**. Windows maintenance and security
scans were active, and the coordinator's priority was changed during build
preparation. These observations support a local improvement but do not establish
a quiet-machine benchmark or a general performance guarantee. They do not resolve
checkpoint 325's original 30-second whole-render observation failure.

## Preserved build failures and cleanup

The first serial build reached its explicit 420-second limit with 13 steps left.
Its complete stdout, stderr, timeout traceback and process-tree retirement are
retained. A separately recorded incremental continuation finished in 90.120
seconds. Rebuilding the two changed source files took 29.044 seconds. No audio
worker deadline or integrity check was relaxed.

The continuation coordinator initially made little observable progress. Only that
owned process was changed from BelowNormal to Normal priority; no system service
or security setting was changed. The original timeout remains a failed build
attempt, despite the successful continuation and six completed measurements.

After collecting the original capture, no owned diagnostic processes remained,
the source checkout was clean, and the VM shut down normally. All VMs are off.
Original VMs, the pristine template and equalizer repositories were untouched.
The cached native build is preserved for future incremental work; repeating a
cold build in this VM would waste time and increase contention.

## Original CI failure

Original run [38766825424](https://github.com/rhamenator/soundcurrent-daw/actions/runs/38766825424)
at `2fdeaf9` finished **failed**: 12 jobs passed, the Windows desktop job failed,
and its required aggregate failed. All original job logs and artifact archives
are retained separately from this native measurement.

The failing test was `desktop-stretch-parent-interruption`, during processing.
`SuspendThread` returned failure with Windows error 5; cleanup then failed with
access denied from `TerminateProcess`. The retained timing records show an
8.709-second partial-boundary wait and a declared parent deadline of 5,000 ms.
These are observations requiring investigation, not grounds to raise the deadline
or call the run green. All 14 job logs and 11 original artifact archives are
retained with verified sizes, hashes and CRCs.

Artifact inspection exposed a retention gap: the uploaded desktop artifact has
timing/failure JSON and logs, but not the failed project's media. The old test
marker was not recognized by the bounded fixture collector. Those missing
original files cannot be recovered from the uploaded artifact.

The Windows interruption fixture now uses an owned parent/UUID directory and
emits a strict UTF-8 marker recognized by the collector. Explicit user-specified
retention paths are not automatically collected. The same path, link, size,
entry-count and no-overwrite checks remain. The desktop artifact includes hidden
files so lock files are retained alongside their manifests. The collector test
passes exact Unicode paths, partial audio bytes, hidden files, and foreign-path
and overwrite refusals. This repairs future evidence collection; it does not
make the original interruption failure pass.

## Evidence and next task

Evidence is in `tests/results/M2/2026-10-15-windows-comp-scan-observation/`.
It includes the captured original and continued logs, exact diagnostic and build
scripts, source/project inventories, independent host verification and original
CI records. Probe binaries remain local; their exact hashes are recorded. The 80-entry archive
is 808,121 bytes with SHA-256
`56667738c4b928ad87196f70e4af297e8da69d4abd3d0e052806ee3a04a6dfe9`.
Its CRC and every payload hash were checked. Original CI artifact ZIPs are retained
locally; this archive includes their metadata, all job logs, and the original
failed desktop artifact, without duplicating the large successful media corpora.

Next: inspect the retained interruption timing and process records to distinguish
a missed observation boundary from a product failure. The original failed project
was not uploaded; a future run must retain its own fixture independently. Ensure cleanup preserves the primary
failure when a retained process handle races with natural exit. Keep the original
timing and interruption requirements, then qualify the source change in CI and
refresh the preview packages. Full DAW parity remains incomplete.
