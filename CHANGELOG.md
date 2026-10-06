# Changelog

## Unreleased — development preview

- C++20 framework-independent session, processing, capture and playback layers.
- Prepared in-process EQ, bounded live parameter events and float headroom.
- Versioned projects, grouped Undo/Redo, worker-side WAV/RF64 recording, verified
  recovery, and offline export.
- Opt-in Qt desktop editor with Linux PipeWire playback/recording, explicit
  routing, master matrix, armed tracks and saved punch locators.
- Saved per-track input-latency controls, preparation/attachment alignment,
  strict schema 1.5 migration and Undo/Redo.
- Equipment profile browsing/import/editor with pinned provenance; audio
  correction routing is still required.
- Linux synthetic, sanitizer and short owned-native test evidence; Windows core
  cross-build evidence. Native Windows audio and installers remain unqualified.
- Frozen Bitwig Studio 6.1.3 / Cubase Pro 15.0.30 parity plan, all-Europe language
  inventory, and other-suite import requirements. These are product targets;
  full parity and reviewed translations have not been delivered.

Detailed dated results are in `tests/results/`; decisions and remaining gates are
in `docs/`. CMake's `0.1.0` is a development version, not a released product.
