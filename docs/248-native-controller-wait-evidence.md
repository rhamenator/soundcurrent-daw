# Controller wait evidence and retained failure projects

Checkpoint 248 improves recording qualification without changing product behavior,
audio coordinates, clock confidence, buffers or original observation deadlines.
The full suite and desktop recovery-copy integration remain incomplete.

## Observable wait stages

Every native controller wait now identifies its operation, failure variant and
source call site. At the original five-second expiry it captures one immutable
published controller snapshot before stack unwinding: phase, commands, error,
EQ revisions, cancellation acknowledgement, result sequence, retained paths and
processing/disk/capture/render progress. A snapshot can still precede preparation
or lag live workers; empty paths or zero generation remain reported as such.
It is not represented as a globally synchronized native-time observation.

The fake SDK publishes its stage, raw-packet count and unchanged virtual-clock
coordinate under a single-writer sequence. The observer makes at most eight
attempts and explicitly marks a busy/inconsistent tuple. Disk acknowledgement,
cycle work/elapsed maxima, pacing maxima and control flags are independently
labelled observations. No missing frame is replayed, no fit confidence is invented,
and the nominal coordinate stays unchanged even at service-thread exit.
Evidence reporting failures are contained so they do not replace the original
five-second failure. Formatting/locking/logging occur outside audio callbacks.

A separate test holds a real controller factory, expires the actual five-second
gate, and proves the published snapshot, cancellation and packet counts remain
unchanged before cleanup. It checks busy-sequence refusal, bounded concurrent
publication observations, failed output-sink containment and eventual owner shutdown.
The simulated-device tuple does not prove physical capture/render alignment.

## Keeping actual failed project files

The native test Temp owner now emits the standard `preserved_failure_project`
marker after controller/native destructors settle. The collector also scans the
bounded CTest LastTest log. Previously it scanned only isolated manual diagnostic
and PipeWire failure logs; the native controller's differently named marker and
CTest path were absent from collection.

Existing Windows parent names are expanded from possible8.3 aliases before exact
temp-parent comparison. This does not approve another directory or normalize a
junction into an allowed parent. The source project/entries retain reparse refusal,
no-overwrite and before/after/copied hash checks. Each project remains bounded by
512 entries and512 MiB. Marker limits cover four manual sources, one PipeWire source
and two controller/evidence sources, seven in total; duplicates count once.
Individual diagnostic logs remain limited to1 MiB, the aggregate CTest log to16 MiB.
Snapshots retain `atomicProjectSnapshot=false`; per-file hash verification does not
claim atomic cross-file state or a pre-cleanup media snapshot.

Linux PowerShell tests preserve exact Unicode/audio/controller bytes from a CTest
marker, reject foreign parents and excessive controller markers, deduplicate markers
and retain existing manual/PipeWire and reparse checks. Windows parentalias tests are
coded and remain independently pending on the new source.

## Independent reserve gate

The two newly added declared-reserve cases now run through `--declared-reserve`
and CTest `manual-declared-reserve`. The original manual aggregate retains its
60-second deadline and original twenty-take concurrency case. Every existing
control condition retains its five-second deadline. The new reserve workflow has
its own30-second envelope and callback audit. No cases or exact-prefix checks are
removed; the elapsed work is qualified in separate workflows.

## Acceptance

Code source3c6a3a9c3e6d3f44b9f9fb242f0b07007dcb0d6d is recorded in the
[evidence index](../tests/results/M2/2026-10-13-native-controller-wait-evidence/index.json),
with51 compressed text/configuration/metadata members,777 source-input bindings
and3 executable hashes. No PCM/executables/original ZIP payload is published.
Fresh Release build145steps passes **4/4 in28.54 s**:

| Workflow | Time |
| --- | --- |
| Controller expiry/evidence | 5.06 s |
| Native startup, controller and recording | 19.19 s |
| Original manual recording control | 1.12 s |
| Declared manual reserve | 3.16 s |

The observed expiry occurs at5,000,980,224 local nanoseconds, while Preparing has
zero published generation, empty job/root, no native packets/result and no
cancellation acknowledgement. These are actual snapshot facts, not a generated
successful preparation. Reserve copies remain bit-exact at48,128 frames within
capacity and96,000 frames on QueueFull; callback allocation/free/lock audit remains zero.
PowerShell retention passes with an exact77-byte controller fixture plus existing
manual/PipeWire cases and refusal checks. No UI source changes, VM, physical
endpoints, volume/device changes or new sanitizer claim are part of this checkpoint.

## Original preceding-source Windows qualification

[Original PR181 run](https://github.com/rhamenator/soundcurrent-daw/actions/runs/38348885352),
source1b6223655ec73e51c54814cc4f79e524b8b5638c, completes all four jobs successfully:
Linux159/159 in459.70 s, Windows native86/86 in302.72 s, Windows desktop20/20
in97.82 s, and core cross-build pass. Its original job logs, selected metadata and
Windows ZIPs are retained separately before further source publication.

That source independently qualifies the preceding recovery-copy APIs on MSVC:
all six mono/stereo/256-channel finite/full source mutation attempts are denied
by the pinned Windows read handle, and PipeWire copies17 paired frames despite
its34-frame standalone raw journal. There is no physical endpoint or new GUI-copy
workflow claim. These passing runs do not explain or erase the original PR180
controller wait or aggregate timeout. Its failed controller Temp bytes remain
absent from the original ZIP; future retention cannot reconstruct those bytes.

## Next concrete work

Retain new-source original Windows terminal results and inspect actual stage
observations if another wait fails. Preserve the original gates and confidence.
Connect both verified-prefix adapters to the desktop controller and review/copy
controls, qualify stale sequences/cancel/close and visible use, and perform real
Linguist update after UI source edits. Continue native playback/installers and
remaining recording/full-suite/European localization milestones.
