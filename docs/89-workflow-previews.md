# Useful workflow previews

Owner delivery priority, 2026-10-07. Full Reference A 6.1.3/Reference B 15.0.30 parity
remains the target. Deliver tested usable workflows incrementally; a preview does
not promote a frozen parity row or establish a complete language/platform.

## First delivery

Windows checkpoint, 2026-10-08: the [native single-track desktop workflow](99-windows-desktop-workflow.md)
passes owned loopback recording, live EQ/Undo, playback, save/reopen and WAV
sample checks with a silent lead-in. The subsequent
[installed Windows preview](102-windows-installed-workflow-preview.md) now provides
a local per-user setup/source pair with bounded clean-clone installation,
recording and removal evidence. Physical, non-silent startup, sustained audio
and broader installer qualification remain open; no binary release is uploaded.

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

## Runtime and upgrade checkpoint

The [separate runtime receipt](../tests/results/X007/2026-10-08-preview-runtime.json)
retains the packaging review correction, **62/62 Linux Debug** checks and the
changed Python/desktop **3/3** checks. The candidate is
`0.1.0~preview.20261008001000.85ec9552cdd7`, a **1,078,184-byte DEB** paired
with source commit `85ec9552cdd7161f08d8b189d842258241747b5e`.
Every one of its **701 tracked source archive files** matches that commit.

An owned rootfs derived from signed Ubuntu Base 26.04.1 installed the initial
candidate and runtime dependencies without g++, CMake or a Qt development SDK.
Normal apt upgrade to the sequenced candidate, remove and reinstall passed;
all owned project/media files stayed identical. Removal deleted the executable,
desktop entry and icon. Separate GUI test tools were installed afterward.
The installed GUI ran as UID/GID 1000 with no effective capabilities on a
read-only rootfs, opened/saved/reopened a Unicode project and exported **128 mono
float frames at 48 kHz**. Export samples exactly match the owned golden render.
Window focus errors in the Xvfb fixture and the original package-order refusal
are retained, rather than counted as successful first attempts.

This is a shared-host-kernel container with private Xvfb/D-Bus, not a fresh
complete desktop or VM. It qualifies runtime dependency resolution and the
scoped project/export/lifecycle operations. It does not qualify application-menu
launch, physical audio, active-recording upgrade refusal,
interrupted installation, preference/recovery preservation, Wayland or HiDPI.
No host packages/default audio routes or equalizer trees were changed. No release
was uploaded. [Candidate guide](90-preview-guide.md) describes the available
package and workflows; the installed audio workflow checkpoint below adds owned
recording/playback evidence. Complete desktop integration remains open.

## Installed audio workflow checkpoint

The same installed candidate was exercised as the normal user with private
PipeWire 1.6.2 and an existing null-sink clock provider. A normal WAV player supplied
owned mono 48 kHz input; no PipeWire implementation or production timing guard was
changed. The actual GUI selected its port, armed, recorded **493,568 frames / 10.28 s**,
stopped normally and attached the verified raw take with zero rejected frames.

An actual playback gain edit **−12 → −6 dB → Undo to −12 dB** was observed in an
independent output tap: 78 stable 100 ms windows at −12 dB and 22 at −6 dB, with
the −12 level before and after the edit. This is stable response evidence, not
exact event timing or callback deadline qualification. The raw file stayed unchanged.
The **480,000-frame** export agrees with an independent direct-form I peaking EQ
oracle to **2.8422e−14**. Normal save/quit/reopen preserved six project/media files
byte for byte; a second export was identical, and reopening made no automatic links.

Original failures remain part of the [new receipt and evidence capsule](../tests/results/X007/2026-10-08-installed-preview-workflow.json):
the first audiotestsrc route stopped after 1,024 frames with ClockDiscontinuity6;
a source configuration attempt created no ports; parser assumptions and probe
lifetime failures were corrected without changing the original bytes. The successful
raw take has **2,048 leading silent frames**. Every later sample matches the source
period, but complete source coverage/initial alignment is unqualified. A periodic
signal cannot independently detect dropped whole periods. The first rejected clock
is not exposed by current diagnostics, so its cause remains unresolved.

Both installed GUI sessions closed normally and all owned processes retired. Host
default metadata and existing links remained identical across all three private
experiments. There was no host package change, physical playback interruption,
VM run, equalizer write or release upload. All 257 compiled/resource inputs match
the pinned package source and earlier qualification cohort. Old receipts stay
unchanged; no frozen F/Q/C/N family or original native71 issue is promoted.

Next implementation task: retain bounded first-fault clock/input observations,
show useful recording failure details in the UI, and define/test first-valid-input
startup alignment without hiding dropped data or weakening clock guards. Then
complete application-menu/Wayland/HiDPI and native Windows preview qualification.

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
