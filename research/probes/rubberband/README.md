# Rubber Band candidate probe

Experimental code under the repository GPL-3.0-only license. This is separate
from the application build and does not adopt Rubber Band into the DAW.
See [feasibility and next contract](../../../docs/133-pitch-stretch-feasibility.md)
and [exact evidence](../../../tests/results/M2/2026-10-09-clip-playback-rate/stretch-feasibility-qualification.json).

To reproduce, download the official rubberband-4.0.0.tar.bz2 URL recorded in the
receipt and verify its SHA256 before extracting. Place the unmodified upstream
root beside `probe/` as `rubberband-4.0.0/`; preserve its COPYING/notices. Build
`probe/` independently with CMake Release, then run the probe with a60s process
budget. Use the root Windows MinGW toolchain for cross-build only. Native Windows
execution has not been qualified. The upstream dependency is not vendored here.

The60s budget and measured RSS are experiment observations, not a realtime
callback guarantee or allocation bound. All generation/processing is synthetic;
no audio endpoints, microphone, desktop app or VM are used.
