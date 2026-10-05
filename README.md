# SoundCurrent DAW

Linux-first professional digital audio workstation and recording suite, planned in C++20 with CMake and Qt 6. Licensed GPL-3.0-only by the owner's decision on 2026-10-05.

**Status: early implementation.** Qt-free session/state and prepared in-process EQ foundations build on Linux and cross-compile for Windows. No recording application or release exists yet. The product goal remains the combined functional capabilities of full Bitwig Studio and Cubase Pro, with Windows, all-Europe localization, and other-suite project import requirements. Completing the first recording slice will not establish that parity. The full active objective is preserved in [GOAL.md](GOAL.md).

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

Baseline: **Bitwig Studio 6.1.3 (full edition)** and **Cubase Pro 15.0.30**, frozen 2026-10-05. Every matrix row is planned, with explicit reference uncertainty; none is reported as implemented or equivalent.

## Next implementation task

Continue **M1 / SLICE-001** with **S4: capture slabs, disk worker, RF64/WAV and recording journal**, including queue/disk failure and durable-prefix recovery fixtures. S1/S2 state and S3 processor foundations are implemented. Actual recording, native audio, UI and WAV export remain ahead. See [the acceptance contract](docs/05-first-slice.md).

## Build and test the current core

Requires C++20, CMake ≥3.20 and OpenSSL 3 Crypto development files on Linux. JSON 3.12.0 is vendored with its MIT notice. No Qt or audio device dependency enters this build.

```sh
cmake -S . -B .cache/build-core -DCMAKE_BUILD_TYPE=Debug
cmake --build .cache/build-core
ctest --test-dir .cache/build-core --output-on-failure
.cache/build-core/sc-project-tool new .cache/example-project
.cache/build-core/sc-project-tool inspect .cache/example-project
```

The developer tool creates an empty one-track session; it does not record. Windows cross-build:

```sh
cmake -S . -B .cache/build-windows-core \
  -DCMAKE_TOOLCHAIN_FILE=cmake/toolchains/windows-mingw.cmake
cmake --build .cache/build-windows-core
```

This requires the installed x86_64 MinGW toolchain. The resulting `.exe` files have not yet been run on Windows. Test evidence is in [SLICE-001 results](tests/results/SLICE-001/). See [third-party notices](THIRD-PARTY-NOTICES.md).

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
