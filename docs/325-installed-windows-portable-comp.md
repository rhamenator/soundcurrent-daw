# Installed Windows editing with Linux render history

Checkpoint 325 installs and exercises the `61bcaa3` Windows preview, sequence
`20261015144332`, in the independent Windows acceptance clone. This extends the
[candidate evidence](324-windows-portable-candidate.md) with actual installed
editing. It does not qualify physical hardware, a pristine OS, or full DAW parity.

## Installation and preservation

The existing Microsoft runtime remained `14.51.36247.00`. The per-user installer
exited zero; all 65 payload files match their receipt. Desktop and Start Menu
shortcuts and the uninstall entry point to the new version. Neither a compiler nor
a Qt SDK was on PATH. All 134 checked files from the two earlier previews remain
unchanged. The installer and corresponding-source hashes are in checkpoint 324.

## Actual desktop workflow

A copy of the [Linux-created project](323-installed-portable-comp.md), including
its Unicode path and complete processing history, was opened by the installed
Windows application. Through the public controls, the test extended the retained
take crossfade from frames 13,968–14,032 to 13,936–14,064 with an equal-power curve.

The original render operation produced two new 4,064-frame stereo spans. Their
requests reference the actual Linux assembly
`077d4102-d5bc-466b-8c51-3144e7343698`; the new completions record the Windows
worker and its `process-commit` memory metric. The Linux history retains its
`address-space` metric. Thus this exercised regeneration from cross-platform
history, rather than only playing previously rendered audio.

Review, Apply, Save, Undo, Save, Redo, Save, normal Quit, reopen, WAV export and
normal Quit completed. Undo restored the original tracks exactly; Redo restored
the entire applied project state exactly. Both actual installed application
processes exited zero after normal close, without forced termination.

An independent NumPy/SciPy calculation reconstructed the export from the saved
clips, source samples, gains and fade curves. All **18,032 stereo frames at
48 kHz** match exactly, with maximum sample error **0**. Floating-point peak
**5.5167012214660645** confirms preservation of internal headroom in this synthetic
test; physical audio was never activated. Export SHA-256:

`5a88856659abc1fdebb48ebdb452d6948fa9e5bfaffc898e2d7cf3dfeb9bcbac`

All 204 original Linux project files remain unchanged. The Windows result retains
202 original media/history files unchanged; only the project and previous-project
state differ among the original files.

## Original failures and limits

Two UI-driver attempts failed before rendering: a collapsed clip section and
combo-box selection that did not commit. The successful driver opened the section
and clicked the uniquely observed visible item using its accessibility-provided
clickable point, after confirming the foreground process and bounds.

The original render exceeded the driver's **30-second observation window**. The
same operation later completed; the application was not restarted and the render
was not retried. Exact elapsed render time was not captured. This is a functional
workflow pass with an unresolved responsiveness finding, not a passing 30-second
gate. Original worker deadlines and verification checks were unchanged. A capture
attempt while work was active failed on an in-use file; later captures preserve
the failure records and completed state.

The initial protected-warp test failure from checkpoint 324 is still unresolved.
This installed workflow does not explain that earlier failure. The newer export
accessibility-label and catalog-inventory changes are not in this installed build.

## CI and retained evidence

Original CI run [38762062379](https://github.com/rhamenator/soundcurrent-daw/actions/runs/38762062379)
at `3adb515eb3f4cafabb81d5af15a0099226e71da0` completed successfully: all 14 jobs.
The preceding failed run remains retained in checkpoint 324. Current CI evidence
retains every job log and API record, plus hashes for the 12 original downloaded
artifact archives; archive sizes, digests and CRCs are verified.

Evidence: `tests/results/M2/2026-10-15-installed-windows-portable-comp/`.
The 3,927,211-byte archive contains 143 entries and has SHA-256
`8fb77ec19b6a2cee1fe5efaa1607ede797682dbe365dac46964ddf9acb61cfd2`.
Its archive includes the actual scripts, original failures, UI observations,
installation and retirement receipts, independent sample checker and new media.
Unchanged synthetic project files reference the hash-pinned checkpoint 323 archive
to avoid duplicating old media. The manifest describes exact reconstruction.
Original authenticated capture ZIPs remain local with a published hash inventory.
Credentials, transport keys, installer binaries and VM images are excluded.

All VMs are off. The temporary receiver and firewall rule are removed, the CD is
ejected, and only this session's six delivery ISOs were deleted. Originals,
templates, existing preview files and equalizer repositories were preserved.

## Next concrete task

Measure the retained-history render phases with bounded timing instrumentation
and complete subprocess output capture. Identify the work responsible for the
slow installed render and the earlier truncated failure before changing limits
or verification behavior. Then qualify any resulting fix through the existing
Linux and native Windows workflows. Full-suite development remains active.
