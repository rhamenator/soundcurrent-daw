# Actual Windows parent interruption and retained render recovery

2026-10-10 UTC. Frozen Reference A/B scope and separate F/Q/C/N gates remain
unchanged. This extends [Linux checkpoint 160](160-linux-parent-interruption.md).

## Native implementation and source binding

The C++ Qt Core qualification process uses the production render controller,
project store, actual render worker and fresh production recovery/review. CMake
now builds it on Linux and Windows and selects the platform's test supervisor.
The native Windows desktop CI builds and runs the new regression alongside its
existing tests, without audio endpoints.

The Windows supervisor opens native process handles, verifies image paths and
records PID/creation time. It uses TerminateProcess for deliberate interruption
and waits on those exact handles before reading terminal codes or closing them.
Termination is asynchronous; requesting it is not terminal evidence.
[Microsoft documentation](https://learn.microsoft.com/en-us/windows/win32/api/processthreadsapi/nf-processthreadsapi-terminateprocess)

During the partial-output witnesses, public thread snapshots/handles select only
the owned helper's threads. Suspend/resume counts are paired and handles closed.
This debugger-style test pause observes an interruption boundary; it is not a
product synchronization primitive or an audio callback operation.
[Microsoft SuspendThread documentation](https://learn.microsoft.com/en-us/windows/win32/api/processthreadsapi/nf-processthreadsapi-suspendthread)

Native MSVC/Qt development-clone build at source
`46a6d3f8f754e08785f2b841a6d18c1379bc0f5e`, tree
`3014b5afc78b93854d0f99872faf0f520a8de40b`, succeeded in 31.5605293 seconds.
The corrected executed Python driver is bound to
`8fc9db65a0aa69d3220fdff8583e26d2fa5fe846`, tree
`3d2c39bc2eb8b3d1df93c9441a781714fbf6ade5`. Among 613 audited native
CMake/C/C++/header/include/Python inputs, only that Python driver changed after
build. The successful run reused the unchanged compiled probe/worker, with
matching executable hashes and the original build receipt preserved. It does
not relabel the native compilation to the driver revision or a later merge.

The original build used two jobs at BelowNormal priority; the driver and its
children also used BelowNormal priority. The corrected session took 13.037617
seconds including its before/after source audits. This is a host development
qualification, **not an installed SDK-free GUI or performance benchmark**.

## Four native witnesses

Fixtures use 1,048,576-frame 48-kHz stereo float input, Unicode paths, unity
stretch and a peak of 2.0. The probe does not open an audio endpoint or request
playback. It qualifies supervision, persistence and recovery, not non-unity
processing quality or physical audio.

| Boundary | Actual Windows result | Fresh production recovery |
| --- | --- | --- |
| Before acknowledgement | Parent forcibly terminated with owned code `0x53430001`; orphan remains live; helper hard watchdog exits 2 | Active while writer owns lease; then Incomplete, no start/audio/complete marker |
| During processing, parent only | Four owned helper threads paused with partial output 786,536 bytes; parent forcibly terminated; helper resumed and exited 0 after committing | Active while paused; then Ready; explicit review succeeds |
| During processing, both processes | Parent forcibly terminated; Active scan; separate helper termination with owned code `0x53430002` | Incomplete; exact 3,301,480-byte partial WAV/hash retained; no final WAV or completion |
| After child reaping, before parent verification | Completed audio/receipt exists at the afterChild hook; parent forcibly terminated | Ready; explicit review succeeds; no invented helper terminal code |

All four saved projects/raw files remain unchanged. Fresh recovery scans/reviews
start no new render worker and attach nothing. Independent retained-file audits
verify every member/hash, process identity/terminal linkage, completion/binding
receipts, incomplete partial bytes and the two completed float WAVs. Completed
unity output numerically equals raw PCM and preserves peak 2.0.

Parent death alone does not immediately contain the Windows helper. Its lease
keeps live work unavailable for review. The resumed orphan here committed and
exited 0; Linux's earlier witness instead committed then exited on SIGPIPE.
Those are separate actual results, not assumed cross-platform exit-code parity.
The separate helper-kill case is not automatic parent-death containment.

## Operator failures preserved

The initial Windows file audit tried to hash `writer.lock` while its writer held
an exclusive, no-sharing handle. It failed with PermissionError. The fix defers
that one lock-file hash during Active comparison; complete file hashes, including
the lock, are captured after actual termination. Production lease behavior did
not need modification.

The next run reached the final boundary but a read of the visible published
`parent-ready.json` returned PermissionError. Its underlying cause remains
unproved. The driver now retries that particular read within the same original
deadline and owned process instance, with live-state checks and terminal cleanup.
It saves each case report immediately. The successful run recorded **zero** such
read refusals; it does not prove the retry branch was exercised or explain the
original refusal. Both original failed scripts, logs, fixtures and available
cleanup receipts remain in the archive.

## Cleanup, evidence and next task

[Original capture and bounded verifier](../tests/results/M2/2026-10-10-windows-parent-interruption/README.md)
retain all three sessions' owned observations and exact source/input manifests.
The archive excludes products, installers and credentials. The verifier executes
no archived code, native process or audio. Runtime regression is the platform's
`desktop-stretch-parent-interruption` CTest; archived observations are not rebound
to future CI runs.

The full independent development clone shut down normally. Its final process
check found no owned probe/helper; all VMs are off. The existing installed preview
clone, originals, pristine template, equalizers and host audio routes stayed
unchanged. No receiver or firewall rule was needed. Product artifacts remain local.
PR100 merged ordinarily through all four protected gates as `673e9e5`, with the
reviewed/tested/merged tree and all eight blobs matching; no policy change/bypass.

Next: actual installed GUI interruption and fresh recovery on both platforms,
then bounded resumable larger recovery inventories. Preserve the distinctions
between parent death, separate helper death, normal quit and power loss. Broad
M2 quality, power-loss/scalability, M2–M11, X004/X005, all-Europe translation/review/UI,
content and native compatibility remain open. Full goal stays active/incomplete.

## 2026-10-11: current CI partial-baseline correction

Current PR107 desktop job 114349970301 failed the supervisor's comparison of a
partial-file hash taken before helper termination with one taken afterward.
Its source 4cccc97 and complete job log remain separately bound locally. The log
does not contain the two hashes/byte counts, so the cause of that particular
change remains an inference; it is not established as data corruption.

Suspending user threads is not a kernel I/O completion barrier. Microsoft
documents that a terminated process cannot exit until pending I/O completes or
is canceled. The supervisor already waits for its exact process handle; it now
establishes the immutable partial baseline after that terminal barrier.
[TerminateProcess contract](https://learn.microsoft.com/en-us/windows/win32/api/processthreadsapi/nf-processthreadsapi-terminateprocess).

The new driver records pretermination and terminal byte counts/hashes separately,
including any difference. Active inventory defers the still-owned writer's
partial hash alongside its exclusive lock; project/raw/intent and other immutable
files remain checked. Once terminal, fresh Incomplete scanning must preserve
every file, including the exact terminal partial digest, and publish no audio or
completion marker. Deadlines, exact-process identity/exit checks and cleanup
remain unchanged. Fresh native CI is required; the original source 8fc9db6
qualified receipt above is unchanged and is not retrospectively expanded.
