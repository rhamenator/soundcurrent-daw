# Installed Linux preview with render progress

Checkpoint 331 qualifies and activates the per-user Ubuntu preview from source
`b5dc2568dbf0e1e623dbaa7d42dcd38fd1eed3ab`, version
`0.1.0~preview.20261015172721.b5dc2568dbf0`.
[Checkpoint 330](330-linux-preview-candidate.md) retains its package, corresponding
source and 27 selected checks. This checkpoint tests the installed application.

## Actual public-control workflow

The installed executable and both observed comp workers match the qualified
package hashes. Public controls change two prepared clips to 1.5× speed, create
an equal-power transition using extended processed handles, render, review,
Apply, Save, Undo, Save, Redo and Save again. Rendering does not change the
arrangement before Apply; Undo restores the original tracks, and Redo restores
the exact saved applied project. A second normal launch reopens the exact state
and exports through the WAV destination control.

The render observer completes in 5,496 ms. Actual visible labels progress through
source/history checking, source-span rendering, assembly/verification and final
verification. The displayed elapsed time stops at 5.0 seconds on completion.
This is one synthetic fixture observation, not a general performance guarantee.

The exported 48 kHz stereo float WAV contains 18,032 frames and matches an
independent gain/fade/mix calculation **bit for bit**, with maximum error zero.
Its deliberately high synthetic peak, 5.5167, confirms retained float headroom.
No synthetic signal reaches physical speakers. All 180 original non-project
fixture files remain unchanged; all 19 installed payload files and the previous
preview's payload remain unchanged. Both direct launches exit normally through
File → Quit, with their owned workers and private audio services retired.

## Launcher and rollback

Only after workflow qualification passed was **SoundCurrent DAW Preview** in
Applications advanced to this version. The previous installation remains
available as **SoundCurrent DAW Preview (before render progress)**. Existing
older launchers were preserved. Desktop entry validation passed, and a third
launch through the normal Applications entry opened the exact installed binary
and saved project, then exited normally through File → Quit.

The tests use a private Xvfb display, session bus and PipeWire service containing
only Dummy and Freewheel nodes. They do not start a VM, alter physical audio
routes or interrupt user playback. This is per-user installed-workflow evidence;
system package installation, clean-machine installation and native Windows
qualification are separate gates.

## Retained setup failure

The first UI-driver attempt copied the historical project without its prepared
6,000-frame clip state. The speed assertion failed before any render started.
Public Undo and Save restored that copy, and the app exited normally. Its logs,
screenshots and original failure are retained. The intended workflow then used
the checkpoint-323 prepared state with SHA-256
`52b7d05a3c0055f88f7eff4935d24399fa39a5f8b48306156a917f3e196b4fd8`;
all referenced media hashes were checked. This was a fixture preparation error,
not evidence of a product render failure or a changed acceptance threshold.

## Evidence and next task

`tests/results/M2/2026-10-15-installed-progress-preview/acceptance.zip` contains
261 entries, 2,278,474 bytes, SHA-256
`ff40da7e9b9ef59e7581e0c9d6397bb0a44863cefcc31f732ad8325159312aa6`.
ZIP CRC and every payload hash pass. It includes scripts, actual UI snapshots,
state/installation/launcher receipts and both attempts. Generated audio is
inventoried by hash; private preferences and application binaries are excluded.

Original [CI 38777492388](https://github.com/rhamenator/soundcurrent-daw/actions/runs/38777492388)
on the same source remains in progress at this checkpoint. Preserve its original
terminal logs/artifacts before advancing the draft PR. Next, assess that native
Windows result and prepare a matching Windows candidate if qualification permits.
Earlier Windows interruption and prolonged-render findings remain open.
Full DAW functional/quality parity, hardware coverage and European localization
remain incomplete.
