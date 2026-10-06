# SoundCurrent DAW

Linux-first professional digital audio workstation and recording suite, planned in C++20 with CMake and Qt 6. Licensed GPL-3.0-only by the owner's decision on 2026-10-05.

**Status: early implementation.** Qt-free session/state, prepared in-process EQ, headless recording/recovery, shared audio bridge and bounded file playback build on Linux and cross-compile for Windows. A developer command records synthetic input; an opt-in native PipeWire fixture now records and monitors between owned nodes. The desktop application and release remain ahead. The product goal remains the combined functional capabilities of full Bitwig Studio and Cubase Pro, with Windows, all-Europe localization, and other-suite project import requirements. Completing the first recording slice will not establish that parity. The full active objective is preserved in [GOAL.md](GOAL.md).

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

Baseline: **Bitwig Studio 6.1.3 (full edition)** and **Cubase Pro 15.0.30**, frozen 2026-10-05. Every matrix row is planned, with explicit reference uncertainty; none is reported as implemented or equivalent.

## Next implementation task

Continue **M1 / SLICE-001** with **S6: the first Qt recording/playback/EQ UI**, then transactional offline WAV export. S6a adds bounded file read-ahead, source/timeline mapping, private live EQ and control-side seek retirement; owned native playback/sink-removal fixtures pass. S6b adds bounded immediate edits and applied-frame receipts through recording/playback; the Qt/controller workflow remains to be implemented. S5 hardware latency, reprepare/reconnect and Windows audio remain open, alongside S4 filesystem/>4 GiB and normal PipeWire unload-memory gates. See [the acceptance contract](docs/05-first-slice.md).

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

## Licensing and distribution

Paid GPL distribution is an option, with corresponding source and license rights preserved. Existing SoundCurrent code retains its copyright and GPL terms. Dependencies, samples, model weights, fonts, codecs, and proprietary plugin binaries require independent inventory; a permissive SDK does not grant rights to vendor content. See [ADR-001](docs/decisions/001-license-and-repository.md).
