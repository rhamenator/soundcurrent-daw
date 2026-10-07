# Changelog

## Unreleased — development preview

- Refresh four noncompiled equalizer DSP review references for the upstream
  quieter post-gain wrapper. Borrowed coefficient/recurrence equations and the
  other 20 inputs remain unchanged; compiled DAW behavior is unchanged.

- Shared project parent leases for selected-track projections, list/decorations
  and timeline interval/query arrays. Staged display replacement preserves the
  previous view on refusal; Project resources and Retry project display allow
  recovery. Initial monitoring/arming preserve requested accepted-prefix state.
  Exact allocation/RSS and graph/cache/IO admission remain open.

- Shared controller parent budget for canonical state, history, retained snapshots
  and declared edit work; atomic parent/child limits and allocation-free credit
  transfers preserve full-budget Cancel. The scrollable Project resources dialog
  persists adjustable memory policies and reports refusal/preferences failures.
  Further graph/cache/IO and exact allocation/RSS admission remain open.

- Shared leases retain immutable Session charges until the last reader releases
  them. Saved revisions, in-flight saves and barriers share current state; edits
  and Undo/Redo admit their next publication first, with Cancel still available
  at a full snapshot budget. Full combined resource admission remains open.

- Configurable Undo command/payload/workspace limits with checked charges,
  observable usage/retirement and a desktop resource dialog. Rejected reductions
  preserve Undo/Redo; active-gesture preflight and preference-failure reporting
  preserve project state. Full combined graph/GUI/IO admission remains open.

- Snapshot-backed track, arm, destination, media and clip selectors; viewport
  timeline with visible-row/horizontal interval queries and stable-ID selection.
  Large-project Linux Qt regressions preserve media, Undo/Redo and Save/reopen;
  combined GUI/history memory, paging and native Windows qualification remain open.
- Require every recording input/monitor route before enabling Record or admitting
  a Start click; reconcile asynchronous route updates before the readiness check.

- Shared resource-admitted media handles and decoded pages across track readers.
  Exact file-backed output tests cover 1,024 tracks sharing one asset/handle and
  96 distinct assets with two handles. Live/offline readers retain frame coordinates
  and per-occurrence nonfinite accounting; sustained/native capacity remains open.

- Resource-admitted total project tracks: trusted state/parser/DSP byte policies,
  indexed preparation and dynamic callback masks replace the fixed 256-track
  model/parser/mix/UI ceiling. Scoped synthetic 4,096-track core and Linux Qt
  Add 4,097/Undo/Redo/Save/reopen evidence; schema 1.7 retains old migrations.
  Large recording, full media/GUI scaling and sustained native
  Linux/Windows workloads remain incomplete.
- C++20 framework-independent session, processing, capture and playback layers.
- Prepared in-process EQ, bounded live parameter events and float headroom.
- Versioned projects, grouped Undo/Redo, worker-side WAV/RF64 recording, verified
  recovery, and offline export.
- Opt-in Qt desktop editor with Linux PipeWire playback/recording, explicit
  routing, master matrix, armed tracks and saved punch locators.
- Saved per-track input-latency controls, preparation/attachment alignment,
  strict schema 1.5 migration and Undo/Redo.
- Recording-only Auto monitoring, sample-exact file/live selection before
  continuous track EQ, and strict schema 1.6 mode persistence.
- Equipment profile browsing/import/editor with pinned provenance; audio
  correction routing is still required.
- Linux synthetic, sanitizer and short owned-native test evidence; Windows core
  cross-build evidence. Native Windows audio and installers remain unqualified.
- Frozen Bitwig Studio 6.1.3 / Cubase Pro 15.0.30 parity plan, all-Europe language
  inventory, and other-suite import requirements. These are product targets;
  full parity and reviewed translations have not been delivered.

Detailed dated results are in `tests/results/`; decisions and remaining gates are
in `docs/`. CMake's `0.1.0` is a development version, not a released product.
