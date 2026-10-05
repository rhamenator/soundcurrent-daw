# SoundCurrent DAW

Linux-first professional digital audio workstation and recording suite, planned in C++20 with CMake and Qt 6. Licensed GPL-3.0-only by the owner's decision on 2026-10-05.

**Status: planning and bounded feasibility only. No DAW application or release exists yet.** The product goal is the combined functional capabilities of full Bitwig Studio and Cubase Pro. Completing the first recording slice will not establish that parity.

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

Baseline: **Bitwig Studio 6.1.3 (full edition)** and **Cubase Pro 15.0.30**, frozen 2026-10-05. Every matrix row is planned, with explicit reference uncertainty; none is reported as implemented or equivalent.

## Next implementation task

Implement **M1 / SLICE-001: one mono track, PipeWire capture, in-process EQ, atomic save/reopen, and WAV export**, following [the acceptance contract](docs/05-first-slice.md). Begin with the framework-independent session model and a synthetic capture fixture, then the disk worker and native adapter. No clip launcher, plugin host, or broad effects work belongs in this slice.

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
