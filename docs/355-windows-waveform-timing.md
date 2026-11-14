# Installed Windows waveform timing qualification

Checkpoint 355 completes the remaining independent timing check for the frozen
`f2f9f7c` Windows waveform preview. A fresh workflow uses the existing installation,
the original owned project fixture, and the corrected UI Automation observer from
the start. No application binary, timing budget or acceptance threshold changed.
This qualifies the recorded preview workflow; full DAW parity remains incomplete.

## Fresh installed workflow

All 66 installed payload files, Desktop/Start-menu shortcuts and the per-user
uninstall entry match the checkpoint-353 receipt. No installer ran. The independent
clone has neither a compiler nor a Qt SDK on PATH; it already has the required
runtime, so this is not a pristine-OS installation test.

The actual installed application passed:

- Waveform channel 2, explicitly absent channel 1024, missing/restored-media retry,
  and return to overview without changing canonical project state.
- Rendered transition review and Apply, followed by Undo/Redo, save and reopen.
- Float WAV export and normal exit from both application launches.

The independent stopwatch starts before invoking Save and render. Review became
available after **21,821 ms**, within the unchanged **30,000 ms** limit. One
observation occurred before all dialog controls were published; it consumed the
same budget. No stale-element exception occurred in this run. The app's separate
elapsed label stayed at **21.2 seconds** after completion. Neither that label nor
a restarted wait substitutes for the independent measurement.

The corrected observer reads Qt's native `FullDescription` property. It handles
only `ElementNotAvailableException` during transient enumeration; other errors,
duplicate controls and expired deadlines still fail. Checkpoint 354's failed
observations remain preserved and are not retroactively marked successful.

## Export and preservation

The stereo float export contains **18,032 frames at 48 kHz**. Every sample is
bit-exact to the independent reconstruction, with maximum error **0.0** and peak
**5.5167012214660645**. Float headroom is preserved. The verifier also checks two
new native span completions, their executor identities, and exact project state
through Apply, Undo/Redo, reopening and export.

The host's 204 original project files and the guest's 202 original history/media
files remain unchanged. After normal exit, the guest rechecks all 66 installed
payload files, **536 installed-preview files** and **291 prior-workflow files**.
No owned application process remains.

The clone ran alone for approximately four minutes, with lowered CPU and disk
priority. It shut down gracefully. The capture receiver stopped, its narrowly
scoped firewall rule was removed, and its hash-verified temporary ISO was removed
after ejecting the configured media. All VMs are off. No physical audio was used.

## Hosted checks and next work

Run `38822590091`, at `be5e14b`, completed successfully: **all 15 jobs passed**.
All 15 original terminal logs and 12 original artifact ZIPs are retained before
advancing the draft PR. The acceptance archive includes the logs and metadata
that identify the retained ZIPs. This run also covers the newer dense-waveform
implementation; the installed previews still use the preceding frozen `f2f9f7c`
source. CI qualification is not an installed dense-preview claim.

The [acceptance manifest](../tests/results/M2/2026-10-15-windows-waveform-timing/manifest.json)
hashes the scripts, native observations, screenshots, verification result and
reconstructable owned project. The archive contains no application binaries or
transport credentials. Channel and completed-render screenshots were inspected.

Next implementation task: add bounded fine-window waveform preparation so zoomed
and cropped views can show accurate source detail instead of shaded coarse-bin
edges. Carry source identity, cancellation, resource accounting and stale-result
rejection through worker protocol, cache and viewport integration. Verify exact
window edges and channel selection with independent sample fixtures. Larger-file
policy, installed dense-preview delivery, the remaining DAW milestones and
European language qualification remain open.
