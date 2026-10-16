# Installed Windows waveforms and retained observer failures

Checkpoint 354 installs the frozen `f2f9f7c` waveform preview in the independent
Windows installer-test clone. Installation, waveform controls, editing, persistence
and export have direct evidence. **Independent render-timing qualification remains
incomplete.** The overall preview qualification is therefore recorded as false;
this is not a full-suite or release-completion claim.

## Installed workflow

The exact checkpoint-353 installer ran once and exited successfully. All 66
installed payload files match its receipt. Desktop and Start-menu shortcuts point
to that installation, and its per-user uninstall registration is present. The
clone has no C++ compiler or Qt SDK on PATH. The existing Microsoft runtime stayed
installed; this was an upgrade-side-by-side check, not a pristine-OS test.

The actual installed application exercised:

- Source-channel selection, including channel 2 and an explicitly absent channel.
- A missing media file, restoration of the same bytes, and explicit waveform retry.
- Return to overview mode without changing canonical project state.
- Rendered transition review and Apply, Undo, Redo, Save and reopen.
- WAV export and normal exit from both application launches.

Waveform status is read through native Windows UI Automation's `FullDescription`
property. This matches the exact Qt 6.12 source: `QAccessible::Description` maps to
`UIA_FullDescriptionPropertyId`, whereas `HelpText` maps to `QAccessible::Help`.
The test uses the registered UI Automation class identifier and typed COM calls;
no application binary or accessibility behavior was altered for the observer.
Captured channel, absent-channel and recovery images were visually inspected.

The exported stereo float WAV contains **18,032 frames at 48 kHz**. Every sample is
bit-exact to an independent reconstruction using the resulting source media and
clip processing: maximum error **0.0**, peak **5.5167012214660645**. Floating-point
headroom is preserved. The host's 204-file original project remains unchanged,
and the installed workflow preserves its 202 original history/media files. Two
new native span completions and their executor identities are verified.

After normal application exit, all 66 installed payload hashes still match.
All **468 earlier-preview files** and **266 prior-workflow files** checked by the
guest remain unchanged. No physical audio was activated.

## Failures and limits retained

The initial VM start could not read the ISO through the protected home directory.
A verified owner-only copy in libvirt's media directory resolved access without
changing home-directory permissions. Installation then completed, but the first
capture upload was blocked by the host firewall before the app workflow started.
A temporary rule limited to the clone's capture connection allowed the existing
setup archive to be uploaded; installation was not repeated.

The first waveform observer incorrectly read `HelpText` and timed out despite
visible prepared waveforms. A subsequent attempt used an unavailable COM program
identifier and stopped before testing. The corrected native UI Automation reader
continued against the **same application process, PID 6264**, and passed the
waveform checks. Original reports, scripts and failure screenshots are retained.

During rendering, the desktop-wide UIA enumeration raised
`ElementNotAvailableException` while a dialog was being replaced. The original
30-second observation did not complete, so it remains failed/unproven. The app
subsequently displayed a completed render with a stable **19.4-second** elapsed
label. That label is not substituted for the independent timing gate.

A continuation handled only that specific stale-element exception and completed
Review/Apply through export against the **same rendered operation**. The initial
failure reports were preserved under separate names; the host verifier explicitly
requires `originalRenderWaitPassed=false` and reports
`overallPreviewQualification=false`. A fresh bounded observation is still needed;
no successful continuation retroactively passes the failed timing check.

The clone ran alone for approximately fifteen minutes, longer than intended due
to these observer repairs. It then shut down gracefully. The temporary capture
receiver, firewall rule and all five owned libvirt ISO copies were removed. All
VMs are off, and the original VMs/template were untouched.

## Hosted evidence and next task

At this checkpoint, run `38822590091` at `be5e14b` is still active. Its Windows
desktop job has passed all 26 tests, including the newer dense waveform work. Linux
synthetic shard 2 has passed all 45 tests, including `desktop-gui-resources` in
3.53 seconds. Those two complete original logs are retained; they do not establish
success of the unfinished overall run. All terminal run logs and original
artifacts must be retained before advancing the PR branch.

Next: use the corrected FullDescription reader and bounded stale-enumeration
handling from the start of a fresh installed workflow, preserving the original
30-second timing budget. Prepare its inputs, media location and capture rule
before booting the independent clone. Keep that session brief. The installed
Windows and Linux previews now use the same frozen waveform source; the newer
dense-overview preview, remaining waveform editing work and full DAW milestones
remain separate open work.

The [acceptance manifest](../tests/results/M2/2026-10-15-installed-windows-waveforms/manifest.json)
records the successful functional checks and the unresolved timing gate separately.
