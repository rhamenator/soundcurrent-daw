# Native and installed Windows protected stretch observations

2026-10-10 UTC. Exact tested source `cceedcc6e6d468a4a22cff617bf26d58515888e8`,
Git tree `5db68c1e903606eb5965f09175b5a983fc78cc64` (normally merged PR93).
Historical failed checkpoints retain their original identities and scope.

## Native qualification

The independent development clone was limited to four CPUs / 6 GiB and two
BelowNormal build workers. A clean exact checkout, 873 selected input hashes,
actual successful MSVC reconfiguration/build, and ten executable hashes are
retained. The executable hashes equal the previously compiled historical build;
this equality was measured after building the new source.

Three separate protected impulse requests completed with the bounded observer:
identity, uniform 3:2, and nonuniform maps. Each retains request, actual PID/exit,
ready/completion packets, original and worker/prototype float WAVs. Whole PCM
matches the prototype, with 1.5 peak headroom. None timed out, exceeded capture
limits, or had uncertain direct-process retirement.

The actual native banks completed: v5 has 519 checks, 33 owned operations with
21 completed renders and 12 expected refusals, 18 complete-PCM comparisons and
parent/adoption/live-graph/export/reopen tests. Windows reused some terminated
PIDs; operation identities distinguish requests. V4 retains 385 checks and 1,045
shared workflow checks. Three independent inspection/media/copy helper receipts
and five targeted native CTest groups passed. The development app's real default
window closed normally, exiting 0. This used SDK DLLs, not an installed workflow.

The old timeout did not recur. Its underlying cause remains unproved. These
observations must not rewrite the earlier failed receipts or claim full quality.

## Installed qualification

The independent acceptance clone has UUID
`670168f5-4e3f-4e7f-b218-3583db31f0f8`. It already contained an older preview and
Microsoft runtime 14.44.35211. This is a side-by-side preview installation on an
existing template-derived clone, **not a pristine OS or fresh runtime install**.
Original VMs and the pristine template were untouched. Only one VM ran at a time;
both test clones were observed off afterward. Host disk contention initially
slowed input; fields were verified/corrected before rendering.

Actual setup exited 0, verified all 64 deployed file bytes/hashes, Desktop and
Start Menu shortcuts, and uninstall registration. The developer compiler and
Qt SDK were absent from the acceptance PATH. Both real app processes loaded Qt,
libsndfile and the Windows platform plugin from the installed preview directory.
The observed installed helper (PID 9192, exact binary hash) also loaded its bundled
libsndfile. Unprivileged WMI process-start tracing was denied; 25 ms process/module
polling observed the helper. Parent PID/start-event tracing is not claimed.

The owned `project-été-Κиїв` fixture used a stereo 48 kHz, 32,768-frame impulse
source. UI actions rendered 3:2 duration with source frame 4,096 mapped to output
frame 6,144. Editing the target to 8,192 disabled Apply/Prepare audition, as shown
in app-only observations retained locally; restoring 6,144 reenabled them.
Prepare audition/Stop retired the prepared state; Play was never activated.
Actual saved snapshots preserve Apply / Undo / Redo; Undo restored the original
source and 32,768 frames. Redo and a fresh process reopen preserved the marker,
render identity and 49,152 frames. Both app processes accepted ordinary close,
exiting 0.

Both actual exports contain exactly 49,152 stereo float frames with 1.5 peak. Every
sample value equals the retained derivative, with no tolerance. The two exports'
PCM bytes match exactly. Derivative/export bytes differ at 2,175 signed-zero
samples because mixing accumulation converts -0 to +0; no nonzero value changes.
Raw source bytes remain unchanged. Existing-file replacement was explicitly
confirmed for the second owned export.

No endpoint activation, physical playback, new installer uninstall/reinstall,
interrupt/cancel/restart recovery, full stretch quality, F/Q/C/N completion or
European language coverage is established by this checkpoint.

## Locally prepared artifacts and source

Unsigned preview build `20261010122005-cceedcc6e6d4` is prepared locally:
installer 35,122,342 bytes, SHA256
`f68a47c80e11a70a978ef20b95e037de1600b5826abb0fd919ab2711bf19bde4`;
corresponding source 801,420,853 bytes, SHA256
`7baa6d4e464a51af880834294a5e91d5bc166e0b310ec26a43ee09c61c33dc3a`.
The source export was independently compared against all 2,211 Git blobs.
Declared CRLF export conversion applies only to
`.github/scripts/prepare-windows-desktop.ps1`. Qt/libsndfile source archives were
also supplied beside setup on the test ISO. Product executables, DLLs, installers,
SDKs, source archives, credentials and full desktop images are not in this capsule.
No product binary/release upload occurred.

## Retained verification

`capture.zip` pins 56 original records, including the original uploaded ZIP bytes.
Scripts/producers inside are read only as provenance; never execute them.
`verify.py` independently checks archive paths/bounds/hashes, conservative scope,
exact source/binary/observer/producer bindings, actual protocol and process exits,
whole retained WAVs, deployment source/payload receipts, Undo/Redo/reopen states,
installed DLL paths and signed-zero-only export differences. The full bank report
is retained, but its other generated media are not independently replayed here.
`refusals.py` rejects 12 meaningful altered-content/claim cases, including changed
PCM after recalculating archive/member hashes. Verification is not native replay.

```sh
python3 tests/results/M2/2026-10-10-windows-protected-preview/verify.py
python3 tests/results/M2/2026-10-10-windows-protected-preview/refusals.py
```
