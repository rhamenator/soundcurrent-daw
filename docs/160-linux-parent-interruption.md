# Actual Linux parent interruption and retained render recovery

2026-10-10 UTC. Frozen Reference A/B scope and F/Q/C/N gates remain unchanged.
This follows [checkpoint 159](159-windows-recovery-guards.md).

## Implemented acceptance probe

A small C++/Qt Core process runs the production StretchController, ProjectStore,
selection publication, actual render worker and production inventory/review.
A Linux Python supervisor uses pidfds for signals, records executable/PID/start
identity, adopts orphan helpers with child-subreaper ownership, and observes actual
wait statuses. It never treats an observation deadline as terminal process evidence.
The existing control-worker qualification hooks hold exact acknowledgement and
post-reaping boundaries; they do not run in an audio callback. During processing,
only the owned helper is stopped/resumed to observe an actual partial WAV.

CMake registers `desktop-stretch-parent-interruption` on Linux with a 60-second
outer limit. One targeted Release CTest passed in 7.36 seconds. A separate retained
run captures the same four workflows and original files. No production C++ input
changed; engine/controller inputs are byte unchanged from `5300010`. The new
probe is a native host build with development/runtime dependencies, **not an
installed SDK-free GUI qualification**. Its observed local source checkpoint is
`fd84747511e9618a661c4870f1c83fc1aa578c51`; retained source hashes bind the probe
and driver despite later documentation commits or branch rebasing.

## Observed workflows

All four saved fixtures contain a 1,048,576-frame, 48-kHz stereo float source
with a peak of 2.0, Unicode media/project paths and a unity stretch request.
This covers supervision/publication/recovery, including float headroom, not
non-unity Rubber Band sound quality or endpoint playback.

| Interrupted boundary | Actual process observation | Fresh production recovery |
| --- | --- | --- |
| Before start acknowledgement | Parent SIGKILL; live orphan still owns writer lease; helper's 5-second hard watchdog exits 2 | Active while owned; then Incomplete. Intent and parent selection retained, no start/audio/complete marker |
| During processing, parent only | Parent SIGKILL while partial audio exists; orphan resumed, committed media, then exited on SIGPIPE 13 when its stdout had no reader | Active while stopped; then Ready. Explicit review succeeds despite abnormal helper terminal status |
| During processing, both processes | Parent SIGKILL; Active scan; separate helper SIGKILL, actual wait signal 9 | Incomplete. Partial WAV bytes/hash retained; no complete marker or final WAV |
| After completion, before parent verification | Helper already reaped at the afterChild hook; complete media exists; parent SIGKILL | Ready; explicit review succeeds. No invented terminal helper exit code |

Every fresh recovery subprocess reads the saved project, scans through the
production controller and reviews only Ready results. Review starts no new worker
and attaches nothing. Saved project/raw file hashes remain unchanged in every
case. The two completed unity results independently compare numerically with the
raw float PCM, preserve peak 2.0 and agree with media/sample/selection digest
receipts. Partial-file recovery preserves the captured bytes; it does not repair,
delete, attach or silently resume them.

Parent death alone does **not** immediately terminate the Linux helper. Until its
actual exit the writer lease prevents review. Bounded work may publish a valid
completion before losing stdout; completion validity comes from independently
verified files, not exit zero. The separate two-process interruption witness must
not be described as automatic parent-death containment.

## Failed setup and evidence

The initial operator supplied a relative root. Production admission refused it
with `Invalid stretch project selection`; the ticket observation deadline expired.
That original log and driver are retained. The supervisor killed/reaped its owned
parent, and no helper was running afterward. The corrected driver passes an
absolute root and detects a parent exit before waiting for a boundary.

[Retained capture and bounded verifier](../tests/results/M2/2026-10-10-linux-parent-interruption/README.md)
include the four original fixtures, process reports, probe/driver source, CTest,
build and failed-setup logs. The archive contains no product binaries/installers
or private transport. Its verifier checks every member/hash and independently
checks project state, leases' observed classifications, statuses, publication,
partial bytes and float WAV contents. It executes no archived scripts or audio.

No VM, audio daemon, output device, physical microphone/speaker or equalizer was
used or changed. All owned probe/helper processes have terminal observation.
The existing installed previews remain bound to their earlier source versions.

## Next implementation task and open gates

Qualify actual parent interruption on Windows with native process handles and
its OS ownership rules. Then exercise installed GUI interruption/recovery on
Linux and Windows, retaining the same boundary distinctions. Implement bounded,
resumable larger recovery inventories after those workflow gates; do not simply
raise the existing 128-entry/8-MiB limits. Power-loss durability needs independent
crash/storage evidence beyond these file-flushed receipts.

Broad M2 processing quality, installed interruption, Windows interruption,
larger recovery inventories, M2–M11, X004/X005, all-Europe translation/review/UI,
content coverage and native-project compatibility remain open. Full goal active;
this checkpoint does not establish product completion or frozen-reference parity.
