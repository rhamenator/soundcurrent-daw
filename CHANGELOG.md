# Changelog

- Development controls explain WASAPI device/project rate conflicts,
  cross-device stream selections and duplicate channels before enabling Start.
  Authored incompatible routes survive reopen; the existing installers retain
  their earlier frozen code. Linux and native Windows Qt control evidence is
  recorded separately from physical audio and installer qualification.

## Unreleased — development preview

- Clip playback speed now supports0.250–4.000 times normal speed, with linked
  pitch, retimed duration/fades, exact origin, Undo/Redo and schema1.11 state.
  Shared live/export processing preserves raw media and split/crop/seek phase.
  Independent pitch/stretch and current installed workflows remain open. See
  [checkpoint132](docs/132-clip-playback-rate.md).

- Owned clips now retain exact fractional source positions at different physical
  sample rates. Shared playback/export, project-frame crop/split, Undo/Redo and
  save/reopen use schema1.10. The desktop shows source/project rates and offers
  signed project crops. Updated installers require the pinned BSD kernel license;
  installed preview refresh, pitch/stretch and foreign-project conversion remain
  separate qualification gates. See [checkpoint131](docs/131-positioned-clip-playback.md).

- Development WAVE checking now validates admitted PCM/float headers and complete
  samples through pinned virtual I/O, preserving original metadata, byte hashes
  and floating headroom. Linux gates pass; native Windows follow-up is separate.
  Broader formats, desktop media selection and project conversion remain open.

- Developer import tooling can bind an explicitly approved media folder and read
  plain relative files through pinned handles with quotas and streaming checksums.
  Original Linux/native Windows folder and checksum checks pass; additional
  case-sensitive reference fixtures await the revised native Windows gate.
  Desktop media selection, audio decoding and destination conversion remain open.

- REAPER inspection now shows original properties and source lines in separate
  tabs, including distinct gain layers and explicit missing/unsupported state.
  Versioned reports receive independent shape/token/ownership validation. Saved
  inspections retain properties, and older outline-only files still reopen.
  Destination project conversion and native/render equivalence remain open.

- Owned C++ import intermediate model preserves original track/item scalar values,
  Unicode byte tokens and opaque state with explicit missing, duplicate, ambiguous
  and unsupported evidence. Source gain layers remain separate. Property preview
  integration, destination conversion and rendered equivalence remain open.

- Original REAPER7.82/Linux saved/reopened project corpus with native property
  witnesses and byte-preserving worker acceptance. Semantic conversion, rendered
  equivalence and Windows source-writer qualification remain open.

- Synthetic concurrent recording tests retain bounded writer phase/backlog
  diagnostics without changing capture policy. Original failed and passing
  native runs are recorded separately; sustained native qualification stays open.

- Save/Open portable `.scinspect` files with exact original bytes, retained
  unverified outline and checksum validation. Disk work runs on the admitted
  worker; existing destinations are preserved and missing original sources are
  supported. Native conversion and Windows Qt/install qualification remain open.

- File menu REAPER project inspection preview with a separate worker, bounded
  shared resource admission, strict source/hash/report validation and async
  cancellation/close. Read-only outlines preserve the current project and source.
  Native format conversion, hard sandboxing and Windows Qt qualification remain
  pending; existing installers do not contain this revision.

- Native capture now copies into prepared resource-admitted storage and releases
  the SDK packet before processing. Poisoned SDK reuse, strict error/metadata
  behavior and memory retirement are tested; native endpoint and installer
  qualification of this revision remain pending.

- Opt-in bounded WASAPI capture lease metadata now separates SDK packet positions
  from acquisition, callback and release timing. One complete and two refused
  native recordings are retained with independently checked raw/export samples.
  This diagnostic does not rebuild or qualify the installed Windows preview.

- A refreshed local Windows installer/source pair passes normal installation,
  shortcuts, main launch/close and preservation through removal/reinstall using
  installed runtime DLLs. Three capture-discontinuity failures are retained;
  this artifact is an installation/UI preview with experimental native audio.

- WASAPI normal playback now submits a bounded native-only silent end guard.
  Owned native tests preserve all non-silent final samples and short ranges;
  startup/end/engine frames remain distinct. Cancellation during the guard stays
  stopped, and the separate production-EQ active-Stop fidelity issue remains open.

- WASAPI playback now admits an explicit device-period startup interval with
  separate native/content timing. Owned native noise, single-sample impulse and
  non-silent desktop playback preserve the first source sample; prepared
  cancellation and timing propagation are checked independently. Existing
  installers and physical/sustained qualification remain separate.

- Opt-in direct Windows render diagnostics reproduce the retained non-silent
  startup attenuation without invoking the DAW mixer/EQ. Independent media
  checks preserve the failure; no production correction or installer change
  is claimed.

- Local Windows 11 x64 installer preparation with per-user preview slots, desktop
  and Start-menu shortcuts, exact-file uninstall, signed Microsoft runtime setup
  and paired GPL/dependency sources. A clean-clone install/main/shortcut/removal/
  reinstall workflow and unchanged native recording/EQ/Undo/project/WAV retry
  pass independent checks. The original observer discontinuity is retained.

- Native Windows desktop playback and single-track recording factories, stable
  device/channel route identities, project sample-rate selection and visible
  monitoring limits. An owned native GUI record/EQ/Undo/play/save/reopen/WAV workflow
  passes independent media checks with a silent lead-in. Non-silent startup,
  Windows monitoring/duplex and broader installer/failure/upgrade qualification
  remain open.
- Late recording-writer failures are reported independently of completed audio
  processing, preserving other finalized lanes and the failed writer's checkpoint.

- Single-track recording errors retain portable, versioned details beside the
  take. Review recordings shows saved details after reopening, including attached
  takes. Malformed metadata and storage failure report separately and preserve
  raw checkpoint recovery. Native Windows and multi-track persistence remain open.
- Desktop language and regional preferences, embedded contextual catalogs,
  independent number formatting and expanded/RTL developer test locales. English
  plus 33 draft catalogs are partial; each draft translates 95 of 562 messages.
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
