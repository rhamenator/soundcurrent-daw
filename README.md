# SoundCurrent DAW

[![Development checks](https://github.com/rhamenator/soundcurrent-daw/actions/workflows/ci.yml/badge.svg)](https://github.com/rhamenator/soundcurrent-daw/actions/workflows/ci.yml)

A Linux-first professional digital audio workstation and recording suite, built
with **C++20, CMake and Qt 6**, licensed **GPL-3.0-only**. The processing engine is
independent of Qt and shared by live audio and offline rendering.

**Early development preview — not a production-ready DAW.** The target is the
combined functional capabilities of **full Bitwig Studio 6.1.3** and **Cubase Pro
15.0.30**, frozen as **SC-DAW-BASELINE-2026-10-05**. No matrix family is currently
qualified as equivalent. Functional parity, processing quality, bundled content
and native-project compatibility are tracked separately.

Windows functionality, all-Europe localization and imports from other suites are
required parts of the goal. Native Windows audio/desktop/installers and reviewed
translations remain unfinished. See [the active goal](GOAL.md) and
[the acceptance matrix](docs/01-parity-matrix.md).

Project-import development now includes [approved media folder/file handles](docs/122-approved-media-roots.md)
and [pinned WAVE metadata/content checking](docs/123-pinned-wave-validation.md).
The desktop inspector now has an [explicit media checklist](docs/124-desktop-media-checklist.md)
with folder/replacement selection, format/sample-peak results and cancellable child
checks. Original source bytes and floating headroom are preserved. Broader audio
formats and destination project conversion remain required; installed previews
need a refresh and separate qualification.

The [Windows desktop workflow](docs/99-windows-desktop-workflow.md) now records
through the native desktop, plays with live EQ/Undo, saves/reopens and exports WAV.
Independent raw/output/export sample checks pass on an owned loopback route with
a silent lead-in. A [local Windows 11 x64 installer/source pair](docs/102-windows-installed-workflow-preview.md)
now passes scoped clean-install, shortcut, normal removal/reinstall and installed
native workflow acceptance. Bounded non-silent startup and normal endings now have
[independent native evidence](docs/107-windows-end-guard.md). Physical/sustained
recording and monitoring/duplex remain open preview gates. The
[refreshed Windows installer](docs/108-windows-installer-refresh.md) passes normal
installation/UI checks, but all three installed native attempts detected capture
discontinuities. It is an installation/UI preview with experimental audio;
the saved full/partial raw takes and failures remain retained. Abrupt production
EQ Stop also remains unresolved.

The [bounded Windows capture trace](docs/109-windows-capture-trace.md) retains
one complete and two refused recordings, with exact raw samples and SDK packet
gaps. It is diagnostic evidence; installed recording reliability remains open.

[Easy installation](docs/88-easy-installation.md) is required on supported Linux
and Windows systems: normal packages/installers, no compiler or manual dependency
assembly, app-menu shortcuts, and tested upgrades/removal that preserve recordings.
The build instructions below are for developers; broader end-user installer
qualification remains open.
The [preview delivery plan](docs/89-workflow-previews.md) starts with the existing
Linux recording/EQ/project/WAV workflow. Local binary packages need fresh-machine
qualification before being described as supported installers.
An Ubuntu 26.04 amd64 DEB candidate now passes scoped runtime dependency installation,
normal-user project/export and upgrade/remove/reinstall checks in an owned Ubuntu
Base environment. See the [candidate guide](docs/90-preview-guide.md).
The installed app also passes a scoped owned-source ten-second recording,
live EQ/Undo, save/reopen and independently checked WAV export workflow.
Recording startup/clock diagnostics, full desktop/menu and physical/sustained
recording qualification remain open.
Single-track code now retains [portable recording error details](docs/92-portable-recording-faults.md)
for reopening. The [latest local Ubuntu candidate](docs/93-installed-portable-fault-preview.md)
passes an installed fault/Save/Quit/reopen/review workflow. Sustained physical
recording and native Windows qualification remain open. The same binary
now passes [normal 11.264-second recording, live EQ/Undo, save/reopen and verified
WAV export](docs/94-installed-normal-preview.md).

This is the DAW repository. [soundcurrent-eq](https://github.com/rhamenator/soundcurrent-eq)
is the free equalizer; `soundcurrent-studio` is the separate premium equalizer.
Borrowed components have pinned provenance and are adapted here without changing
those repositories.

## What works today

- Versioned portable project state, stable IDs, strict validation, worker-side
  save/reopen and typed/grouped Undo/Redo.
- Prepared in-process EQ, smoothing, bounded live parameter events and
  floating-point headroom.
- WAV/RF64 recording, read-ahead playback, verified take admission and checkpoint
  recovery, and offline WAV export through the shared processing engine.
- An opt-in Qt desktop with tracks/timeline, explicit Linux PipeWire routes,
  live EQ, colored meters, master channel matrix and simultaneous armed recording
  with project playback.
- Saved punch locators and grouped takes, with short 32-track owned-native desktop
  evidence for raw samples, live EQ output, Undo/Redo and Save/reopen.
- Equipment profile browsing, imports and editable custom copies from a pinned
  1,092-speaker catalog. Monitoring/printing correction and portable profile pins
  still need implementation.

These workflows have scoped [test receipts](tests/results/). Sustained native
recording failures remain open. Synthetic or short owned-route success does not
establish physical latency, a supported Windows application, or full DAW parity.
The [changelog](CHANGELOG.md) summarizes the development snapshot.
The [localization refresh](docs/100-localization-reuse-refresh.md) adds reviewed
equipment/settings vocabulary and signed RTL input; 33 language catalogs remain
partial drafts with English fallback, awaiting native-speaker review.

The product targets lower-budget recording studios, including studios with
substantial hardware. [Track scalability](docs/67-track-scalability.md) requires
no fixed product/license ceiling on total project tracks, with separately measured
real-time and hardware-input capacity. The [first scaling implementation](docs/78-resource-admitted-projects.md)
removes the fixed total-track ceiling from the model, parser, mixer and desktop
Add Track control. Synthetic 4,096-track core and Linux Qt workflows have scoped
evidence. A [shared media pool/cache](docs/79-shared-media-cache.md) now serves
1,024 file-backed tracks with one handle in a bounded exact-output test.
[Undo resource settings](docs/81-history-resource-admission.md) let the desktop
raise retained command, payload and operation-workspace budgets. Existing history
survives rejected reductions; usage and oldest-command retirement are visible.
These declared checks are one input to future combined memory admission.
The [retained-session ledger](docs/82-retained-session-resources.md) also keeps
older immutable snapshots charged until their last reader releases them. Saves
and barriers share current state; snapshot-budget refusal preserves active edits
and leaves Cancel available. [Shared controller admission](docs/83-controller-memory-resources.md)
now also charges canonical state, history and declared edit work against one
parent. **Edit → Project resources…** exposes persistent adjustable parent/snapshot
policies and live usage. [GUI payload admission](docs/84-gui-memory-resources.md)
now leases selected-track copies, lists and timeline indices from that parent.
A refused display retains its old view with resource settings and explicit retry.
[Prepared execution admission](docs/85-graph-memory-resources.md) extends that
parent to DSP/playback pools, reader buffers, shared caches and WAV output buffers;
old/new graphs retain credit through control-side retirement.
[Coordinated execution policy](docs/86-execution-memory-policy.md) makes new desktop
playback/recording/export preparation follow the trusted parent and adds declared
capture pools, take banks, bridge bindings and writer workspace. Copied owner state,
other IO, exact allocator/RSS and measured sustained capacity remain required.

The [viewport timeline and model-backed lists](docs/80-virtualized-session-views.md)
now have bounded Linux Qt large-project regression workflows. Combined memory,
recording, meters/waveforms, freeze/bounce and sustained Linux/Windows workloads
remain staged.

## Build on Linux

Core dependencies: a C++20 compiler, CMake >=3.20, OpenSSL 3 Crypto development
files, and libsndfile 1.2.2. JSON and the libsndfile API header are vendored with
[third-party notices](THIRD-PARTY-NOTICES.md). Desktop builds also need Qt >=6.4
Core/Gui/Widgets; fixtures need Qt Test. Ninja is used in these examples.

Native Linux audio requires pkg-config and **PipeWire development headers >=1.6.2**
with an existing user daemon. Build products are kept in ignored `.cache/`.

```sh
cmake -S . -B .cache/build-desktop -G Ninja \
  -DCMAKE_BUILD_TYPE=Debug -DSC_BUILD_DESKTOP=ON
cmake --build .cache/build-desktop --parallel 2
ctest --test-dir .cache/build-desktop --output-on-failure --parallel 1
.cache/build-desktop/soundcurrent-daw
```

For development machines without that PipeWire version, add
`-DSC_BUILD_PIPEWIRE=OFF`. This permits editor, synthetic and offline workflows;
**native playback and recording are unavailable in that build**. No PipeWire fork
or replacement daemon is bundled. With `SC_BUILD_DESKTOP=OFF` (the default), the
core build requires no Qt. `SC_BUILD_MEDIA=OFF` selects state/engine/transport only.

CTest does not connect physical audio devices. Native integration fixtures are
opt-in and must run serially after builds and tests finish.

## Desktop recording workflow

Create/open a project, select or create a track, prepare recording and explicitly
choose its input/output routes. Arm the selected track and use Record/Stop.
Successful raw takes are verified before attachment. To overdub multiple tracks,
enable **Record armed tracks with project playback**, select arms, prepare and
choose all packed inputs/master outputs explicitly. Punch settings are captured
at preparation and cannot change while the prepared session is active.

Live scalar EQ edits and Undo use applied-frame receipts. File → Recover
recording… previews/copies an owned checkpoint; File → Export WAV… renders an
immutable project snapshot with range, cancellation and overwrite confirmation.
Closing waits for recording/export finalization before Save/Discard/Cancel.

See [desktop recording](docs/22-desktop-recording.md),
[multi-arm workflow](docs/37-desktop-duplex-recording.md),
[punch controls](docs/54-project-punch-controls.md), and
[export](docs/24-desktop-export.md) for bounds and remaining work.

## Developer tools

```sh
.cache/build-desktop/sc-project-tool new .cache/example-project
.cache/build-desktop/sc-project-tool inspect .cache/example-project
.cache/build-desktop/sc-record-tool synthetic .cache/new-synthetic-project
.cache/build-desktop/sc-export-tool render-mix PROJECT_DIRECTORY MIX.wav
```

`synthetic` generates input and uses the real disk worker; it refuses an existing
project directory. Export refuses existing targets without explicit hash-based
consent. These commands are development tools, not a complete user installation.
See [recording/recovery](docs/13-recording-contract.md) and
[offline export](docs/23-offline-export.md).

## Windows source and development status

Clone the same repository on Windows:

```powershell
git clone https://github.com/rhamenator/soundcurrent-daw.git
Set-Location soundcurrent-daw
```

Native MSVC/Qt Windows desktop and owned WASAPI workflows now have separate
[acceptance evidence](docs/99-windows-desktop-workflow.md). A per-user Windows 11
x64 installer and matching GPL/dependency sources are being prepared locally;
[clean-installed acceptance](docs/101-windows-installer-preparation.md) is a separate gate.
On a Linux host with MinGW:

```sh
cmake -S . -B .cache/build-windows-core -G Ninja \
  -DCMAKE_TOOLCHAIN_FILE=cmake/toolchains/windows-mingw.cmake \
  -DSC_BUILD_MEDIA=OFF -DCMAKE_BUILD_TYPE=Release
cmake --build .cache/build-windows-core --parallel 2
```

`scripts/build_windows_media.py` additionally builds hash-pinned libsndfile 1.2.2
for the headless disk layer; it does not qualify Windows runtime or deploy all
runtime dependencies. The planned native adapter uses user-mode WASAPI. Source
copies and unsigned local development do not require buying a signing certificate.
Trusted distribution and installers remain separate gates; see
[Windows/localization](docs/08-platforms-and-localization.md) and
[signing budget](docs/49-windows-signing-budget.md).

## Plan, evidence and next task

- [Scope and frozen baseline](docs/00-scope-baseline.md)
- [Source-linked parity matrix](docs/01-parity-matrix.md) · [machine-readable requirements](research/parity.json)
- [Architecture and real-time threading contracts](docs/02-architecture.md)
- [Dependencies and licenses](docs/03-dependencies.md) · [decision records](docs/decisions/)
- [Milestones and measurable exit criteria](docs/04-roadmap.md) · [first-slice gates](docs/05-first-slice.md)
- [Other-suite project import](docs/10-project-import.md) · [equipment profiles/editor](docs/17-equipment-profiles.md)
- [Continuing equalizer reuse audit](docs/25-equalizer-reuse-updates.md)
- [European language coverage audit](docs/47-europe-language-inventory-audit.md)
- [Native desktop punch evidence](docs/56-native-desktop-punch.md) · [punch interruption/recovery](docs/59-native-punch-fault-recovery.md)
- [Main branch protection and PR workflow](docs/58-repository-branch-protection.md)
- [Track scalability without a fixed product ceiling](docs/67-track-scalability.md)
- [Complete documentation index](docs/README.md) · [dated test results](tests/results/)

Saved per-track [input-latency controls](docs/60-input-latency-controls.md) now connect
canonical project state to accepted recording preparation and take alignment.
Saved [recording-only Auto monitoring](docs/61-auto-recording-monitoring.md) selects live
input inside desired punch boundaries while preserving continuous EQ history.
Prepared [deferred capture starts](docs/62-deferred-capture-start.md) now let audio
publish an exact one-shot origin before disk startup, with immutable worker binding.
The [manual punch engine owner](docs/63-manual-punch-engine-owner.md) now preserves
continuous playback/EQ across replenishable takes, reliable commands and delayed
postroll, with scoped synthetic disk/concurrency acceptance.
The [manual recording control owner](docs/64-manual-recording-control-owner.md) now
starts and joins consumers, verifies bounded groups and handles empty/faulted takes.
The [native manual owner](docs/65-native-manual-recording.md) now has finite
32-arm repeated-take and late-service acceptance on owned PipeWire routes.
[Default exports](docs/66-repeatable-export.md) are repeatable across render seconds.
[Native manual fault/recovery tests](docs/68-native-manual-fault-recovery.md) now
expose retained channel-alignment and callback-deadline failures alongside finite
owned-route successes. [Priority Stop/Cancel](docs/70-manual-priority-interruption.md)
now terminates audio independently of held disk startup, with scoped native Linux
evidence. [Public native buffer handoff observations](docs/71-native-port-handoff.md)
now retain IO/buffer startup state beside independent waveform markers. One new
finite Stop passes; original46's delayed channel remains unresolved.
[A controlled startup experiment](docs/72-controlled-native-startup.md) now reproduces
a one-cycle delay when an allocated buffer is queried before IO readiness;
deferring that query prevents the measured mechanism. Production readiness and
capacity-aware acquisition retain logical port tracing. The
[manual recording desktop panel](docs/76-manual-recording-panel.md) now connects
finite repeated takes, explicit routing, EQ updates, preview adoption and priority
Stop/Cancel to Qt controls. [Owned native desktop evidence](docs/77-native-manual-panel.md)
now checks repeated groups on3/32 inputs, live EQ, adoption/retry and Stop/Cancel/Close.
Valid native silence is handled without exposing stale backing bytes. A separate
active clock gap under sanitizers remains unresolved; sustained/physical and Windows
runtime qualification remain open. Additional monitor
policies, tempo/loop/take lanes and comping remain required. Native engine
punch fault recovery now has scoped evidence; desktop fault/discovery, process-kill
and empty-preroll workflows remain required. Sustained native recording, independent Windows qualification and
all other frozen-reference requirements remain required. The first recording
slice still has open independent acceptance gates.

## Contributing, security and license

See [CONTRIBUTING.md](CONTRIBUTING.md) and [SECURITY.md](SECURITY.md). GitHub CI
builds/tests Linux synthetic desktop workflows without a native audio daemon and
cross-compiles Windows core targets. These checks do not claim native device or
Windows runtime qualification. No hosted release binaries are published yet.

[GPL-3.0-only](LICENSE) permits paid distribution while preserving recipients'
license rights and corresponding-source obligations. Borrowed code/data retain
notices and provenance; proprietary assets and algorithms are not assumed
reusable. No dual-license rights are implied.

GitHub backs up tracked source, history, documentation and committed receipts.
Build caches, local recordings and ignored failure executables/media are excluded;
back those up separately if needed. [Windows copy and backup instructions](docs/57-repository-backup.md).

The development [foreign-project inspection](docs/120-import-property-preview.md)
shows original REAPER property values and preserved source lines, including
missing/unsupported state. Saved inspections reopen without the original file;
importing their audio into a destination project remains under development.

### Editable clip gain and fades (development source)

Select a clip and expand **Clip gain and fades** to set gain, mute, polarity and
linear/equal-power/smoothstep fades. Apply is one Undo/Redo item; save/reopen
preserves exact settings. Split/trim retain the source envelope and raw recordings
stay unchanged. The shared playback/export reader preserves float headroom.
Controls are read-only while audio is prepared; live clip automation, timing/rate/
pitch and refreshed installed packages remain separate work. See
[the workflow, evidence and remaining gaps](docs/129-clip-gain-and-fades.md).
