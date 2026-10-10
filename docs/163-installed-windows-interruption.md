# Installed Windows GUI interruption and fresh recovery

2026-10-10 UTC. One actual installed partial-render interruption is qualified on
Windows11 in the existing full independent acceptance clone. This extends
[checkpoint162](162-installed-linux-interruption.md); the full goal remains active
and incomplete, with frozen Reference A/B and separate F/Q/C/N requirements.

## Exact installed preview and supervision

Installed sequence20261010183111 remains bound to source
`4fb8c1cdf1b1365e5834256f4b39b51566dbed52`, tree
`d1b6be0bd9114af21d73a5dd6ba8172b53510c44`. No product input was changed or rebuilt.
All64 installed payload hashes passed before launch. Critical Qt/libsndfile modules
in both GUI processes came from that installed directory. C++/CMake/Qt development
tools were absent from the restricted runtime PATH. Windows PowerShell compiled
only a C# test observer using the system runtime; it is not a new DAW build.

The owned observer uses public process handles, image names, creation FILETIMEs,
Toolhelp parent/thread observations and actual Wait/GetExit calls. It pauses only
threads of the identified owned helper. Handles remain open across deliberate
termination and every thread/process handle is retired after observed termination.
Parent/helper TerminateProcess requests use separate owned codes0x53430001 and
0x53430002; the request alone is not terminal evidence.
[Windows API documentation](https://learn.microsoft.com/en-us/windows/win32/api/processthreadsapi/nf-processthreadsapi-terminateprocess)

The GUI and observer use BelowNormal priority. The clone has four CPUs and6GiB;
its host process uses nice10 and I/O priority7. Originals/template/equalizers and
host audio routes stay untouched. The emulated device retains its SPICE backend;
no playback, capture, driver change, audition preparation, Apply or export is requested.

## Actual successful session

The fixture has Unicode paths, 1,048,576 stereo float frames at48kHz, duration3/2,
pitch0, formants off, no context and no protected markers.

| Actual process/boundary | Observation |
| --- | --- |
| Source GUI3012 | Open fixture; select clip; configure stretch through UI Automation; render using installed helper |
| Helper8304 | Four owned threads paused after partial output4120 bytes; exact image/creation time, parent and parsed arguments bind it to the fixture |
| Parent interruption | Actual handle wait returns1396899841; helper remains live and holds the lease |
| Fresh GUI6448 | Reopen fixture; recovery row shows Renderer still owns this job; Review reports IsEnabled=false |
| Separate helper interruption | Actual handle wait returns1396899842; all four owned thread handles close after death |
| Fresh GUI Scan again | Row changes to Incomplete render; Review reports IsEnabled=false; exact partial bytes/hash retained; no final WAV/completion |
| Normal fresh Quit | Actual process exit0; all three owned process handles terminal, observer thread joined |

The operator focuses the recovery list and sends Home before recording its rows
and button properties. Unlike the Linux screenshots, Windows records actual UI
Automation IsEnabled values. The SelectionItem IsSelected property was not
captured separately; these records must not be advertised as that assertion.
No completed audio quality or physical hardware qualification is implied.

Original saved project/raw and the older proven Applied/Attached fixture compare
unchanged. Original, reopened and final project snapshots match byte for byte.
The partial SHA-256 is
`4a240b51f270d942e4a6c7f82329b52e3b3e823a7883ad7100b8916e489ad033`.
The v4 ordinary R3 request/intent and separate selection binding agree; the test
is not a protected-marker v5 render or a completed non-unity quality measurement.

## Original failures preserved

Three earlier operators remain in the capture with original code, records and
actual cleanup receipts. Each new fixture starts only after prior owned handles
are terminal and a fresh process-presence guard passes.

1. Stretch control resolution failed after opening the fixture. Explicit combo
   expansion/selection and waiting for published controls resolved the next run;
   the original missing-control cause was not independently established.
2. Get-FileHash refused the active partial file as used by another process. An
   explicit read handle allowing read/write/delete sharing subsequently succeeded.
   Production writer behavior was unchanged.
3. A command-line substring guard rejected the observed helper. The next operator
   parses arguments and normalizes the exact root path. Its retained raw command
   contains forward/mixed separators and oldSubstringMatched=false, demonstrating
   that the earlier guard would reject this valid owned command. The previous
   run did not capture its raw CIM fields, so its exact predicate failure remains
   unproved rather than being silently assigned a cause.

Initial VM starts also failed because the test CD under the home workspace was
unreadable to QEMU. Moving only the owned private CD into libvirt's image directory
allowed boot; no original VM/disk permissions or base image were changed.

## Evidence, cleanup and next implementation task

[Retained capture and audit](../tests/results/M2/2026-10-10-installed-windows-interruption/README.md)
include four original ZIPs, executed operator sources, process/control/file
observations and host cleanup. They exclude executables, installers, runtime
packages, private transport material, credentials and personal audio. Bounded
verification runs no archived/native/audio program. Every recorded processing
path/digest/blob is checked against all184 inputs in the original installed source
and comparison commitdb20b36; this is not a relabeled compilation receipt.

PR102 ordinarily protected-merged as
`c4ab84841fcecf0d1fecd14789c748d7ffe6f85c`, after all four corrected-head CI gates,
exact test-merge/tree and all nine reviewed blobs were verified. Initial-head
results remain separate. No administrative bypass or protection change.

Twelve semantic refusals rebind nested/outer hashes to exercise altered button
state, helper exit, partial/opened-project bytes, missing source binding,
power-loss overclaim, reordered death/Active events, regressed timestamps,
reduced payload inventories and changed DLL hashes. Review corrections enforce
the successful event sequence and increasing UTC times, plus equality of every
installed payload entry with the independently retained, hash-pinned original
64-file package receipt. Original native observations remain unchanged. The clone shut down gracefully; actual VM state is off,
receiver exit0, owned CD ejected, temporary firewall rule removed and all four
owned private CDs removed after hash/ownership checks. All VMs are off.

Next: bounded resumable larger recovery inventories, with cancellation and
snapshot/stale-cursor handling. Keep additional installed interruption boundaries,
selection-property evidence, power-loss durability, M2 processing quality,
M2–M11/X004/X005, all-Europe translations/review/UI, content and native-project
compatibility open. Parent/helper deaths here are separate actions, not automatic
containment. Product artifacts remain local; full goal stays active/incomplete.
