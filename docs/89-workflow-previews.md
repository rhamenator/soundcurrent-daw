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

Review correction: the build cache's canonical source directory must match the
current checkout. Before stripping or adding packaging metadata, the exact CMake
installed file set and every executable/icon/desktop-entry/license/provenance byte
must match current qualified inputs. An unchanged executable hash alone cannot
prove that an older or different install tree matches the paired source archive.
Owned failure tests reject another/ambiguous source directory, five independently
stale inputs, extra/missing files and symlink payloads; retry preserves originals.

Freeze a monotonically increasing UTC `--preview-sequence` (YYYYMMDDHHMMSS) for
each candidate, and pass `--previous-version` when updating a distributed preview.
The builder checks actual Debian version ordering before creating an output tree.
Commit hashes identify source; they must not determine upgrade order. The first
owned upgrade attempt correctly refused the earlier date/hash version scheme as
a downgrade; its original log and package remain retained. No downgrade override
is the fix. A frozen sequence also gives rebuilds the same package version.

The [first local preparation receipt](../tests/results/X007/2026-10-07-local-ubuntu-preview.json)
records source `d23d3632f09e147d4fc7e663001075d4b93109b8`, Debug build,
**1,077,580-byte DEB** and separate exact source archive. Every extracted installed
payload byte matches the stage; offscreen help/version startup passes. All **698**
tracked source archive files match the commit bytes. No system installation,
fresh-machine/audio qualification or public release upload is claimed. The paired
artifacts remain in the local ignored preview directory. The next delivery gate
is an owned fresh Ubuntu install and recording/EQ/reopen/export workflow.

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
