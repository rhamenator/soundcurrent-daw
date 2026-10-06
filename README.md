# SoundCurrent DAW

Linux-first professional digital audio workstation and recording suite, planned in C++20 with CMake and Qt 6. Licensed GPL-3.0-only by the owner's decision on 2026-10-05.

**Status: early implementation.** Qt-free session/state, prepared in-process EQ, headless recording/recovery, shared audio bridge and bounded file playback build on Linux and cross-compile for Windows. A developer command records synthetic input; an opt-in native PipeWire fixture now records and monitors between owned nodes. An opt-in Qt preview supports asynchronous create/open/save, scalar EQ undo and Linux selected-track playback with explicit outputs, live EQ receipts and a colorized peak meter. The desktop now connects selected-track input/arm/record/monitoring and manual take recovery. A worker-side float WAV/RF64 exporter and desktop range/overwrite/progress/cancel workflow now exist; a qualified release remains ahead. The product goal remains the combined functional capabilities of full Bitwig Studio and Cubase Pro, with Windows, all-Europe localization, and other-suite project import requirements. Completing the first recording slice will not establish that parity. The full active objective is preserved in [GOAL.md](GOAL.md).

`soundcurrent-studio` already contains the premium equalizer. This separate repository is named `soundcurrent-daw` to preserve that work. It is local, has no remote, and has not been published or pushed.

## Plan

1. [Scope and frozen reference baseline](docs/00-scope-baseline.md)
2. [Source-linked parity matrix](docs/01-parity-matrix.md) and [machine-readable requirements](research/parity.json)
3. [Engine architecture and threading contracts](docs/02-architecture.md)
4. [Dependency and license decisions](docs/03-dependencies.md)
5. [Staged backlog and exit criteria](docs/04-roadmap.md)
6. [First implementation slice](docs/05-first-slice.md)
7. [Equalizer reuse audit](docs/06-reuse-audit.md)
8. [Feasibility evidence and limitations](docs/07-feasibility.md)
9. [Decision records](docs/decisions/)
10. [Windows and all-Europe localization](docs/08-platforms-and-localization.md)
11. [Implemented session-state contract](docs/09-session-state-contract.md)
12. [Other-suite native project and exchange imports](docs/10-project-import.md)
13. [Prepared EQ and real-time transport](docs/11-engine-contract.md)
14. [Capture, disk worker and recovery](docs/13-recording-contract.md)
15. [Shared audio bridge and native PipeWire](docs/14-native-audio-contract.md)
16. [Read-ahead and take playback](docs/15-playback-contract.md)
17. [Immediate controls and applied-frame receipts](docs/16-immediate-controls.md)
18. [Equipment profiles and profile editor requirement](docs/17-equipment-profiles.md)
19. [Desktop project controller and editor](docs/18-desktop-controller.md)
20. [Native playback owner and clock bridge](docs/19-native-playback-owner.md)
21. [Desktop playback and live EQ](docs/20-desktop-playback.md)
22. [Desktop recording and manual recovery](docs/22-desktop-recording.md)
23. [Offline WAV export core](docs/23-offline-export.md)
24. [Desktop snapshot export](docs/24-desktop-export.md)
25. [Continuing equalizer reuse updates](docs/25-equalizer-reuse-updates.md)
26. [Portable per-channel project routing](docs/26-project-routing.md)
27. [Saved recording-monitor preferences](docs/27-monitoring-preferences.md)
28. [Recording-job discovery and recovery](docs/28-recording-discovery.md)
29. [Transactional track and clip edits](docs/29-multitrack-edits.md)
30. [Desktop timeline and selected-track workflow](docs/30-desktop-timeline.md)
31. [Shared-clock multitrack EQ and mix](docs/31-multitrack-mix.md)

Baseline: **Bitwig Studio 6.1.3 (full edition)** and **Cubase Pro 15.0.30**, frozen 2026-10-05. Every matrix family remains unqualified, with explicit reference uncertainty and scoped evidence for partial workflows. None is reported as equivalent.

## Next implementation task

The owner added expanded equipment profiles/import and a profile editor as **X005**; [its contract](docs/17-equipment-profiles.md) includes source/rights, editable curves, save-copy prompts and monitor-versus-print routing. The Qt preview now includes **Equipment → Profile library and editor…**, a searchable pinned 1,092-speaker catalog with subtype and active/passive filters, JSON/response imports, curve/filter editing, local undo/redo and saved custom copies. Monitoring/print routing and portable project profile pins remain required.

Continue **M2 multitrack foundations**. [M2a typed editing](docs/29-multitrack-edits.md) now supplies grouped track/clip operations, bounded mixed Undo/Redo and verified take-admission Undo; developer CLI commands expose persisted edits. [M2b](docs/30-desktop-timeline.md) exposes desktop track/clip selection, exact-range/split/move edits and selected single-track preparation without changing canonical order. [M2c1](docs/31-multitrack-mix.md) adds the Qt-free shared-clock EQ/matrix graph, fair read-ahead worker and static multitrack WAV/CLI export. Next: native owner/desktop master-output integration, then one playback/capture clock for simultaneous overdub. Punch/loop, takes/comping, fades and the remaining M2 workflows stay required. **M1 / SLICE-001** remains incomplete on its independent native Windows, physical, filesystem, load/deadline and normal module-unload gates; S8 routing, native corpus, monitoring persistence and recovery discovery now have scoped evidence. [S7b desktop export](docs/24-desktop-export.md) now connects immutable accepted-prefix capture, track/range/tail/RF64 selection, worker-side destination inspection/consent, progress/cancel and safe close. [S7a](docs/23-offline-export.md) now exports selected ranges through a private shared EQ instance with preroll, tails, cancellation and completed-file publication. S6g connects Linux desktop input/arm/record/monitoring, verified take attachment and manual recovery; [the desktop recording contract](docs/22-desktop-recording.md) records evidence and limitations. S5 hardware latency, reprepare/reconnect and Windows audio remain open, alongside S4 filesystem/>4 GiB and normal PipeWire unload-memory gates. See [the acceptance contract](docs/05-first-slice.md).

## Desktop development preview

```sh
cmake -S . -B .cache/build-desktop -G Ninja -DCMAKE_BUILD_TYPE=Debug -DSC_BUILD_DESKTOP=ON
cmake --build .cache/build-desktop
ctest --test-dir .cache/build-desktop --output-on-failure
.cache/build-desktop/soundcurrent-daw PROJECT_DIRECTORY
```

Requires Qt6 Core/Gui/Widgets development files (Test for fixtures), in addition to the core dependencies. This preview edits project state with asynchronous file operations, grouped scalar undo, scrollable controls and dirty-close prompts. For a project containing audio, select its track in **Tracks and timeline**, choose **Prepare playback**, select every output explicitly, then **Play**. For recording, choose monitoring Off/Post-EQ, **Prepare recording**, select every required input/output, **Arm selected track**, then **Record**. Stop or unarm finalizes and verifies the raw take before attachment. File → Recover recording… previews/copies an owned checkpoint. Linux recording/playback/live EQ/undo/meters are connected; **File → Export WAV…** selects a track/range and renders an immutable snapshot, including unsaved EQ edits; existing targets require explicit confirmation. Progress/Cancel stay visible in the status bar. [The playback contract](docs/20-desktop-playback.md) records exact evidence and limitations. The default Windows native backend is unavailable and its Qt GUI remains unqualified. The default build remains Qt-free.

## Offline export developer workflow

The new `render-mix` command exports every saved track with matching-layout unity
routes through the shared multitrack graph. Custom channel matrices are available
in the core API; mixed-layout GUI routing is still required.

```sh
.cache/build-desktop/sc-export-tool render PROJECT_DIRECTORY OUTPUT.wav
.cache/build-desktop/sc-export-tool render-mix PROJECT_DIRECTORY MIX.wav
.cache/build-desktop/sc-export-tool render PROJECT_DIRECTORY OUTPUT.wav --start 137 --end 12003 --tail
python3 tests/verify_export_cli.py
```

Exports the saved first track to float WAV/RF64 with the same in-process EQ and selected-range preroll. Existing destinations are refused unless their current content has been explicitly approved with `--replace-sha256 CONFIRMED_HASH`; `fingerprint FILE.wav` prints that hash. SIGINT/SIGTERM cancel before publication. The core is independent of Qt/audio backends; desktop export controls are available through File → Export WAV…. [Contract and limits](docs/23-offline-export.md).

## Build and test the current core

Requires C++20, CMake ≥3.20, OpenSSL 3 Crypto development files and a libsndfile1.2.2 runtime/library on Linux. JSON and the libsndfile API header are vendored with notices. The default Linux build also needs pkg-config and PipeWire development headers ≥1.6.2; it opens no audio device during CTest. Use `-DSC_BUILD_PIPEWIRE=OFF` for a headless build without that dependency, and `-DSC_BUILD_MEDIA=OFF` explicitly for state/engine/capture transport without disk writing.

```sh
cmake -S . -B .cache/build-core -DCMAKE_BUILD_TYPE=Debug
cmake --build .cache/build-core
ctest --test-dir .cache/build-core --output-on-failure
.cache/build-core/sc-project-tool new .cache/example-project
.cache/build-core/sc-project-tool inspect .cache/example-project
```

The project tool creates an empty session. The recording tool exercises a real disk worker with generated buffers (no device/audio route):

```sh
.cache/build-core/sc-record-tool synthetic .cache/new-synthetic-project
.cache/build-core/sc-record-tool inspect .cache/new-synthetic-project/media/capture-ASSET_UUID
.cache/build-core/sc-record-tool recover PROJECT_DIRECTORY CAPTURE_JOB_DIRECTORY
python3 tests/verify_record_cli.py
```

Use the actual asset UUID created under `media/`. Synthetic refuses an existing project directory. Recovery copies a verified checkpoint into a new asset and saves it in the matching project. Inspect before explicitly requesting recovery.

The native integration test is opt-in and uses an existing user PipeWire daemon. It selects only owned source/track/sink ports and checks existing links and defaults before/during/after:

```sh
python3 tests/verify_pipewire_fixture.py
```

It records ten seconds of synthetic audio through real PipeWire, then tests source removal. No physical input/output link is created. This is separate from device latency, deadline and Windows qualification.

The playback command reopens and checks the first track without audible output, writes or export:

```sh
.cache/build-core/sc-play-tool verify PROJECT_DIRECTORY
python3 tests/verify_play_cli.py
python3 tests/verify_pipewire_fixture.py \
  --binary .cache/build-core/sc-pipewire-playback-fixture
```

The last command is an opt-in, owned PipeWire file-playback/sink-removal test; run native fixtures serially. [Playback contract](docs/15-playback-contract.md) explains range selection, underflow and seek/worker limits.

Windows state/engine/transport-only cross-build:

```sh
cmake -S . -B .cache/build-windows-core \
  -DCMAKE_TOOLCHAIN_FILE=cmake/toolchains/windows-mingw.cmake -DSC_BUILD_MEDIA=OFF
cmake --build .cache/build-windows-core
```

For the full current headless disk layer, explicitly build the pinned libsndfile dependency and link it:

```sh
python3 scripts/build_windows_media.py
```

This Linux development script downloads a hash-verified upstream1.2.2 archive into ignored cache, disables external/MPEG codecs, builds a DLL/import library and the DAW headless targets, and copies the DLL beside the executables. It installs nothing and publishes nothing. Requires MinGW, Ninja and `/usr/bin/python3`; remaining compiler runtime DLLs and notices require deployment inventory. The resulting `.exe` files have not yet been run on Windows. Test evidence is in [SLICE-001 results](tests/results/SLICE-001/). See [third-party notices](THIRD-PARTY-NOTICES.md).

## Reproduce the bounded experiment

The optional probe consumes the existing GPL Studio SDK from a read-only checkout at commit `65151a8fec1aa4b4e6c283e4514d7f3d9166fe2b`. It is not bundled or fetched automatically. Verify that commit before rebuilding.

```sh
cmake -S experiments -B .cache/eq-probe \
  -DSOUNDCURRENT_SDK_SOURCE=/home/rich/dev/soundcurrent-studio \
  -DCMAKE_BUILD_TYPE=Release
cmake --build .cache/eq-probe --target eq-feasibility -j 4
.cache/eq-probe/eq-feasibility .cache/new-probe.wav
```

The probe generates no audible output and does not alter audio routing. Build products and raw reference documents are ignored. Source manifests retain canonical links and content hashes; vendor documentation is not redistributed here.

## Multitrack editing foundation

Developer track/clip edits now use the same Qt-free transactional command model as the canonical project controller. See [M2a operations, history and limits](docs/29-multitrack-edits.md). Raw takes are preserved; verified take admission can now be undone and redone. Desktop track/timeline controls and multitrack transport remain in progress.

## Licensing and distribution

Paid GPL distribution is an option, with corresponding source and license rights preserved. Existing SoundCurrent code retains its copyright and GPL terms. Dependencies, samples, model weights, fonts, codecs, and proprietary plugin binaries require independent inventory; a permissive SDK does not grant rights to vendor content. See [ADR-001](docs/decisions/001-license-and-repository.md).

The [native recording owner](docs/21-native-recording-owner.md) now supplies explicit input/monitor routing, raw capture, ordered finalization/recovery and asynchronous verified take attachment. It is exercised through native integration fixtures; S6g now connects GUI Record/input/arm/monitor/manual recovery controls; desktop snapshot export now exists; native roundtrip/portability now has scoped evidence; independent platform, physical and durability gates remain open. Equipment profiles still apply no audio correction.

The Linux desktop now has **Prepare recording / Arm / Record / Stop**, explicit input and optional post-EQ monitoring selection, colored input/monitor peaks, live scalar EQ/undo, raw-take attachment, and **File → Recover recording…** with preview/copy. [Desktop recording contract](docs/22-desktop-recording.md). Closing waits for take finalization/verification before Save/Discard/Cancel. Core/desktop export now exists. Typed input/playback/monitor routing intent is saved with strict named-placeholder restoration; Off/Post-EQ monitoring preferences now persist with shared Undo/Redo and passive restore; preparation captures pending edits through an accepted-prefix barrier. Opening a project now discovers stored recording metadata on a separate worker, with a scrollable review list, activity/legacy diagnostics and verified preview/consent/copy; [contract](docs/28-recording-discovery.md). Simultaneous overdub and native Windows qualification remain open.

M2c2 connects the [shared-clock mix to native Linux playback and desktop controls](docs/32-native-mix-playback.md). Enable **Mix all tracks (matching channel layouts)**, prepare, explicitly select outputs, then Play. The inspector can edit any mixed track without retargeting audio. A dedicated persisted master matrix and simultaneous recording/overdub remain the next tasks; full parity is unqualified.
