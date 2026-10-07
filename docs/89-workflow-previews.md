# Useful workflow previews

Owner delivery priority, 2026-10-07. Full Bitwig 6.1.3/Cubase Pro 15.0.30 parity
remains the target. Deliver tested usable workflows incrementally; a preview does
not promote a frozen parity row or establish a complete language/platform.

## First delivery

An Ubuntu 26.04 amd64 recording/EQ/project/WAV preview using the existing PipeWire
backend. Include a normal DEB application package, desktop/menu icon, declared
runtime dependencies, exact source archive and capability/limitation notes. The
user should not install developer tools. Other Ubuntu versions, Fedora/RHEL and
Windows need separate backend, runtime and installation qualification.

`tools/package_linux_preview.py` is maintainer tooling. It refuses a dirty source
tree, changed tested inputs/executable, unsuccessful qualification, a native-audio
disabled build or a different first-target OS/architecture. It stages normal CMake
install output, includes notices/docs, derives linked runtime dependencies from
the copied binary and declares Qt platform plugins/existing PipeWire. It does not
install the package, configure a daemon, change routes or upload a release.
It retains failed packaging output and prepares paired application source.

Each produced package records its actual Debug/Release build type, original and
stripped executable hashes, source commit/tree, dependency metadata and extracted
payload/startup checks. Those checks use the existing developer host: they are
not clean-install, audio or sustained-recording qualification. First packages
remain local until fresh-machine setup and real preview workflows are qualified.

## Workflow acceptance before recommending use

1. On an owned fresh supported system, install the supplied binary package without
   a compiler/Qt SDK; launch through the normal application menu.
2. Create a project and record one explicitly selected input for ten seconds,
   initially with an owned synthetic PipeWire source/sink and then a separately
   coordinated physical-device check. Existing user playback must be preserved.
3. Stop, play the raw take through live EQ, change and Undo a gain gesture.
4. Save, close and reopen; media, IDs, edits and route intent remain correct.
5. Export WAV and independently check the resulting frames and samples.
6. Demonstrate one recoverable interrupted recording, then upgrade/uninstall
   without deleting the owned project/media/recovery data.

The implementation already has scoped short owned-source and synthetic evidence,
but physical latency, sustained recording/native observation71, full graph/RT
capacity, native Windows and release installation remain gaps. Do not recommend
the preview as the only recorder for important sessions or imply these gaps are
resolved. Preserve clear faults and recovery rather than hiding failures.

## Following previews

| Preview family | Useful workflow | Qualification needed |
|---|---|---|
| Recording/editing | Dependable multitrack/punch/takes, grouped edits and recovery | M2 workflow boundaries, sustained resource/device evidence, installers |
| Native Windows | The same recording/EQ/project/export workflow | User-mode audio/backend, native Qt and file failures, Unicode/installer/trust tests; no VB-CABLE prerequisite |
| Mixer/monitoring | Buses, sends, cue mixes, monitoring-only correction | Routing/PDC/export separation and per-platform evidence |
| MIDI/plugins/performance | Compose with expressive MIDI, isolated plugins and launcher capture | M3/M5/M6 detailed workflows, lifecycle/crash/state and timing evidence |

Keep source backups public as authorized. Build and test local binary/source pairs
first; public release publication and distribution trust remain separate from a
source push. [X007](88-easy-installation.md) remains the installation contract.
