# Installed Linux comp workflow with portable history

Checkpoint 323 qualifies the installed Linux preview at source `61bcaa3`, version
`0.1.0~preview.20261015144332.61bcaa3439ac`. The Applications entry **SoundCurrent
DAW Preview** now opens this version. The preceding `2580164` installation and
its launcher are preserved as **SoundCurrent DAW Preview (before portability
fix)**; the older rollback entries remain available.

This is a per-user Ubuntu 26.04 amd64 installation. It is not a clean-OS install,
a public binary release, Windows qualification or a full DAW parity claim.

## Actual installed editing workflow

A fresh copy of the native Windows comp-history fixture was opened through the
real Applications desktop entry. Public AT-SPI controls and normal combo-box
keyboard selection performed the workflow. A private display, D-Bus session and
PipeWire daemon kept the test away from physical endpoints and user playback.
No VM was started.

1. Change both selected clips to 1.5× playback speed: 4,000 output frames each,
   with the existing fractional source position preserved. Position them at
   frames 10,000 and 14,000; save.
2. Request retained-take handles and extended processed handles for the
   equal-power crossfade at frames 13,968–14,032; choose **Save and render**.
3. The installed workers finish in **6.108 seconds**, inside the original
   30-second observation limit. Both worker paths and executable hashes match
   the installed package, and both processes retire. Rendering leaves the saved
   timeline clips unchanged.
4. Review the selected transition, choose **Apply reviewed audio**, and save.
   The two new clips have 4,032 frames and a 64-frame overlap. One Undo restores
   both original clips exactly. Redo and Save reproduce the full applied project.
5. Choose **File → Quit**. The actual installed app exits with code zero.
6. Launch the same project through Applications again. Its full saved state is
   unchanged. Export through the real dialog, save, and choose **File → Quit**;
   this second launch also exits with code zero.

The prior installed failure and keyboard-shutdown timeout in
[checkpoint 322](322-portable-render-history.md) remain recorded. This successful
run uses the corrected worker and an explicit public Quit action; it does not
retroactively qualify that failed run.

## Independent export check

The exported WAV contains **18,032 stereo Float32 frames at 48 kHz**. A separate
Python/NumPy/SciPy calculation reads the two adopted source WAVs and reconstructs
their placement, clip gain, equal-power fades and summation. Every exported sample
is bit-identical to the independent result; maximum absolute error is zero.
The leading and trailing silence are checked, and the peak remains
`5.5167012214660645`, confirming floating-point headroom in the file.

Export SHA-256:
`70a071120e26b4aa4a2b5cdd4a220d0932c604d5ad926ac9834067201894af6b`.

The original source fixture is unchanged. In the copied project, all 180 original
media/history/support files remain byte-identical. `project.json` and
`project.previous.json` change through normal Save operations; both match the
final applied state. The previous preview and every new installed payload hash
remain intact. App and private audio-daemon processes have exited.

## Build, packaging and accessibility

All 25 selected package checks pass in 176.23 seconds, including repeated comp
production, worker validation, export, localization catalog checks and existing
recording/controller tests. This is scoped regression coverage, not the entire
suite or native-speaker translation review.

The local 7,098,014-byte DEB has SHA-256
`f006fca3e1c7b6753d3c3de06926f6364cb3ee8c43f99b097ae3e0f9f1470509`.
Its matching GPL source archive has SHA-256
`b21806e762538d7cd1c4d1f4efb3d76cbe6d80cb1574d8c1023c0c4e9cdee0d4`.
All 8,057 tracked source-archive entries match the frozen commit, including six
Git-prescribed line-ending conversions; gzip CRC and the full archive hash pass.
Both are under `.cache/p323-package-20261015144332/`. The source archive includes
historical evidence and is about 2.49 GB; it is not needed to run the application.

The first export driver stopped because the visible destination label had no
accessible association with its text field. No export had started. The same live
dialog was continued using the observed field and value, then completed normally.
The source now gives that label an explicit Qt buddy association. The actual
Qt accessibility interface exposes the localized name, verified by the existing
`desktop-export-ui` suite (2.59 seconds). This small accessibility correction is
newer than the installed `61bcaa3` package; its installed/native Windows check is
still required. The initial driver failure is retained separately.

## Hosted checks and Windows preparation

[Run 38752921724](https://github.com/rhamenator/soundcurrent-daw/actions/runs/38752921724)
at `61bcaa3` passes all 14 jobs, including both Windows native groups and their
required aggregate. It qualifies the portability correction independently on
hosted Linux/Windows runners; it predates the additional export-label correction.
All 14 original job logs and 12 artifact archives are retained locally, with
archive size, digest and CRC checks passing.

The original Windows build-input artifact was downloaded and checked against its
published size and SHA-256. All 1,794 native source-input hashes match an isolated
checkout of `61bcaa3` with the producer's line-ending settings, and all 15 product
deployment-file hashes match. Dependency/provenance checks pass. The bundle is
prepared locally for the next Windows qualification; no new Windows installer or
local VM execution is claimed here.

## Evidence and next work

The acceptance archive and reconstruction manifest under
[`tests/results/M2/2026-10-15-installed-portable-comp`](../tests/results/M2/2026-10-15-installed-portable-comp/)
retain scripts, public UI observations/screenshots, saved states, output WAV,
qualification reports and package receipts. Shared historical fixture files are
referenced by exact hash from checkpoint 322's committed archive; reconstruction
is checked byte-for-byte. No executables, credentials or private session settings
are included.

Next: qualify the corrected Windows candidate, including editing a project with
Linux-created render history. Prepare the artifacts before a brief independent
VM session. Continue the full milestone backlog afterward; sustained hardware,
processing quality, complete localization, content and full professional workflow
parity remain open.
