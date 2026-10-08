# Changelog

## Unreleased — development preview

- Native Windows desktop playback and single-track recording factories, stable
  device/channel route identities, project sample-rate selection and visible
  monitoring limits. An owned native GUI record/EQ/Undo/play/save/reopen/WAV workflow
  passes independent media checks with a silent lead-in. Non-silent startup,
  Windows monitoring/duplex and the installer remain unqualified.
- Late recording-writer failures are reported independently of completed audio
  processing, preserving other finalized lanes and the failed writer's checkpoint.

- Single-track recording errors retain portable, versioned details beside the
  take. Review recordings shows saved details after reopening, including attached
  takes. Malformed metadata and storage failure report separately and preserve
  raw checkpoint recovery. Native Windows and multi-track persistence remain open.
- Desktop language and regional preferences, embedded contextual catalogs,
  independent number formatting and expanded/RTL developer test locales. English
  plus 33 draft catalogs are partial; each draft translates 95 of 564 messages.
  Native-speaker/full UI qualification remains open. Numerical timelines and
  equipment charts retain their direction under RTL layouts.
- Reviewed equalizer localization vocabulary, localized standard dialog actions,
  script/region-aware catalog selection and signed RTL numeric input. Catalog
  updates preserve unfinished translator text, plural forms and comments.
- Easy Linux/Windows installation is an explicit product acceptance requirement.
  Useful installable workflow previews are the delivery priority; package/runtime
  and native Windows qualification remain unfinished.
- Local Ubuntu 26.04 amd64 DEB preview preparation verifies every install input
  against qualified source and pairs an exact source archive. Frozen increasing
  version sequences fix same-day upgrade ordering. Owned Ubuntu Base runtime
  installation, normal-user GUI Unicode project/WAV export and normal package
  upgrade/remove/reinstall pass; complete desktop/recording qualification remains open.

- New desktop graph/capture/render preparations follow the trusted Project resources
  budget. Recording envelopes use immutable prepared payload usage; declared raw
  pools, bridge bindings, manual banks and writer workspace share the parent.
  Callback behavior, device routing and project schema remain unchanged.

- Shared project admission for prepared mixes/playback pools, reader bindings and
  decode buffers, shared media caches and WAV export output buffers. Retained
  generations keep credit until control retirement; preparation failures unwind
  credit before audio activation or export publication. Desktop resource settings
  share live/offline usage. Capture/other IO, exact allocator/RSS and sustained
  capacity remain open.

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
