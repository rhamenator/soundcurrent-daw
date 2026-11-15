# Installed Linux comp preview

The local Applications entry now launches source `258016450f93ebbecd2d62fa8ee6f9f9e60964e0`,
version `0.1.0~preview.20261015124133.258016450f93`, from its own per-user directory.
This uses the unchanged [checkpoint 319 package](319-linux-windows-comp-previews.md).
It does not rebuild or relabel that package as the later documentation revision.

## Installation and rollback

All installed payload files match the extracted, qualified DEB. The running main
executable SHA-256 is
`5673cce89335a47b865acc9f60a89db689bf10d8ca31c383cc8f73fc2254609c`.
The desktop entry passes `desktop-file-validate`; actual `gtk-launch` invocations
resolve to that executable and pass the project directory correctly.

**SoundCurrent DAW Preview (before 2026-10-15)** launches the preceding
`0.1.0~preview.20261013210831.1a3b2022cffa` installation. Its complete file inventory
is unchanged. The older **SoundCurrent DAW Preview (previous)** launcher is also
unchanged. No system package, equalizer repository, physical device route or VM
was changed by this installation.

## Acceptance evidence

The actual installed main window was launched through the desktop entry with an
owned project in a Unicode directory. Separate runs exercised Save/Quit and
reopen/Save/Quit, with normal exit code zero. Another run exercised the Export
audio dialog and normal Quit. Screenshots were inspected. Project state and raw
media remain unchanged after these workflows.

The export contains 480,000 mono float samples at 48 kHz. An independent SciPy
filter calculation from the saved EQ parameters and original raw samples matches
every exported sample exactly. The fixture's peak of `2.439275026321411` confirms
that this float export retains values above full scale; it was never sent to
physical speakers. Export SHA-256:
`1aa32b4d823b36c1272c72054da30e002a07c5ac72560685e3db8daefdf9d59d`.

Two earlier export-driver failures are retained. The first did not restore main
window focus after closing a modal dialog, so its Quit shortcut did not close the
app before the deadline. The second sent a key-release event to an already closed
dialog. Neither counts as a normal-exit success. The corrected driver explicitly
focuses the dialog/main window and uses focused keyboard events for the closing
dialog. It passes without extending deadlines or changing application code.

Evidence is in
[`acceptance.zip`](../tests/results/M2/2026-10-15-installed-linux-comp-preview/acceptance.zip),
with its adjacent archive manifest. It includes installation inventories, original
attempt records, scripts, screenshots, the owned fixture and export, and the final
independent verification result.

## Remaining work

This workstation test uses a private display and isolated PipeWire daemon. It does
not qualify a fresh OS installation, physical hardware, the newly added comp
render/edit workflow in the installed UI, or the Windows installer. Those scopes
remain distinct from the native/build/comp evidence in checkpoint 319. Current CI
at documentation head `6fd99c5` was still running at this checkpoint; no newer PR
revision is submitted until its original terminal evidence is retained and audited.

Next: qualify the existing Windows installer in the independent SDK-free
acceptance clone, preserving its older preview and all original VMs. Full DAW,
processing-quality, content, project compatibility and localization work remain
incomplete.
