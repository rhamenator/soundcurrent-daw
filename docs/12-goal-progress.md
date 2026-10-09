# Active full-suite goal: progress and completion evidence

The owner activated the complete [goal](../GOAL.md) on 2026-10-05 and added X004 other-suite project import. The reference baseline is unchanged. This goal remains **active and incomplete**; no milestone or compile result substitutes for the full completion audit.

Delivery priority, 2026-10-07: [useful installable workflow previews](89-workflow-previews.md),
starting with Linux recording/EQ/project/WAV. The full timeline remains early:
the recording slice is substantially implemented but its end-to-end qualification
is open, M2 is in progress, and most M3–M11 subsystems remain ahead. No defensible
completion percentage or date is available; combined full-suite parity is a
multi-year planning assumption, not a measured schedule. Preview gates make
useful delivery independent of the eventual parity qualification date.

## Current evidence

| Requirement family | Current authoritative evidence | Still required |
|---|---|---|
| Frozen reference coverage | `research/baseline.json`, 92 source-linked `research/parity.json` workflow rows | Detailed suboptions/corpus and licensed hands-on reference comparisons |
| State/reliability | S1/S2 snapshots/identity/gestures; S4 verified-prefix recording recovery/SIGKILL/short-write fixtures | General edits, autosave, recovery discovery/UI and broader faults, migrations, backups, missing-media/plugin UI |
| Shared processing | S3 prepared peaking EQ, sample events, float headroom, private live/offline fixtures | Full graph/routing/PDC/timing domains/latency/tails; devices and advanced processors |
| RT transport/lifetime | Bounded scheduled/immediate queues and applied-frame receipts; concurrent/wrap/replay fixtures; retirement-credit and off-RT destruction fixtures | Full native device/deadline qualification, full graph crossfades/state migration/epochs, deadline/stress qualification |
| Recording/editing/mixing | S4 RF64 recovery plus S5 native owned-source ten-second raw capture/EQ/monitor/save-reopen and source removal; S6a bounded read-ahead/playback/seek retirement and native file playback/sink removal; S6f/g desktop recording/verified attachment/manual recovery | S5 hardware timing/alignment/reprepare/Windows, S7 export, portable routes/automatic discovery/remaining acceptance, then M2/M4 full workflows |
| MIDI/automation/performance/modular | Full requirements remain in M3/M5/M7 matrix | Destination models, implementation and workflow/quality evidence |
| Plugins/instruments/content/analysis | License/dependency inventory and M6/M7/M8 requirements | Actual adapters, catalog/content rights, sound quality and failure qualification |
| Notation/immersive/video | M9/M10 requirements remain intact | Models, rendering/routing/synchronization and hardware/quality qualification |
| Native and exchange import | X004 contract plus P080/P081/P082/P091 | Versioned native/exchange adapters, source fixtures and preservation/loss/render reports |
| Equipment profiles/editor | X005 pinned offline Qt library/editor, generated speaker catalog, strict import, custom-copy/undo and dirty-close evidence | Shared engine schema, monitoring/print routes, portable pins, capture/target curves, rights and platform/localization qualification |
| Windows | Native MSVC/Qt desktop/disk checks; native single-track record/EQ/Undo/play/save/reopen/WAV with independent retained media | Non-silent startup, physical input, monitoring/duplex, rate conversion, plugin/full workflows and installer |
| All-Europe localization | Extensible 143-item planned inventory; partial contextual desktop catalogs/runtime and independent language/format settings | Complete coverage audit, remaining translations/plurals/diagnostics, native review, per-platform UI/help/installer qualification |
| Distribution/security/licenses | GPL license, pinned source provenance/notices, local build/test evidence | Actual packages, SBOM/transitives, source delivery, untrusted-input/failure and clean-install gates |
| Easy installation | X007 [Linux/Windows setup contract](88-easy-installation.md), explicit fresh-install/upgrade/failure/remove/localization/source workflows | Runtime/support audit, graphical packages/installers and native qualification without development tools |

There is meaningful implementation work available. Reference-license access, native Windows qualification, source-suite project corpora and native-speaker reviews are external evidence needs, not reasons to block current engine/media work or claim those gates passed.

## Execution checkpoint

Prior S6b checkpoint: **progress**, committed S6b bounded immediate edits/applied-frame receipts as `d4c42a8`.

S6c checkpoint: **progress**, added the S6c asynchronous project controller and Qt project-editing preview, separate I/O ownership, revision/content-correct saves, immutable snapshots, grouped scalar undo, queue pressure and priority shutdown, and a close barrier before dirty choices. Linux controller/UI/core and sanitizer evidence is recorded in [the contract](18-desktop-controller.md). The new desktop adapter is not yet connected to recording/playback/export and has no Windows GUI qualification. The owner added **X005 equipment profiling and a profile editor**; its full [contract](17-equipment-profiles.md) and goal extension remain required, not implemented. Next is production S6 native transport/preparation/meter/recovery integration, then S7 export. The full objective remains active and incomplete.

Release/completion requires the objective's entire requirement-by-requirement audit, including all frozen-reference workflows and owner-accepted gaps. Keep this document and test manifests current as work advances.

## S6d checkpoint (2026-10-05)

Production playback now has a backend-free clock bridge and a PipeWire control/preparation owner for inactive explicit routing, sticky faults, bounded observations, immediate receipts and native-before-reader shutdown. The native file fixture uses this owner. Linux10-group debug/sanitizer tests and Windows headless cross-build pass; serial native sample and disconnect tests pass. One native run overlapping headless tests timed out with no retained child stages; its cause remains unknown and the load/shutdown gate stays open. Normal dependency-unload memory, hardware and Windows native gates remain open. [Contract](19-native-playback-owner.md); [evidence](../tests/results/SLICE-001/2026-10-05-native-playback-owner.json). Next: asynchronous GUI Play/Stop/output selection and edit reconciliation, then recording/export. Full frozen parity, X004, X005 and localization remain incomplete.

## S6e checkpoint (2026-10-05)

Previous goal turn was **progress**: `c9c4c17` added native playback ownership. This turn connects the real Linux desktop to an asynchronous transport/preparation worker, explicit output selectors, Play/Stop, live EQ/undo, accepted/applied receipt state and colorized numeric metering. Controller/UI failure tests and real owned native GUI signal/offline replay evidence are recorded in [the contract](20-desktop-playback.md). Both workers join before window close; completed native routes remain until Stop/reprepare/close so downstream final delivery is not raced. Native timeout/module-unload and hardware/Windows/accessibility/localization gates remain open. Next: recording ownership, input/arm/record/monitor, raw take attachment and recovery UI, then S7 export. Full goal, X004 and X005 remain incomplete.

## X005 equipment editor checkpoint

Owner-requested transfer is **progress**: a committed equalizer snapshot now supplies
an offline profile library/editor in the actual DAW Equipment menu. Source/data hashes,
GPL notices and upstream candidates are recorded; the equalizer repositories are not
modified. Linux catalog/import/fit/reference-preservation/undo/save-prompt/menu fixtures
and sanitizer evidence are in [X005 results](../tests/results/X005/2026-10-05-equipment-editor.json).
This does not apply correction to audio. X005-A/B are partial; shared engine schema,
control-room/print routing and portable pins remain open, as do native Windows,
localization and release-rights gates. The full goal remains active and incomplete.
Next primary slice task remains native recording ownership, input/arm/record/monitor,
raw take attachment and recovery UI, followed by S7 export. Profile routing joins M4.

## S6f checkpoint (2026-10-05)

Previous goal turn was **progress**: `42e259d` added the X005 pinned offline equipment library/editor. Production Linux recording now owns inactive input/monitor setup, activation-time disk jobs, raw-before-DSP capture, bounded sticky terminal causes, native-before-writer shutdown and failure recovery. The canonical controller admits finalized takes only after separate journal/hash verification while preserving intervening EQ edits. [Contract](21-native-recording-owner.md) and [evidence](../tests/results/SLICE-001/2026-10-05-native-recording-owner.json) separate native synthetic, controller, sanitizer and Windows headless results. Input/arm/record/monitor/recovery desktop controls and close choreography are next, then S7 export. Native Windows/hardware/deadline/unload gates remain open. All 92 frozen parity rows, X004, profile processing/portability and European localization remain incomplete.

## S6g checkpoint (2026-10-05)

Previous goal turn was **progress**: `942ba76` added the production recording owner and typed verified take admission. The desktop now exposes first-track input/arm/Record/monitoring and input/monitor meters through a separate preparation worker. Live EQ/undo follows canonical revisions with bounded exact-suffix retry. A retained receipt cannot be overwritten, and close waits for stop/join and verified attachment before its dirty prompt. Manual recovery previews/copies an owned verified prefix to a new asset and preserves the original. [Contract](22-desktop-recording.md) and [manifest](../tests/results/SLICE-001/2026-10-05-desktop-recording.json) record actual scope. Native Windows/hardware/deadline/unload, portable per-channel routes, simultaneous overdub/Auto/multitrack and automatic recovery discovery remain open. Next S7 offline WAV export; full slice, all 92 frozen parity rows, X004/X005 processing/portability and European localization remain incomplete.

## X005 catalog and equipment taxonomy update

Previous goal turn was **progress**: `091d513` connected desktop recording and verified recovery. The owner reiterated equipment profiles/editor adoption from the equalizer chat. Its later committed `459627c` update is now pinned by file revision/hash in this repository: 1,092 speaker corrections across 255 brands, five added models and no changes to the earlier correction filters. Optional subtype/power metadata imports with backwards-compatible defaults, is searchable/filterable, and joins editor undo/redo/custom-copy persistence. Changing equipment kind resets incompatible filter selections; visible translated kind labels use stable data identifiers. [Update evidence](../tests/results/X005/2026-10-05-equipment-catalog-update.json) separates Linux UI/parser/catalog tests from unqualified platforms/audio. Equalizer checkouts remain untouched; no publication. Profile routing/printing/project pins and shared engine schema remain required, alongside Windows/localization/rights qualification. All frozen parity rows remain incomplete; next main slice task remains S7 offline WAV export.

## S7a offline export core checkpoint

Previous goal turn was **progress**: `df920a5` updated the equipment catalog/editor taxonomy. This turn adds the framework-independent selected-track float WAV/RF64 export API and developer CLI using the existing reader/private EQ, selected-range preroll and disclosed bounded tails. Complete-file flush/hash/source/overwrite revalidation precedes publication; cancellation/failures preserve destinations and clean owned temporary files. Postpublication durability errors report the complete file explicitly. [Contract](23-offline-export.md) and [manifest](../tests/results/SLICE-001/2026-10-05-offline-export-core.json) distinguish Linux debug/sanitizers, independent chunks/direct-form-I sample oracle, actual active-writer SIGTERM and Windows headless cross-build. Equalizer checkouts and user audio remain untouched; no push. Next S7b desktop export worker/dialog, then S8 and remaining route/record/platform gates. Full frozen parity, X004/X005 processing/portability, Windows runtime and European localization remain incomplete.


## Continuing equalizer source refresh (2026-10-06)

The owner reported further equalizer changes. The read-only review now observes public
`080195a` and premium `a63cb44`; prior draft borrowed inputs are committed unchanged.
The inventory covers all 12 inputs independently in both repositories, including the
premium equipment header, collector, catalog, license, report, source registry and tests.
[Audit evidence](../tests/results/X005/2026-10-06-equalizer-reuse-committed-refresh.json)
records matching hashes/committed blobs, retained integrity, independent premium edits,
informational HEAD changes and source preservation. No compiled DAW behavior changes
are warranted by this refresh. Separate per-channel project routing work is ongoing
and unqualified; next implementation work resumes that S8 persistence/migration and
portability acceptance. Full parity, X004/X005 routing/printing/portable pins, Windows
runtime and European localization remain incomplete.


## S8a project routing and native roundtrip (2026-10-06)

Previous goal turn was **progress**: `61a0e9a` refreshed committed equalizer input provenance and independent premium profile tracking. This turn completes portable first-track per-channel input/playback/monitor intent, strict schema 1.1 and explicit 1.0 migration, mixed semantic undo, ordered worker-side channel patches and epoch/revision-aware dropdown restoration. Named unresolved placeholders require explicit choices; restoring/undoing intent does not activate or reconnect an active graph. [Contract](26-project-routing.md) and [evidence](../tests/results/SLICE-001/2026-10-06-project-routing.json) separate core/controller/GUI, 17 debug/sanitizer groups, Windows headless cross-build and actual native owned routes. The native ten-second raw/live-EQ/undo/save/relocate/reopen/export workflow passes with zero sample differences and two pre-existing links/defaults preserved. A source-removal prefix and diagnostic native sanitizers also pass; normal module-unload/deadline/physical/Windows gates stay open. Remaining first-slice gates and M2 multitrack work follow. Full 92-family parity, X004 native imports, X005 monitor/print/profile pins/measurement/rights and European translation delivery remain incomplete.

## S8b saved monitoring preferences (2026-10-06)

Previous goal turn was **progress**: `1380243` persisted per-channel routing and
qualified the native recording/export roundtrip. This turn stores Off/Post-EQ
monitoring per track in strict schema 1.2 with explicit 1.0/1.1 defaults, shared
semantic Undo/Redo, passive restore and accepted-prefix recording preparation.
A prepared graph retains its captured mode through Undo; the desktop discloses
the mismatch until Stop/preparation. [Contract](27-monitoring-preferences.md) and
[evidence](../tests/results/SLICE-001/2026-10-06-monitoring-preferences.json) record
18 passing Linux debug and sanitizer groups, Windows headless cross-build, native
480,000-frame live/Undo/save/relocate/reopen/export with zero differences, input
removal and diagnostic native sanitizers. Native reopen now restores its saved
mode without a manual fixture override. Two existing links/defaults stay intact.

The owner's equalizer update request was rechecked: all 24 registered reused
inputs still match public `080195a` and premium `a63cb44`; isolated audit CLI
failure/source-preservation fixtures pass. Ongoing Windows equalizer installation
and routing work remains outside the adopted subset. Neither equalizer checkout
was altered; no publication. Next: bounded project recording-job discovery and
consented verified-copy recovery, then M2 multitrack foundations. Physical, native
Windows, normal module-unload, deadline/filesystem/>4 GiB and general recovery
gates remain open. All frozen parity families, X004 native imports, X005 audio
routing/printing/portable pins/measurement/rights and European language delivery
remain incomplete; the full goal stays active.


## S8c recording discovery checkpoint (2026-10-06)

Previous goal turn was **progress**: `63cf570` persisted monitoring preferences
and captured accepted recording preparation state. This turn adds bounded
Qt-free recording-job metadata discovery, cooperative writer leases, explicit
legacy uncertainty, attached/recovered-chain diagnostics and cancellable verified
copying. A separate latest-request I/O owner performs passive Open scans without
preparing or activating audio. The desktop offers a scrollable list followed by
verified preview/consent; close dismisses both dialogs and waits for closed
receipts. Global transport Stop also cancels recording preparation/recovery.

[Contract](28-recording-discovery.md) and
[evidence](../tests/results/SLICE-001/2026-10-06-recording-discovery.json) separate
19 passing debug/sanitizer groups from later focused copy-cancellation and
controller/UI checks. The final discovery fixture has 46 checks, including an
owned writer killed with SIGKILL and recovered after its OS lease releases.
Windows headless core/tests cross-build passes; runtime/GUI/audio remain
unqualified. Native owned-route recording/live EQ/Undo/save/relocate/reopen/export
passes with zero sample differences and two existing links/defaults preserved.
Diagnostic native sanitizers still use `PIPEWIRE_DLCLOSE=false`, which does not
close the normal dependency-unload gate. No deadline/load claim is made.

All 24 registered equalizer inputs still match the observed public `080195a`
and premium `a63cb44` heads. Neither checkout was modified; no push/publication.
Next primary implementation: **M2 multitrack foundations**—stable track/clip
commands and Undo, selection/timeline, prepared shared-clock playback graph and
simultaneous overdub. M1 physical, native Windows, filesystem/power-loss/>4 GiB,
normal module-unload and load/deadline gates remain open. Autosave/edit journals,
snapshot recovery and missing-media relinking also remain required. Full frozen
parity, X004 native imports, X005 monitor/print/profile pins/measurement/rights
and all-Europe translation/review/UI delivery remain incomplete. Goal stays active.


## M2a track/clip command foundation (2026-10-06)

Previous goal turn was **progress**: `642f6be` added passive recording discovery
and verified consented recovery. This turn starts M2 with Qt-free stable-ID track
insert/remove/rename/reorder, clip insert/remove/range/move/split, transactional
1–64-operation groups and scoped structural history. Mixed Undo/Redo is bounded
by 256 units and a 32 MiB counted-payload budget. Verified raw-take admission now
joins Undo while preserving audio files. Controller prevalidation preserves an
unrelated active gesture on failure; concurrent save/barrier snapshots remain
exact. Removing a track during take verification prevents wrong-track admission;
Undo/restoration and retry work without changing the raw recording.

[Contract](29-multitrack-edits.md) and
[evidence](../tests/results/M2/2026-10-06-multitrack-edits.json) record 20 passing
final debug and ASan/UBSan/LSan groups, 72 domain checks, 286 controller checks,
325 desktop checks including actual recovery Undo/Redo, developer CLI persistence
and native owned roundtrip. Windows headless cross-build passes; native
Windows/Qt/audio remain unqualified. An initial large-name history stress fixture
timed out under sanitizers; final validation avoids repeated band lookups, and a
1,024-clip retained-history fixture exercises byte retirement without the redundant
large-name scan workload. The original 60-second gate remains and final tests pass;
this is not a load/deadline or physical performance certification.

All 24 registered equalizer inputs still match; no borrowed update or checkout
write is needed. Nothing was pushed. P008/P009/P086 have explicitly scoped
subworkflows, with complete frozen-reference requirements and F/Q/C/N axes
remaining unqualified. Next: desktop stable track/clip selection and timeline
bindings, then shared-clock live/offline multitrack graph and simultaneous overdub.
All later M2 tasks, independent M1 gates, full reference parity, X004 native
imports, X005 audio/profile portability/rights and European localization remain
required. Goal remains active and incomplete.


## M2b desktop timeline checkpoint (2026-10-06)

Previous goal turn was **progress**, committed M2a typed edits as `4fc2266`.
This turn adds [desktop stable track/clip selection and a rectangle timeline](30-desktop-timeline.md),
exact range/split/move/duplicate/insert controls and selected-track EQ/routes.
Canonical order is preserved. Accepted-prefix preparation captures its selected
ID, and owners reconcile using that ID even when the inspector moves elsewhere.
Take admission targets the prepared track; unrelated EQ is not sent to it.
Transport still prepares one track, so shared-clock multitrack playback and
simultaneous overdub remain the next implementation task.

[Evidence](../tests/results/M2/2026-10-06-desktop-timeline.json) separates actual
Qt keyboard/mouse edits, raw-file preservation, Save/reopen, fake endpoint
identity/isolation, Linux debug/sanitizers and the owned PipeWire regression.
Final debug and ASan/UBSan/LSan suites each pass all 21 groups. The serial native
roundtrip retains 480,000 frames, zero direct/native-prefix export differences,
zero RT allocations/frees/blocking locks and preserved defaults/owned route cleanup.
The new widget initially exposed a derived-destruction signal callback bug; its
destructor now disconnects child handlers before teardown. Selection handlers
also defer list/scene reconciliation until the emitting input event finishes,
including when a new model has just been published. Full regression
found an Open-before-timer preparation race, fixed by synchronizing initial
selection before capturing the target. Save-button and asynchronous scene/route
waits in the new fixture were corrected; none of these failures were waived.

The read-only equalizer audit still matches all 24 registered inputs and retained
snapshots; no borrowed source refresh or checkout write is needed. No publication.
Windows headless CMake reconfiguration/build passes with unchanged core objects;
Qt/native Windows remains unqualified. P001/P008/P009/P086 retain full frozen
acceptance and unverified F/Q/C/N axes. Independent M1 physical/native Windows,
filesystem, deadline/load and normal module-unload memory gates remain open,
alongside X004 native imports, X005 audio/portable profiles/rights and all-Europe
translation delivery. The full goal remains active and incomplete.


## M2c1 shared-clock multitrack graph checkpoint (2026-10-06)

Previous goal turn was **progress**, committed the desktop timeline/selected-track
workflow as `0b170ac`. This turn adds [one shared project cursor, independent EQ
lanes and an explicit sparse output matrix](31-multitrack-mix.md), fair bounded
per-track read-ahead, callback-only mixing and off-RT worker lifetime. Scheduled
and immediate EQ/enable events retain generation/UUID mapping and applied-frame
receipts. Float64 summing preserves float32 headroom; gaps and stale track-frame
counts remain visible while healthy lanes retain offsets.

Both static selected-track and new multitrack WAV export share that graph and
the existing publication transaction. `render-mix` exposes matching-layout unity
routes for every saved track; the API handles explicit custom/mixed-layout
matrices. It does not silently drop mismatched channels or modify project/media.
The new CLI fixture initially lacked its media directory and used the wrong
processor JSON key; both were corrected to the existing schema. No test gate
was waived.

[Evidence](../tests/results/M2/2026-10-06-shared-mix-graph.json) separates independent
direct-form-I/matrix and source-coordinate oracles, 32-track short live-callback/
WAV corpus, variable blocks, 1/2/8/32/256-channel identities, mixed stereo/mono
routing, late/blocked/failed-reader behavior, source preservation, token retirement
and zero audited RT allocation/free/lock observations. Linux 23-group debug and
ASan/UBSan/LSan suites, independent old/new CLI checks including actual SIGTERM,
Windows headless cross-build and serial owned native single-track roundtrip pass.
Native Windows/Qt/audio is unqualified; these tests do not establish physical
load/deadline or full M2 duration/capture acceptance. All 24 registered borrowed
inputs still match, with equalizer checkouts untouched and no publication.

Next integrate the prepared mix into the native playback owner/desktop with
explicit master output layout/routes, then a shared playback/capture callback
for simultaneous armed tracks/overdub. Production UI currently prepares one
selected track. General bus/DAG/PDC/feedback/crossfade/state migration, punch/loop/
takes/comping/fades and all later frozen workflows remain required. P001/P017
scoped evidence leaves every F/Q/C/N axis and full reference acceptance unchanged.
Remaining M1 native Windows/physical/filesystem/module-unload/deadline, X004 native
imports, X005 processing/portable profiles/rights and all-Europe localization
gates remain open. Full goal remains active and incomplete.


## M2c2 native shared-clock playback checkpoint (2026-10-06)

Previous goal turn was **progress**, committed the shared-clock engine/read-ahead/
WAV workflows as `e445ca7`. This turn connects that prepared run to the production
PipeWire owner, shared native clock gate and desktop worker. The desktop matching-
layout mix has explicit output selection anchored to its preparation track, and
inspector changes do not retarget it. Stable-ID EQ deltas/Undo address every mixed
lane. Whole-model applied revisions require receipts from every affected lane,
with independent watermarks rather than the highest global receipt.

[Contract](32-native-mix-playback.md) and
[evidence](../tests/results/M2/2026-10-06-native-mix-playback.json) separate backend-
free clock/failure and withheld-lane receipt tests, actual Qt mixed-track controls
with a synthetic endpoint, and owned native output/source-coordinate/independent
summing comparisons. No native Windows, hardware, deadline/duration or full
frozen-reference acceptance is inferred.

Dedicated master layout/matrix/output state and its editor remain required next,
followed by one native playback/capture callback for simultaneous armed tracks/
overdub. Desktop export still selects a track; explicit multitrack API/CLI export
exists. Punch/loop/takes/comping/fades, general buses/sends/sidechains/PDC/VCA and
all later reference workflows stay in scope. Independent M1 native Windows/
physical/filesystem/module-unload/deadline, X004 imports, X005 processing/portable
profiles/rights and all-Europe localization remain open. No equalizer writes or
publication. Full goal stays active and incomplete.

Qualification detail: final Linux debug and ASan/UBSan/LSan suites pass all 23
groups; Windows core cross-build and independent old/new export CLI checks pass.
The new Qt fixture initially selected an anchor before the asynchronous track
list was populated; it now waits for that visible state. A first native 32-track
GUI run timed out after acknowledged Undo while awaiting completion/downstream
delivery. Its original log has no terminal state snapshot, so the cause remains
unknown. The fixture now wakes on sink gaps and emits terminal diagnostics; the
next isolated native mix run completed with exact samples. That rerun does not
resolve long-duration/deadline/shutdown qualification or prove a cause for the
earlier timeout. The failed evidence is retained, with no production suppression.

## M2c3 saved master checkpoint (2026-10-06)

Previous goal turn was **progress**, committed native/desktop shared-clock
playback as `9f875ce`. This turn adds optional stable-ID master state in schema
1.3, exact sparse matrix/independent output intent, migration without guessed
routes, typed semantic history and atomic track-removal detach/Undo. The desktop
editor stages explicit channel-pair/gain rows and submits one edit, with a visible
4,096-entry bound preserving larger saved state. Saved project mix preparation
and `render-mix` compile this plan; output selection no longer modifies an
inspected track. Matrix changes require Stop/reprepare; scalar EQ remains live.

[Evidence](../tests/results/M2/2026-10-06-master-matrix.json) separates schema/
invalid-input/migration/history, real Qt dialog/desktop persistence/output
ownership, bounded-editor preservation/focus/shape tests, stereo sample oracle,
owned native saved-plan playback and platform builds. General bus/DAG/PDC/mixer,
desktop master export, editor virtualization/load, simultaneous recording and
overdub alignment remain required. The earlier native completion timeout is
still unexplained. This checkpoint also observed an owned sink clock gap
while the player remained Running (position 110592, sink prefix 108544).
The fixture now records first-gap clock diagnostics, and the next isolated
saved-master/full/disconnect/export batch passed exactly. No cause or production
fix is claimed; finite passing runs do not resolve duration/deadline/shutdown
qualification. Full goal remains active/incomplete, with no equalizer writes or
publication and all frozen F/Q/C/N/X004/X005/localization requirements intact.

Next: characterize/diagnose the native graph/sink clock-gap observations, then
shared native playback/capture owner for simultaneous armed tracks and
overdub, preserving raw takes, one project origin, counted gaps and verified
finalization/recovery. Independent M1/native Windows/physical/filesystem/module-
unload/deadline gates remain open.


## M2c4 callback-fault and timing checkpoint (2026-10-06)

Previous goal turn was **progress**, committed saved master state/editor as
`46437b5`. This turn retains the first callback validation record independently
of the lossy meter queue, forwards it through the native owner and copies it
before worker retirement. New preparation clears current-generation facts while
old snapshots remain immutable. Invalid callbacks still leave the cursor intact
and silence certified buffers; no callback logging or heap/lock work was added.
The PipeWire unsupported-quantum path now carries its complete received clock.

[Contract](34-native-timing.md) and
[evidence](../tests/results/M2/2026-10-06-native-timing.json) separate backend-free
4321 clock checks, 65 synthetic controller checks, 23-group serial debug and
ASan/UBSan/LSan suites, three optimized core groups, Windows compilation-only,
owned native sample oracles, scheduler observations and deliberately rejected
sink loss. Bounded fixture timing is sorted only after join. Final 32-track
480000-sample debug and Release runs are exact with no observed missing frames
or callback allocations/frees/blocking locks; no universal deadline claim follows.

A concurrent debug run failed awaiting synthetic recording capture at UI line
368; its cause remains unknown even though serial debug and sanitizer suites
passed. Original failure evidence is retained. The controlled sink-gap fixture
initially waited 20 seconds at its Undo checkpoint after detecting the deliberate
gap; that wait now wakes and fails promptly. This is a test correction. The final
sanitized native attempt faulted before injection at engine frame 6144. Retained
clock facts show one missing 1024-frame block with matching ID/rate and no flags;
measured callback elapsed time exceeded the 21.333-ms period. No cause for the
historical completion/sink-gap failures or general performance fix is claimed.
All failures remain visible; no fault was suppressed to pass.

The read-only equalizer audit still matches all 24 registered inputs; checkouts
remain untouched. No dependencies/license choices/publication changed. All 92
frozen family acceptance/quality/F/Q/C/N values remain unchanged. Native Windows,
physical/filesystem/deadline/load/duration/module-unload, X004 project imports,
X005 monitoring/print/portable profiles/rights and all-Europe localization gates
remain open. Full M2/full goal stays active and incomplete.

Next: one shared native playback/capture owner for simultaneous armed tracks and
overdub, explicit channel maps, common timing origin, raw bounded capture pipes
and off-RT writers. Qualify timestamp/alignment, counted gaps, stop/finalization
and interrupted-recording recovery with independent sources; retain diagnostics
and investigate the open timing/UI observations under declared optimized load.


## M2d1 shared playback/capture foundation (2026-10-06)

Previous goal turn was **progress**, committed retained callback facts and timing
evidence as `115cce9`. This turn adds the framework-free duplex bridge: one device
clock gates the existing file mix and explicitly mapped armed raw pipes. Every
raw take gets the same device origin/project start. Off retains file playback;
Post-EQ monitoring uses a prepared live replacement through the existing track
EQ/events/matrix. All raw/live copies precede output writes, including aliased
input/output views. Empty replacements preserve the offline path. File pipes
retain their offset/failure accounting even when their audible signal is replaced.

[Contract](35-shared-playback-capture.md) and
[evidence](../tests/results/M2/2026-10-06-shared-playback-capture.json) separate the
new backend-free 32-armed-track corpus from existing native playback regression.
Independent source/matrix checks compare every output and raw sample; 9866-frame
takes retain common origins, supplied latency/initial trim and save/reopen state.
Known 1-kHz response verifies Post-EQ monitoring and its exact first-block control
receipt; concurrent Off/Post-EQ raw files remain identical. Invalid clocks/buffers
do not move either operation. Queue/writer faults preserve unequal accepted
prefixes/rejected counts and stop without inventing a common length. Full meter
queue cannot lose the retained first fault. Interrupted unfinished mono/stereo
writers recover verified 4096-frame copies with preserved originals and alignment.

The new test initially used the wrong receipt member and omitted factory names;
compilation caught both. Its recovery assertion initially misunderstood the
legacy `writerActivityConfirmed` field: acquiring the exclusive inactive lease
sets it true. The assertion now follows that authoritative contract. No gate or
production failure policy was waived. Final review reserves the immutable run's
declared playback budget before adding raw pool/metadata payload; recomputing
from a later caller model could undercount prepared state. Native ownership must
preflight before allocating all caller-owned pools. Callback atomic types are
compile-time qualified as lock-free.

The bridge is not yet connected to a production native duplex owner or desktop
armed-track workflow. Native Windows/physical alignment/PDC, M2 ten-minute
synthetic and 30-minute native declared-device runs, process-kill/disk-full
multitrack recovery, Auto/punch/loop/takes/comping remain required. Earlier native
completion/sink gaps, concurrent UI recording timeout and normal module-unload/
filesystem/deadline gates remain open. Read-only equalizer audit still matches all
24 registered inputs; no checkout writes, new dependency, push or publication.
All 92 frozen acceptance/quality/F/Q/C/N contracts and X004/X005/all-Europe scope
remain intact. Full goal stays active and incomplete.

Next implement production PipeWire duplex preparation/activation/rollback and
callback-before-writer join/result/error retention, with owned native sources and
independent sample/gap/recovery tests. Then connect desktop arm maps/monitoring
and grouped verified take handoff/history.

## M2d2 production duplex ownership (2026-10-06)

Previous goal turn was **progress**, committed the shared bridge as `4eaa50f`.
This turn adds a framework-free recording owner and production PipeWire duplex
wrapper: combined capture admission before pool allocation, inactive packed input
and master ports, explicit routes, activation-only writers, native-before-disk
joins, and independent per-lane receipts/errors/progress/jobs plus retained reader
errors. Canceled/joined disk owners retire their descriptors/activity leases
before owner destruction so verified recovery is available immediately. Cancel
withholds all take receipts even when finalization wins a race, while preserving
completed media and the earlier terminal winner. No GUI/disk/allocation/blocking
work enters audio.

[Contract](36-duplex-recording-owner.md), [ADR-026](decisions/026-duplex-recording-ownership.md)
and [evidence](../tests/results/M2/2026-10-06-duplex-recording-owner.json) record exact
source hashes, failures and scope. All 25 Linux debug groups and 25 ASan/UBSan/LSan
groups passed; five optimized core groups passed. Windows headless owner/core/
fixtures compile/link but have no execution or native adapter/UI qualification.

The five serial Release native cases use only owned nodes and preserve defaults/
cleanup: 32-plane independent source, production duplex owner, stereo sink and one
unarmed file lane. Normal captures exactly 480000 frames per raw lane and exact
stereo matrix/file output. Source removal retains 48128-frame prefixes; actual
lane17 write failure leaves a verified 8192-frame recovery prefix and finalizes
other lanes; all canceled lanes recover 49152 frames; lane17 initial-journal
failure joins 17 earlier writers without audio activation. All checked sample
differences/missing frames/direct RT allocation/free/mutex counts are zero.
Normal observed p99/max elapsed callback times are 2.591540/2.788460 ms at the
1024-frame 48-kHz quantum. These finite observations are not a duration/load gate.

Initial tests caught a forbidden fixture project overwrite and cancellation's
retained activity lease; the fixture now swaps an independently created test
project and the owner retires joined disk resources. Review identified the
cancellation/finalization race and added a completed-writer regression. An initial
native write-fault fixture failed on a contiguous clock after output teardown,
with equal 12288-frame sink/graph prefixes. Original buffer/flag/capacity facts
were missing, so its precise cause is not proven. The fixture now coordinates
declared unmapped closure only after a valid clock/full observed graph prefix,
retains richer diagnostics and keeps final sample/prefix checks strict. No
production clock fault is suppressed; the original failure remains in evidence.

The read-only equalizer audit still matches all 24 registered inputs and both
reviewed heads. No dependency/license changes, equalizer checkout writes, push
or publication. All 92 acceptance/quality/F/Q/C/N/reference contracts stay intact.
Earlier native completion/sink gaps/concurrent UI timeout, native Windows,
physical/PDC, declared ten-minute/30-minute runs, deadline/load/disk-full/process-
kill/filesystem/module-unload and full professional workflow gates remain open.
X004 imports, X005 monitoring/print/portable profiles/rights and all-Europe
coverage/translation/review/UI remain required. Full goal remains active/incomplete.

Next: **M2d3 desktop accepted-prefix multi-arm preparation**, explicit packed
input/master routing, live monitor/EQ receipts and grouped independent verified
take admission/history, with separate failed-lane recovery. Preserve current
selection/intervening edits and native-before-worker-close ordering.

## M2d3 desktop multi-arm recording and verified groups (2026-10-06)

Checkpoint `be64344` was **progress**, not completion. The desktop now captures
accepted-prefix full-project arms/range, prepares the production duplex owner on
its worker, saves explicit packed input/master routes and reconciles EQ/enable
changes against stable prepared track IDs. Applied model revisions wait for every
participating generation/lane receipt, including lower held receipts after higher
other-lane revisions. Inspector selection and canonical reorder do not retarget
capture. The list shows captured/written frames and red failed-lane diagnostics;
the actual project peak is shown and unavailable raw peak metering is hidden.

Joined successful receipts are retained as one pending group. All finalized,
inactive journals/identities/extents/media hashes are verified off-thread before
one canonical history adoption against the latest model. A bad last receipt or
removed target rejects the whole group; intervening scalar/routes survive. One
Undo/Redo changes the entire take group and leaves raw media untouched. A failed
writer preserves other successful takes and its own checkpoint/error/job; a
partial activation constructor retains an existing diagnostic directory and the
initiating error without asserting a valid recoverable journal. Active Save/close
waits for all writer joins and group verification before saving.

[Contract](37-desktop-duplex-recording.md), [ADR-027](decisions/027-desktop-duplex-recording.md)
and [evidence](../tests/results/M2/2026-10-06-desktop-duplex-recording.json) retain exact
source/log hashes and chronological qualification. All 25 current Debug groups
passed in 23.85 seconds; all 25 ASan/UBSan/LSan groups passed in 64.71 seconds,
then the expanded active-close UI group passed separately in 8.73 seconds. Seven
optimized groups passed in 5.65 seconds. The headless Windows core compiles/links;
no native Windows or Qt/UI qualification is inferred.

Final serial Release native GUI runs use three or 32 owned source planes, one
existing unarmed file, the production desktop/controller/duplex owner and an
independent stereo sink. Each raw take contains exactly 240000 frames (five
seconds), with common native origin and exact raw source coordinates. Offline EQ
replay at actual applied frames plus an independent float64 matrix sum matches
the stereo output exactly. Group Undo/Redo and Save/reopen pass; missing frames
and direct host callback allocation/free/mutex counts are zero. 32-arm observed
elapsed p99/max callback times are 3.088963/6.143693 ms. These finite timings do not
qualify duration, deadlines, controlled load or physical alignment. Defaults,
owned-only routes and cleanup observations are retained. The revised lane17
initial-journal failure regression also passes without audio activation.

The first full Debug UI run aborted on a filesystem iterator assertion; review
found a new test holding references into a temporary snapshot during lane I/O.
The test now retains the shared snapshot. Original abort stack details were not
captured. The first 32-arm native GUI run timed out before its first capture
threshold, after an exact 3-arm run. Its phase/routes/callback counts were not
captured, so its precise cause is unproven. The fixture now captures a complete
canonical route-command prefix, waits for exact visible choices and retains
terminal diagnostics. The synchronized 3/32 rerun passes without changing the
production DSP/clock or suppressing any fault. The initial timeout and every
historical native/sink/concurrent UI observation remain recorded and their
reliability gates remain open.

The read-only equalizer audit still matches all 24 borrowed inputs and both
reviewed heads. Equalizer repositories remain untouched. No dependency/license
change, publication or push. All 92 acceptance/quality/reference/F/Q/C/N contracts
remain intact and unpromoted. M1/M2 duration/load/physical/Windows/unload and full
professional workflows remain open, as do X004 import, X005 profile monitor/print/
portable rights and all-Europe coverage/translation/review/UI qualification.
The full goal remains **active and incomplete**.

Next: **M2d4 declared-duration acceptance**. Implement the reproducible 32-track
ten-minute synthetic P001 workflow with exact timestamps/source/hash, stop/save/
reopen and bounded disk/cancel/failure evidence; then qualify the independent
30-minute declared native/device/load/alignment gate. Punch/loop/Auto/take lanes/
comping, fades and every other M2/full-product requirement stay in the backlog.

## M2d4a ten-minute synthetic duration and recovery (2026-10-06)

Checkpoint `15047ad` was **progress**, not completion. The new opt-in fixture
uses the actual framework-independent duplex engine, fixed capture pools and
32 disk writers. It wall-paces a private 48-kHz source with variable callback
sizes, explicit permuted mono inputs and a signed stereo matrix. Source generation,
independent matrix comparisons, pacing, progress and every file/hash operation
stay outside the audited callback. It uses default unity EQ; it does not imply
a non-flat live-event/native deadline workload or physical alignment measurement.
Production implementation and dependency/license choices are unchanged.

The Release ten-minute run passed: 28,800,000 frames per lane, all 921,600,000
raw samples exact, every finalized inactive journal/origin/identity/hash/extent
verified, original canonical project unchanged until Save, exact per-track
alignment and Save/reopen/media verification. 29,613 callbacks have zero audited
allocation/free/blocking-lock calls and zero missing track frames; every output
sample matches the independent float64 matrix oracle. Peak output 4.08854 retains
float headroom, with zero dropped meter observations. Stream time was 600.042
seconds and complete verification finished at 647.910 seconds. The producer had
12 late cycles, maximum 7.867691 ms; this is retained as a whole-fixture pacing
observation rather than hidden or treated as native callback/deadline evidence.

Five short normal/cancel/write-fault/stall-overflow/process-kill-recovery modes
passed on Release, Debug and ASan/UBSan/LSan. Every valid finalized/recovered
prefix is independently source/hash verified, attached to a copied model and
saved/reopened. Cancellation keeps canceled receipt errors, lane17 disk failure
preserves the initiating error and 31 other receipts, and pool exhaustion names
the failed capture without forcing all lane extents equal. SIGKILL targets only
the supervisor's freshly spawned child. Discovery/copy recovery verifies all
32 durable prefixes (49,152 frames each in these probes), keeps original media
and journal hashes unchanged and saves/reopens. Sanitized normal pacing has
99 late cycles, maximum 933.916285 ms, and stream wall time 2.937620 seconds;
it is a memory/correctness observation, not a performance pass. Windows headless
fixture/core compile/link; execution, Qt/native audio and Windows parity remain
unqualified. Full ordinary suites were not broadened because production code is
unchanged and the prior checkpoint retains their qualification.

The initial probe exposed a fixture expectation confusing clock ID 17 with
driver delay 777. The recorded journal was correct; the expectation was fixed
without production changes. The supervisor later retained the killed child's
ready/signal exit separately from recovery, and captured bounded timeout partial
stdout/stderr. Final Release normal/kill and an owned timeout diagnostic probe
passed. [Contract](38-recording-duration.md), [ADR-028](decisions/028-recording-duration-qualification.md)
and [evidence](../tests/results/M2/2026-10-06-recording-duration.json) retain source/log
hashes, workload, per-lane receipts, corrections and all inherited unresolved
native/sink/concurrent UI observations. None is waived by this synthetic result.

The final read-only equalizer audit matches all 24 borrowed inputs and both
reviewed heads. No equalizer checkout writes, push or publication. All 92 frozen
acceptance/quality/reference/F/Q/C/N contracts are byte-equivalent in their
canonical projection; only P001/P088 implementation/gap/evidence notes advance.
The full goal stays **active and incomplete**. M1/M2/native 30-minute/physical/
controlled-load/disk-full/filesystem/power-loss/unload/Windows and all remaining
professional workflows, X004, X005 and all-Europe qualification stay required.

Next: **M2d4b declared 30-minute native qualification**. Extend the owned source/
production duplex owner/independent sink fixture with streamed full-range media
oracles, complete bounded timing coverage, nearest-rank 99.9th percentile/max,
actual device/rate/quantum/scheduler/load records and complete failure diagnostics.
Keep defaults/owned routes and strict origin/gap/prefix checks. Qualify physical
round-trip alignment separately. Punch/loop/Auto/takes/comping/fades and the rest
of M2 stay in the backlog.

## M2d4b native admission, full timing and retained failures (2026-10-06)

Checkpoint `4ad7164` was **progress**, not completion. The production PipeWire
adapter now owns selected link-state listeners and admits engine/capture only
when every selected link is Active. Native activation remains necessary for
format/buffer negotiation; a closed admission gate writes bounded certified
silence without advancing DSP, capture or origin. Control-side waits and listener
retirement stay outside RT. Later inactivity/error retains DeviceLost. This
addresses an admission deficiency; the precise original delayed-plane cause
remains unproven. No DSP algorithm, capture pool/budget or durability change.

The new opt-in streamed native qualifier prepares fixed full-duration timing
storage before activation, retains each elapsed/current-period pair, and computes
nearest-rank 99.9%/maximum after join. Missing periods/storage overflow deny
qualification. Optional current-clock observation covers admission/shutdown;
production does not acquire instrumentation clocks. Owned software routes use
the existing daemon, Dummy-Driver, 1024/48000 and observed SCHED_RR priority20
native data loops. Defaults and previous links stay unchanged. Full sample,
headroom, hashes, canonical Save/reopen and synthetic alignment oracles stay
separate from timing/physical/Windows claims.

All 26 Debug groups passed in 23.53s and all 26 ASan/UBSan/LSan groups in 64.97s;
Windows headless compilation/linking passed without execution/native/Qt claims.
Final two-second normal/sink-removal cases and existing five-mode duplex owner,
three/32-arm desktop and single-file playback/removal regressions passed serially.
The normal case checks3,072,000 raw/192,000 output samples exactly and retains all
callback periods with zero host-owned allocation/free/blocking-lock counts.
Original mismatched lane 26/input 22, pre-activation negotiation timeout and missing
shutdown timing periods remain recorded with the corresponding scoped changes.
The first mismatch and all seven inherited reliability failures remain open.

The first declared 1800-second run failed after about 54.9547 audio seconds with
capture-pool exhaustion. Initiating lane 1 rejects1024 frames at a contiguous
1024-frame clock with no XRUN/discontinuity; several healthy lanes retain an
additional quantum before shutdown. Every writer joins with captured=written,
and no canonical takes attach. Complete callback timing passes its finite
thresholds for this failed prefix, which does not qualify duration. Original
checkpoint phase timings were not captured: disk/scheduler/backlog cause is
unknown. All original sources, diagnostics, journals and media remain retained.

Fixture-only disk boundary timers measure worker intervals without changing
production policy. A120-second diagnostic verifies184,320,000 raw and11,520,000
output samples exactly, Save/reopen and peak4.30698. Observed worker maxima
115.297016ms write/hash/header/flush and81.513784ms journal-to-next-write do not
explain the original2.730667-second pool exhaustion; the latter includes idle/
scheduling, not just fsync. Owner p99.9=4.675373ms but maximum17.834714ms exceeds
80% of its period (83.6002%). Sample verification passes; deadline qualification
fails. No threshold is loosened, no duration failure is replaced by a shorter pass.

The new offline `verify-retained` mode verifies every full raw prefix against
source coordinates/sample hashes, inactive journals, identity/origin/latency and
exact RF64 extents, then privately replays file/EQ/matrix over the common sink
range. Release checks all 84,424,704 raw and5,275,648 output samples exactly,
including raw lengths2,637,824..2,638,848 and18,432 rejected frames. Peak4.01682
preserves float headroom. Separate before/after SHA-256 snapshots prove all 105
original files unchanged. No native nodes, trimming, Save or copy recovery occur.
Final Debug/sanitized read-only and deliberate wrong-origin results are recorded
in [the evidence](../tests/results/M2/2026-10-06-native-duration-qualification.json).
[Contract](39-native-duration-qualification.md) and
[ADR-029](decisions/029-native-route-admission-and-duration.md) preserve scope and
failed gates. Current fixture-only timers/verifier have their own source hashes;
the pre-instrumentation long run's original hashes remain separate.

Read-only equalizer checks, source hashes and frozen contract equivalence are
recorded before checkpoint. No equalizer writes, dependency/license change,
publish or push. All 92 acceptance/quality/reference/F/Q/C/N contracts stay intact
and unpromoted. M1/M2/30-minute/physical/controlled-load/filesystem/power-loss/
unload/Windows/full workflows, X004, X005 and all-Europe qualification remain
required. The full goal remains **active and incomplete**.

Next: **M2d4c writer-backlog and callback-tail diagnosis**. Add bounded worker-only
phase and queue occupancy observations spanning audio flush, journal publication
and slab-drain scheduling. Reproduce a declared stall under the same32-track
budget, evaluate any justified durable-checkpoint/burst-buffer change with fault
and exact-prefix tests, then repeat the unchanged30-minute native sample and
callback-budget acceptance. Keep all retained failures visible. Punch/loop/Auto/
takes/comping/fades and every remaining professional workflow stay required.

## M2d4c1 writer phase/backlog observations (2026-10-06)

Checkpoint `2eed17a` was **progress**, not completion. Optional recording writer
observations now pair write/hash, audio flush, journal publication and idle waits
with written/durable cursors and the disk owner's ready/acquired backlog. The
consumer reads existing queue publication counters; audio push/finish add no
counters, clocks, allocation, blocking, logging or disk work. Ready-frame extents
are documented upper bounds for final partial slabs, and producer partial state
is omitted. Construction observations have no producer backlog. Existing fault/
cancellation callbacks and capture memory/checkpoint/durability policies remain
unchanged. Fixed fixture phase maxima/context and callback maximum clock/start
facts are inspected after joins, without an unbounded timeline or RT clock work
in production. Observers must outlive construction and joined disk owners.

Four targeted Release groups passed; all 27 Debug groups passed in 23.95 seconds
and all 27 ASan/UBSan/LSan groups in 66.10 seconds. Windows headless compilation/
linking passed, without native/Qt execution claims. Actual tests cover partial and
acquired queue accounting, exact phase ordering/durable cursors/slab return,
construction exclusion, no observer on audio, source/header/checkpoint preservation,
fixed maxima/context, phase pairing and retained incomplete/backwards observations.
Native experiments start only after all CPU builds/tests are terminal.

The ten-second native normal probe verifies 15,360,000 raw/960,000 stereo output
samples exactly, hashes, Save/reopen and peak 3.75449, with complete callback period
coverage and finite timing thresholds met. The declared four-second lane17 journal
stall initiates capture failure on that lane, retains QueueFull/rejected frames,
and records 4.003536049 seconds with ready slabs growing 0→32; written/durable
cursors preserve publication ordering. All 5,798,912 full raw/360,448 common output
samples verify exactly, including unequal raw lengths 180,224..181,248. Largest
lane17 write gap is 4.018562400 seconds. All paired phases complete; zero audited
host-owned callback allocations/frees/blocking locks. Canonical state, defaults
and prior links stay unchanged; owned nodes/links retire. No injected stall is
claimed to establish the original historical failure cause.

The unmodified 120-second native diagnostic then fails around 58.026667 audio
seconds. Initiating lane0, all 32 raw lanes and sink exhaust the fixed pool;
contiguous ID30/1:48000 clock has no XRUN/discontinuity and no missing file-track
frames. Every writer joins at 2,785,280 frames; each raw journal retains 1,024
rejected frames. Complete owner timing p99.9=6.887259 ms/max10.147618 ms (47.567%
of period) passes finite thresholds for this interrupted prefix, not duration.
The observed route is Dummy-Driver at 1024/48000 with native SCHED_RR priority20;
physical interface and controlled competing-load qualification remain absent.

Phase observations establish checkpoint backlog in this new run. Lane26 at
written 2,654,208/durable 2,605,056 waits 2.469220532 seconds in audio flush
(ready0→28), then 1.803449198 seconds in journal publication (ready28→32), after
slab return. Its write gap reaches 4.272674676 seconds, exceeding the 2.730667-second
pool horizon. All workers reach 32 ready slabs. The overall largest journal phase
is 2.287432765 seconds during final drain, which is kept separate from the
initiating checkpoint. The low-level filesystem/header/descriptor/scheduling
cause is not isolated by wall time. The original uninstrumented failure remains
unproven; measured new evidence does not retroactively waive any historical fault.

Read-only verification checks every full prefix, journal, origin/identity, sample
checksum and media hash: all 89,128,960 raw/5,570,560 common stereo samples exact,
peak 4.16414, aggregate 32,768 rejected frames visible. All 105 original file-content/
extent snapshots match; canonical one-asset project remains unchanged. No trim,
Save, take attachment or copy recovery. The first launch attempt rejected the new
mode at argument parsing before any native child; choices were corrected and
source hashes refreshed before the qualified probes. [Contract](40-writer-backlog-diagnostics.md),
[ADR-030](decisions/030-worker-phase-and-backlog-observation.md) and
[evidence](../tests/results/M2/2026-10-06-writer-backlog-diagnostics.json) retain exact
source/test/phase/failure facts and every inherited unresolved observation.

The read-only equalizer audit still matches all 24 reviewed inputs and both heads.
No equalizer writes, new dependency/license choice, publication or push. All 92
frozen acceptance/quality/reference/F/Q/C/N contracts remain intact and unpromoted.
The full goal stays **active and incomplete**. M1/M2, declared 30-minute native,
physical/controlled-load/filesystem/power-loss/unload/Windows/full professional
workflows, X004, X005 and all-Europe qualification remain required.

Next: **M2d4c2 durable checkpoint burst policy**. Evaluate phase staggering and
an explicit aggregate-budgeted reserve against the observed multi-second gap.
Keep one-second durable checkpoint bounds, raw timestamps, immediate live
monitoring and fixed RT bounds. Verify absorption of a declared four-second stall
and visible exhaustion beyond the admitted reserve, plus cancellation/failure/
recovery and source/hash/canonical ordering. Larger pools alone do not establish
sustained storage throughput: require the unchanged 30-minute sample/deadline
acceptance next. Punch/loop/Auto/takes/comping/fades and every remaining product
workflow stay in the backlog.


## 2026-10-06 continuation: M2d4c2 admitted recording reserve

Previous checkpoint **d361b87** is **progress**, not completion; the persistent
full goal remains active and incomplete. Continue from M2d4c1's measured checkpoint
backlog without waiving any of its eleven inherited unresolved observations.

Capture now admits1..256 preallocated sample slots independently of slab/callback
size, using fixed512-token SPSC queues and a sentinel outside all admitted slots.
Playback stays fixed32slots. The off-RT helper admits2..20seconds, rounds up whole
slabs and refuses impossible slot/object/per-pipe/shared budgets. Aggregate ceiling
remains256MiB. The desktop offers2/5/10seconds(default10), captures accepted Prepare
intent, normalizes actual armed captures and preserves immediate live monitoring.
Unarmed front-track normalization was corrected with a256-channel-unarmed/mono-arm
regression. First shared checkpoints distribute unspecified thresholds across one
regular interval; explicit/disabled choices and regular completed-block cadence
are tested. Nominal one-second frame spacing is unchanged; stalled disk publication
has no hard durable wall-time guarantee and uncommitted memory is not crash-safe.

Five targeted Release groups pass5.74s. All27Debug pass26.45s and all27ASan+UBSan+
LSan pass66.33s, serial after all four builds terminate. Existing Windows headless
targets compile/link only. Pool tests verify1/33/118/256slots, repeated wraps, exact
samples/timing, returned ownership and full-prefix exhaustion; writer tests verify
short first/regular/final cadence. Controller/UI test accepted5-second choice despite
later widget change, invalid request no job/canonical mutation, and focus-safe wheel.
Initial new test errors (inactive inspection of open writer, ready-selector expected
disabled) were corrected; initial copied-recovery check incorrectly included updated
previous-save backup, corrected with explicit backup/canonical evidence. No automatic
restart/edit took place while an exact build/test/native handle remained live.

Owned native20-second normal and controlled4-second absorption both verify all
30,720,000 raw/1,920,000 output samples exactly, Save/reopen, overs and complete finite
timing gates. Production helper admits118x4096=483,328frames(10.069333s). Lane17's
4.003025413s journal stall queues0→46 slabs and recovers without rejected frames.
Declared12-second exhaustion queues0→118 over12.003501364s, retains initiatinglane17/
QueueFull/rejection and independent full raw extents561,152..562,176. All17,988,608raw/
1,122,304 common stereo samples/hashes are exact, canonical unchanged. Actual recovery
on an independent copy restores every32take and all17,988,608samples bit-for-bit with
new IDs/recoveredFrom/origin/alignment/Save-reopen and previous-save backup. Original
files and copied source media/journals stay unchanged; no physical microphone or
speaker route is used.

The normal120-second native retake then passes all184,320,000raw/11,520,000output
samples, peak4.1435 and Save/reopen. Owner complete5625call timing p99.9=4.805532ms/
max13.698197ms(22.526%/64.210%period), source/sink also meet unchanged gates. Maximum
ready queue1slab, flush89.351638ms,journal56.715259ms,writegap98.962843ms. All phase
pairs complete, no audited host callback allocations/frees/blocking locks. Defaults
and prior links unchanged; no owned nodes/links remain. This run and injected
absorption do not prove original underlying filesystem/scheduling causes or sustained
throughput. All eleven inherited failures, originals and source hashes are retained.

[Contract](41-checkpoint-burst-policy.md), [ADR-031](decisions/031-admitted-capture-reserve-and-checkpoint-phases.md)
and [evidence](../tests/results/M2/2026-10-06-checkpoint-burst-policy.json) record scoped
facts. All24equalizer snapshots/heads re-audit unchanged, with no writes. No new
library/license choice, push or publication. All 92frozen acceptance/quality/reference/
F/Q/C/N contracts stay intact and unpromoted. M1/M2, Windows native/Qt/install,
physical/load/filesystem/power-loss/unload/full professional workflows, X004, X005
and all-Europe localization remain required; full goal remains **active/incomplete**.

Next **M2d4c3**: run the unchanged1800-second native workload and full sample/current-
period timing gates with the admitted reserve, serial after all CPU/recovery work.
Retain every phase, clock, source/media/hash and scheduler fact on pass or failure;
no shorter sample or compile gate substitutes for it. Then qualify remaining
independent reliability/platform/physical gates and continue the staged full backlog.


## 2026-10-06 continuation: M2d4c3 native wall/CPU diagnosis

Previous **be93d8f** is **progress**, not completion. Exact long-runhandle47244 was
confirmed live, then exited1 after212.10wallseconds; no timeout/restart assumption
or source edit occurred while it was live. The1800-second post-reserve attempt
stops around196.3audioseconds. Sink previous6727368437+1024 next6727370485+1024
skips1024frames with mapped stereo/capacity1024. Owner has no retained callback
fault and is stopped by control; raw32writers independently join9,422,848frames,
zero rejection/invalid/missing file frames, sink9,420,800. All301,531,136full raw/
18,841,600common output samples/hashes verify exactly read-only, peak4.4291, all
original file contents/extents/canonical/journals/origins unchanged. No trimming,
Save/attachment/recovery changes that original. Owned nodes/links retire and
system defaults/prior links stay unchanged.

Owner complete9202call p99.9=4.542799ms(21.294%period), maximum20.768787ms(97.354%)
fails unchanged80%maximum. Maximum clockposition6727369461 exactly matches skipped
sinkcycle. Callback start10.071982ms after nativeCLOCK_MONOTONIC cycle timestamp;
observed finish30.840769ms after it versus21.333333msperiod. Installed SPA header
warns that timestamp has jitter; this is coincidence evidence, not a proven scheduler/
processor/physical deadline cause. Original thread CPU and full received sinkflags/
rate/ID were not reported, so remain unknown. Every33writer phase pairs complete,
maxready1, writegap92.346822ms, flush68.712531ms,journal70.482890ms. No pool exhaustion;
another reserve increase lacks support from this run.

Add explicit optionalCLOCK_THREAD_CPUTIME_ID to the test-only timing helper, default
disabled and enabled only by the native duration fixture. Two CPU clock reads inside
existing wall interval retain fixed totals/means/maxCPU/maxwall-minus-CPU and CPU
associated with maximum wall callback. Missing/impossible intervals remain visible;
CPU completeness stays separate and never replaces full wall/current-period/60%/
80%gates. No per-callCPUvector: existing1million16-byte elapsed/period storage and
max nativeclock/start/overflow retention remain. Postjoin logs now include full
already-retained sink clocks and maximumXRUN/discontinuity flags. No production
source, route/buffer/parameter/schema/library/license change.

Targeted Release/Debug/SAN deterministic timer tests pass, including associatedCPU,
unknown/impossible/overflow coverage and retained clockflags. Only the test helper/
fixture/unit source changed, so prior27Debug/27SAN production gates stay scoped to
be93d8f; no unnecessary full-suite rerun.20-second native CPU probe verifiesall
30,720,000raw/1,920,000output samples, overs/Save-reopen and complete wall/CPU coverage.
Owner meanCPU2.114234ms,maxCPU5.884950ms withmaxwall5.888962ms, largest remainder18.748us;
source/sink also complete/pass. It does not explain the original20.77msoutlier.
First probe source pins precede output-only fullflag extension; final source/tests
are pinned separately. All builds/probes/retained-verification handles terminate
before any relevant edit or next native launch.

[Contract](42-native-callback-cpu-diagnostics.md),[ADR-032](decisions/032-supplement-native-wall-timing-with-thread-cpu.md)
and [evidence](../tests/results/M2/2026-10-06-native-callback-cpu-diagnostics.json)
retain original launchcommit/binary/sources, clock/timing/phase/media/scheduler and
all twelve unresolved observations. All24EQsnapshots/heads match; no EQwrite/push/
publication. All 92frozen acceptance/quality/reference/F/Q/C/N contracts remain
intact/unpromoted. Full goal active/incomplete; Windows/physical/load/filesystem/
power-loss/unload/full professional/X004/X005/all-Europe gates remain required.

Next **M2d4c4**: unchanged1800-second native workload with supplementalCPU/clockfacts,
then choose any actual processing/scheduling change from an observed outlier, keeping
all original sample/RT/current-period/full-duration gates. Neither short passes nor
uncontrolled host timing establish reliability or waive historical failures.

## 2026-10-06 continuation: M2d4c4 checked audio flush

Previous `5b23173` is progress; full goal remains active and incomplete. The two
preceding continuations were verified waits on exact handle 5205 while the same
1800-second run remained live. This continuation observes its terminal exit 1,
then waits for exact read-only verification handle 83118 to terminate successfully
before any recording source edit or build. No timeout restart or overlapping
native/build/test workload was introduced.

That run stops after roughly 1234.6 audio seconds: lane 25 fills all 118 slabs,
retaining 59,260,928 frames and rejecting 1,024; the other lanes retain 59,261,952.
All 1,896,381,440 full raw samples and 118,521,856 common stereo samples verify
exactly, including raw suffixes/overs, with all 105 original files and canonical
state unchanged. All callback wall/CPU coverage and individual finite gates pass.
Repeated checkpoint work exceeds committed audio spans, accumulating backlog;
maxima include post-stop draining and do not identify one underlying storage cause.
Host context identifies ext4 on an SSD; no hardware/scheduler/power setting changed.

Pinned libsndfile 1.2.2 sf_write_sync only issues an unchecked OS sync; AudioFile
also flushed the same descriptor with error checking. Remove the redundant library
call, preserving header/error checks, checked application fsync/FlushFileBuffers,
and journal flush/atomic rename/directory flush. Frame cadence, reserve, slabs,
RT graph/callbacks and schemas stay unchanged. A Linux owned-inode application
fsync EIO test verifies writer failure, no journal advancement, exact earlier
256-frame recovery/new identity and preservation of the 512-frame original file.
The wrapper does not intercept the libsndfile DSO or prove sync cost savings.

Release five targeted groups pass; all 27 Debug groups pass in 24.01s. Full
ASan+UBSan+LSan passes 26/27 in 75.00s with desktop-ui close timeout at line 1183;
a serial unchanged isolated recheck passes in 8.66s. Preserve both receipts and
the unknown failure-time mode/state; do not label the full sanitizer suite a clean
pass or increase its timeout. All configured Windows headless targets compile/link;
Windows native/Qt/runtime/install gates remain open.

The first short normal native probe skips a sink cycle around 13.5s. All
20,742,144 raw/1,292,288 common output samples verify exactly, original files
unchanged, max disk queue 1. Individual callback budgets pass but source and owner
maxima share the missing cycle: owner starts 14.598437ms and ends 22.007843ms after
native nsec versus a 21.333333ms period. Cycle timestamp jitter and scheduler/CPU
causes remain unresolved. Preserve this run; individual budgets alone do not
establish whole graph continuity.

One bounded unchanged retake passes normal/4s absorption/12s exhaustion. Normal
and absorption each verify 30,720,000 raw/1,920,000 output samples, Save/reopen,
floating overs and complete finite wall/CPU coverage. Absorption queues 46 of
118 slabs. Exhaustion retains lane 17 and all 17,988,608 full raw/1,122,304 common
output samples exactly; independent-copy recovery verifies all 32 takes, timing
origins, new IDs/recoveredFrom, alignment, hashes and Save/reopen/previous backup,
without altering originals or copied source media/journals. These passes do not
resolve earlier continuity/UI/storage failures or qualify sustained duration.

[Contract](43-recording-checked-flush.md), [ADR-033](decisions/033-single-checked-recording-flush.md)
and [evidence](../tests/results/M2/2026-10-06-single-checked-recording-flush.json)
retain fifteen unresolved observations, exact sources/binaries, logs, clocks,
phases and original media hashes. All 24 equalizer inputs and heads re-audit
unchanged; no equalizer writes, new dependency/license, push or publication.
All 92 frozen acceptance/quality/reference/F/Q/C/N contracts remain unchanged
and unpromoted. Full professional/Windows/physical/load/filesystem/power-loss/
unload, X004/X005 and all-Europe localization remain required.

Next M2d4c5: unchanged 1800-second native qualification after every build/test/
recovery/read handle terminates. Preserve exact range/sample/timing gates and
all historical failures. If late sink cycles recur, measure composed cycle timing
and scheduling/processing evidence; if storage backlog recurs, investigate
coordinated publication or filesystem service costs without relaxing durability.

## 2026-10-06 continuation: M2d4c5 native resource/cycle diagnosis

Previous `c60b5d8` is progress: production checkpoint correction, actual flush-error
recovery, scoped tests/evidence and local commit. This turn confirms exact handle
38556 remains live, then terminal exit 1 after 161.37 wall seconds, without source
edits/rebuilds during it. Exact read-only verification handle 53524 terminates
successfully before any related edit. No expired observation causes a restart.

The unchanged 1800-second attempt stops around 145.9 audio seconds on a 1,024-frame
sink clock skip. All 32 raw lanes retain 7,001,088 frames, no rejection or missing
file samples; sink retains 6,999,040. All 224,034,816 full raw and 13,998,080 common
stereo samples verify exactly, including overs/extra raw suffix; all 105 original
files, hashes, journals and canonical state stay unchanged. Defaults/prior links
remain and owned routes retire. No attachment/Save/trim changes that original.

All individual wall/CPU coverage and finite budgets pass. Maximum owner wall
16.722020ms and CPU 16.718543ms occur at the skipped cycle. Start is 4.939578ms
after native nsec, finish 21.661598ms after it versus a 21.333333ms period.
Native timestamp jitter means no exact physical deadline claim. Original kernel/
user/fault/switch components remain unknown. Disk queue max 1, flush 76.732782ms,
journal 91.409512ms: no pool exhaustion; historical storage cause stays unresolved.

Add optional Linux calling-thread getrusage snapshots inside existing fixture CPU/
wall timing. Validate times/signs/bounds and every backwards counter; unknown
intervals remain explicit. Fixed totals/maxima retain user/system time, page faults
and context switches, including context associated with maximum wall callback.
Timeval accounting can differ from thread CPU clock and is not pure DSP time.
An independent maximum from native cycle timestamp to callback end retains its
own clock/start/wall/CPU/resource context; missing/future/jittered/overflow context
is explicit. It is context, not a physical-deadline gate. Existing million16-byte
wall samples, quantiles, coverage and 60%/80% limits stay unchanged. No production
processor, RT query/logging/allocation/lock, routing/buffer/durability/schema or
library/license change; helper defaults off, native fixture explicitly opts in.

Release/Debug/SAN targeted timing tests pass in 0.01/0.01/0.08s, covering every
counter regression, live reads, totals/associated distinct wall/cycle maxima,
overflow/unknown retention and cycle-end boundaries. Four Linux fixture/helper/test
sources change; prior production Debug/scoped sanitizer results remain at c60b5d8,
including the unresolved desktop close timeout. Windows has no source/runtime
change and still needs independent native/Qt/install qualification.

A serial20s probe verifies 30,720,000 raw/1,920,000 stereo samples exactly, overs
peak3.98858, Save/reopen, complete wall/CPU/resource coverage and unchanged finite
budgets. Owner maxwall5.684524ms, maxend-after-cycle8.436455ms; no measured callback
page faults/switches or end beyond native interval. Resource user5.436ms/system0
at the worst callback retains the accounting-resolution caveat. This short probe
does not explain the original long outlier or qualify sustained native operation.
Only parity implementation/gap/evidence annotations change after its source pins.

[Contract](44-native-thread-resource-diagnostics.md), [ADR-034](decisions/034-native-resource-and-composed-cycle-context.md)
and [evidence](../tests/results/M2/2026-10-06-native-thread-resource-diagnostics.json)
retain sixteen unresolved observations, exact launch/binary/source/phase/clock/
media facts. All 24 borrowed equalizer inputs and heads match, no equalizer edits/
new dependency/license/push/publication. All 92 frozen acceptance/quality/reference/
F/Q/C/N contracts remain intact and unpromoted; full goal active/incomplete.
Windows, physical/load/filesystem/power-loss/unload, full professional workflows,
X004/X005 and all-Europe qualification remain required.

Next M2d4c6: the unchanged1800s native workload with resource/composed-cycle facts,
serial after all handles terminate. Choose any actual processing/memory-residency/
scheduling change from observed component evidence, retaining all historical faults
and original sample/RT/current-period/full-duration gates.

## 2026-10-06 continuation: M2d4c6 settled EQ smoothing work

Previous `7ffebbc` is progress: fixture resource/cycle instrumentation, tests/evidence
and local commit. This turn confirms exact native handle64334 live, then terminal
exit1 after398.71wall seconds, and exact reader1314 terminal success before any
related edit. No observation timeout/restart or overlapping native/build/test work.

The unchanged1800s resource-instrumented attempt stops around382.6audio seconds on
1024-frame sink skip. Raw32lanes independently retain18,363,392frames, zero rejected/
missing file samples; sink18,361,344. All587,628,544full raw/36,722,688common stereo
samples verify exactly read-only, peak4.11241, all105original files/canonical/
media/journals/hashes unchanged. No trim/Save/attachment changes that original.
Defaults/prior links stay and owned routes retire.

Owner max20.932047mswall/20.930266msCPU at skipped cycle fails80%maximum (98.119%),
ending22.457169msafter native nsec. Resource context20.392msuser/0.104mssystem,
zero faults/switches during this callback, with coarse-accounting caveat. Owner
59,607minor faults elsewhere/max460/no major faults remain visible, no address or
root-cause attribution. Diskqueue max1, no exhaustion. No governor/NUMA/memory-lock/
affinity/scheduler/device changes. Processing/cache/frequency causes remain unknown.

PreparedEq now counts active band ramps (audio owner,0..64), increments only on
idle-to-active, preserves count on retarget, decrements at completion and clears
on stopped reset. Skip advance while both band/wet counts0; wet-only skips idle
band scan. That old body was a no-op when no ramp remained. Recurrence/interpolation/
first sample/same-frame ingress/fault/reset/bypass-history/headroom/IDs/schema/
version/latency/queues/reserve/durability remain unchanged. No new callback timing
query/allocation/lock/logging or dependency/license. GPL notices/snapshots retained;
DAW adaptation and later upstream candidate recorded without writing equalizers.

A closed-form gain oracle tests3/all64 concurrent, same-frame superseded, overlapping,
retargeted/restarted/wet/reset ramps within5e-7, with bit-identical1/7/31/127/512frame
partitions. Existing independent peaking/headroom/numeric/timestamp/RT gates remain.
Release4targeted groups pass0.84s; all27Debug23.66s and27ASan+UBSan+LSan61.75s pass.
Earlier UIclose failures are not resolved by a later passing suite. All Windows
headless configured targets compile/link, no native/Qt/runtime/install qualification.
All Linux Release targets build consistently after the private header change.

Pinned old/new standalone EQ measurements run serial ABBA after builds/readers
terminate, five repeats per process.3band 32mono before medians342.076/326.273ms,
after312.613/312.491ms;64band1mono before143.721/139.921ms, after131.209/133.461ms.
Final block digests match. An earlier after measurement during builds remains
retained/excluded. Input generation/reset/preparation/digest outside timing;
no full audio oracle, capture/mix/native/whole-pipeline or worst-case conclusion.
This modest reduction does not explain the20.9mswhole-pipeline outlier.

Serial20s normal/4s absorption each verify30,720,000raw/1,920,000output samples,
Save/reopen, overs and complete wall/CPU/resource coverage with original finite gates.
Normal ownermax6.955083ms/maxend-after-cycle10.269865ms; absorption6.523845ms/
8.927618ms.12s exhaustion explicitly retainslane17/all17,988,608full raw/1,122,304
commonoutput samples exactly. Independent-copy recovery verifies all32takes/newIDs/
recoveredFrom/origin/alignment/hash/Save-reopen/previous backup while originals and
copied source media/journals remain unchanged. No physical route or user music
interruption. These short successes do not qualify sustained native or erase faults.

[Contract](45-steady-eq-ramp-work.md), [ADR-035](decisions/035-skip-settled-eq-ramp-work.md)
and [evidence](../tests/results/M2/2026-10-06-steady-eq-ramp-work.json) retain17unresolved
observations, exact launch/binary/source/resource/clock/media and benchmark facts.
All24reviewed EQinputs/heads match, no equalizer writes/push/publication. All 92frozen
acceptance/quality/reference/F/Q/C/N contracts stay intact/unpromoted. Full goal
active/incomplete; Windows, physical/load/filesystem/power-loss/unload, professional
workflows, X004/X005 and all-Europe qualification remain required.

Next M2d4c7: unchanged1800s native workload with settled-ramp change and full resource/
composed-cycle facts, after all CPU/build/test/recovery/read handles terminate. If
outliers persist, measure actual whole-pipeline processing/cache/frequency costs;
do not substitute isolated EQ savings or shorter ranges for original gates.


## 2026-10-06 continuation: M2d4c7 same-callback processing intervals

Previous turn was a verified wait on exact native handle91488. This turn confirms
it live, then terminal exit1 after341.80supervisor seconds, and exact retained
reader28022 terminal success before edits/builds. All related build/test/native
handles subsequently terminate before the next dependent change/experiment.

The unchanged49f28e7 1800s attempt skips1024sinkframes around326.25audio seconds.
Owner25.807899mswall/25.806142msCPU at the skipped cycle exceeds21.333333msperiod,
ends26.668947msaftercycle, and reports25.337msuser/.373mssystem with no faults/
switches in that callback. All32raw lanes retain15,660,032frames/no rejected/missing
raw frames; sink15,657,984. Read-only verification checks every501,121,024raw and
31,315,968commonoutput sample exactly, peak4.18949; all105originalfiles/project/
media/hashes/journals unchanged. A frozen executable is reconstructed with the
exact failed binary SHA. All33disk phase pairs complete, readyqueue maximum6/118,
no exhaustion. No routing/default/host-policy change; owned routes retire.

A separate uninstalled Linux diagnostic executable wraps unresolved cross-object
GNU ABI calls in unchanged static libraries: DuplexBridge, raw CapturePipe pushes,
EqLiveDriver and inclusive PreparedMixGraph. Production APIs/libraries add no
profiling clocks/hooks. One audio writer uses fixed totals and four complete worst
callback snapshots with their own clocks/statuses. Thread-local scope excludes
preparation/source/sink/offline work. Unknown intervals remain explicit. Inclusive
mix contains EQ and must not be added to it; clocks/wrappers add overhead, included
in the unchanged original whole-callback wall/CPU/resource/sample/RT gates.

Release wrapper acceptance passes after correcting an inadmissible test slab to
256frames; production admission was unchanged. Debug3groups1.36s and ASan/UBSan/
LSan3groups3.31s pass. Tests verify actual boundary coverage, exact wrapped/original
bridge samples, terminal/outside scopes, fixed ranking/ties/unknowns and no callback
allocation/free/blocking lock. Current Windows headless build/configuration succeeds
with Linux-only profiling excluded; no Windows runtime/Qt/install qualification.
Earlier full27group results retain their checkpoint scope and do not erase faults.

Serial120s native diagnostic verifies184,320,000raw/11,520,000output samples exactly,
origins/alignment/hashes/Save-reopen/overs peak4.2659. All5625activeblocks have32raw
pushes/33EQ-driver calls/one mix, no unknown intervals. Ownermax11.235597ms and
p9996.067810ms pass current finite gates with complete wall/CPU/resource coverage.
Same-callback tails vary: one10.55msbridge interval spends10.22msCPU inEQ drivers;
another8.83ms spends7.77msCPU inraw capture. Largest11.23ms interval contains both,
reports32minor faults; that cannot explain the earlier25.8ms/no-fault interval.
Averages are not native worst-case/physical evidence; the long gate stays open.

[Contract](46-native-processing-stage-diagnostics.md), [ADR-036](decisions/036-test-only-native-processing-stages.md)
and [evidence](../tests/results/M2/2026-10-06-native-processing-stage-diagnostics.json)
retain eighteen unresolved observations and exact launch/source/binary/clock/phase/
media facts. All24borrowed inputs/heads match; no equalizer edits/dependency/license/
push/publication changes. All 92frozen acceptance/quality/reference/F/Q/C/N contracts
remain unchanged and unpromoted. Full goal remains active/incomplete; full M2,
physical/load/filesystem/power-loss/unload, Windows, professional features, imports,
equipment routing/portability/rights and all-Europe delivery remain required.

Next M2d4c8: run the separate stage diagnostic on the unchanged long workload to
retain interval facts at an actual missed cycle. If aggregation is insufficient,
retain longest individual capture/EQ-call interval/prepared ordinal in that same
snapshot; choose a production change from that evidence. Do not substitute short
passes, CPU accounting or unrelated maximum/average intervals for the real gate.


## 2026-10-06 X002 bounded European inventory audit

Previous goal turn was progress as5e13406. The exact long native handle5818 was
confirmed live before independent localization research/data work; no related
engine source/build or CPU benchmark changed during that run. All62pinned native
source files/binaries/processing libraries remain unchanged. Language inventory
is not a compiled input to the running fixture. Its later failure is separately
retained, not resolved by this language work.

The Council of Europe table has document status2025-12-09 despite an older URL
filename. It is a bounded minority-language cross-check, not the product's entire
European scope. The IANA snapshot is2026-09-17, unchanged SHA from the initial
identifier audit. Thirteen planned language work items raise the register130to143;
registered sco-ulster/oc-aranes variant candidates and four unresolved named
community/catalog decisions become explicit. All original130identities and
qualification states remain, with only two variant-list additions. Every proposed
language/script/region/variant/prefix is registered; unresolved community IDs are
work items, not fabricated BCP47 codes or approved base-language fallbacks.

[Audit](47-europe-language-inventory-audit.md) and
[evidence](../research/europe-language-coverage-audit-2026-10-06.json) distinguish
identifier checks from community mappings, translation, formatting/toolkit and
platform/UI evidence. Every catalog remains planned, native-review/UI-qualified
false; no empty catalog counts as support. No runtime dependency/font/CLDR bundle
or language picker is added. Wider European community/member-language/script/
accessibility coverage and real UI/errors/recovery/help/installer catalogs/reviews/
Linux-Windows workflows remain required. All24borrowed EQ inputs and heads match;
no equalizer write/push/publication; all92frozen contracts remain unpromoted.
Full DAW goal is active/incomplete. Next X002 task is community and actual desktop
string/catalog auditing with reviewed language/formatting selection; sustained
native recording and every other full-product gate remain open.


## 2026-10-06 continuation: M2d4c8 individual processing intervals

Native5818 is confirmed live then terminalexit1 after462.63wall seconds. Frozen
actual5e13406 stagebinary SHA is retained before any rebuild. Reader71685 terminates
with known copied-executable-mode error before execution; after correcting only
the owned copy's execute mode, exact2843 verifies all retained samples terminal
success before source edits/builds. Independent X002 inventory work, committed
b18b0a6, is not an engine input and leaves all62native source/binary/library pins
unchanged. No timeout-based restarts or CPU experiments overlap the native run.

Sink skips2048frames betweencycles183967and183970 around447.32audio seconds.
Owner25.097698mswall/25.095800msCPU atfirstmissedcycle starts17.261859msaftercycle,
ends42.359557ms. Source8.394433mswall/8.391981msCPU shares that clock, starts4.633199ms
late. Both outlierfaults/switches0; ownerreported24.019msuser/1.074mssystem. Matching
stage snapshot raw5.806479msCPU,EQdrivers18.350076msCPU,inclusivemix19.171763msCPU.
Mix includesEQ; aggregates do not locate individualcalls, and CPU/accounting is
not instruction/cache/frequency/interrupt proof. Inter-client lateness is unisolated.
All33diskphase pairs complete,maxready5/118,noexhaustion.

All32raw lanes retain21,471,232frames/no rejected/missing rawframes;sink21,468,160.
Read-only all687,079,424raw/42,936,320common stereo samples exact,peak4.28308;
105originalfiles/project/media/hashes/journals unchanged. Raw suffixes retained,
no trim/attachment/Save oforiginal. Defaults/priorlinks stay;ownedroutes retire.

Separate testhelper retains each selectedcallback stage's longest known individual
wall interval, its pairedCPU and zero-based local invocationordinal. Equalwall
keeps firstcall; inconsistent/unknown cannotreplace validmax, even zero remains
known. Aggregate totals omit ordinals without a selectedcallbackclock. Existing
querypairs reused, no extra clocks/allocations/locks/logs/queues. No production
DSP/backend/API/parameter/schema/latency/durability/staticlibrary change. Morefixed
fields/comparisons/copies have overhead includedinoriginaltiming. Top4still rank
bridgewall, not lateness; missingmatchingclock requiresboundedcycle-selectedprobe.

Newpaired-selection/ties/unknown/inconsistent/zero/snapshot/globalomission tests
join actualABI/boundary/exactaudio/RT checks. Releasepasses0.01s;threeDebuggroups1.31s
and ASan/UBSan/LSan3.40s pass. No newWindows qualification; Linux-onlyhelper changes,
Windows/productioninputs unchanged. Original27group/headless build results retain
checkpoint scope, not native/Qt/runtime/install qualification.

Serial20s native verifies30,720,000raw/1,920,000output samples exact, origins,
alignment/hash/Save-reopen/overs, completewall/CPU/resource coverage and original
finite gates. Ownermax3.815271ms/maxcycleend6.379656ms. All938activeblocks have32raw/
33EQ/1mix calls; individualmaxima known/paired/withinactualordinalbounds. Thisshort
check cannotresolve thesustainedmiss or erase anyearlier observation.

[Contract](48-individual-native-processing-calls.md),[ADR-037](decisions/037-retain-individual-processing-call-context.md)
and[evidence](../tests/results/M2/2026-10-06-individual-native-processing-calls.json)
retain nineteen unresolved observations and exactlaunch/source/binary/resource/
clock/phase/stage/media facts. All24borrowed inputs/heads and92frozenacceptance/
quality/reference/F/Q/C/N contracts intact/unpromoted;no equalizerwrite/dependency/
license/push/publication.143locale workitems are planned, no review/UI/translation
qualification. Full goal remainsactive/incomplete, with physical/load/filesystem/
power-loss/unload/Windows,punch/loop/takes/comping,otherprofessionalworkflows,X004/
X005 andall-Europe delivery stillrequired.

NextM2d4c9: unchanged1800s separate diagnosticwithindividualcallfacts atmatching
missedcycle, then choose boundedprocessing/placement/furthermeasurement fromactual
evidence. Preservecurrentlongrange/sample/period/resource/durability gates; do not
assign a host/instructioncause froman interval alone.

## 2026-10-06 continuation: retained individual-call miss and signing budget

Exact11110 terminates exit1 before actualcafc539 diagnostic binary freeze; reader
35316 terminates exit0 before source/build edits. All62launch source pins and
binary/library hashes match. At56.92audio seconds sink skips1024frames. Same-cycle
owner21.854538mswall/21.851922msCPU ends23.490041ms aftercycle; original maximum
gate fails. Matching rawmax ordinal3 wall4.177203ms/CPU4.175100ms; EQ wallmax ordinal4
1.494642ms/.784833ms. Observer and other bridge intervals remain unisolated, no
root-cause claim. Diskready79/118 without exhaustion; all33phase pairs complete.
Read-only all87,425,024raw/5,459,968stereo samples exact and105originalfiles unchanged.
The[twentieth observation](../tests/results/M2/2026-10-06-individual-stage-sustained-failure.json)
preserves originalnineteen by pinned evidence; no gate relaxed or parity promoted.

Owner reports certificate cost constraints. Both equalizers' current uncommitted
Windows packaging uses signed VB-CABLE while preserving their deferred own-driver
source. Read-only exactfile/head receipts record policy review; all24borrowed
DSP/profile inputs still match. The[DAW decision](49-windows-signing-budget.md)
retains ordinary user-mode WASAPI with no cable/own-driver/purchased-certificate
development prerequisite. Unsigned local artifacts and public trust/install
qualification remain distinct. Free signing/distribution candidates require
eligibility/package/workflow review; none enrolled or published. No equalizer
writes, host-policy changes, dependency additions or purchase.

Full goal active/incomplete; all92frozencontracts unchanged. Next implement a
deterministic desktop-close stale-error reproduction/fix while sustained native
performance, physical/durability/Windows and allremaining full DAW/X004/X005/
all-Europe workflows remain open. A different workflow's pass cannot erase any
of the20retained observations.

## 2026-10-06 continuation: desktop close error baseline

After native11110 and reader35316 terminated, the bounded fixture at f2763e3
published a parameter rejection without Qt event processing, then requested
Close. Exact40017 terminated with the expected clean-close timeout before the
product fix. Each accepted Close now snapshots its published error serial before
committing focused edits. Historical errors remain displayed; only newer errors
cancel an active close. A retry captures a fresh baseline.

Exact99860 targeted Debug workflows pass. Debug five-group handle13965 and separate
sanitized build64080/Release build64019 terminate before further execution.
Sanitized five-group18197 and the Release targeted execution also terminate
successfully. Debug five groups pass7.46s; ASan/UBSan/LSan five groups pass15.11s.
Clean/dirty historical rejection and new Save failure/retry preserve exact state,
prompt counts and all five worker retirement ordering. Optimized desktop builds.
No engine/backend/schema/durability/library change, no native audio/VM execution.

[Contract](50-desktop-close-error-baseline.md),
[ADR-038](decisions/038-close-error-request-baseline.md) and
[evidence](../tests/results/M2/2026-10-06-desktop-close-error-baseline.json) preserve
all20observations, including the earlier unknown-mode UI timeout. The deterministic
bug does not prove that original failure's cause. Original native artifact hashes,
all24borrowed inputs/heads and92frozen contracts match, no parity promoted. No
equalizer writes, new dependencies, signing expense, publication or host changes.

Next implement M2 prepared punch ranges and sample-exact boundary splitting in
monotonic duplex capture with partition/overdub/raw-media/failure tests. Then UI
and native integration; loop/take lanes/comping and all remaining M2/full product
requirements stay in scope. Sustained native quality remains independently open.
Full goal active and incomplete; Windows and all-Europe delivery remain required.

## 2026-10-06 continuation: prepared raw punch range

Previous goal turn made progress as f2763e3 and14b1eff. No prior native/build/test/
reader handle remains live. The unchanged goal, current worktree and contracts
were reread before M2 implementation.

Optional immutable raw-engine `[begin,end)` capture range is admitted inside
playback and requires matching pipe start frames before pools/jobs. Callback
intersection and separate prepared offset pointers preserve full-block playback/
Off/Post-EQ monitoring and aliased raw safety. Exact punch-out finishes pipes
without stopping playback; first captured sample, not preroll, defines origin.
Unknown timestamps remain unknown; overflow faults before capture. Existing
alignment, schemas, durability and worker retirement policy remain unchanged.

Five ranges/five partitions qualify exact mono/stereo samples, one-frame/edge
boundaries, file/live matrix/headroom/slack, nonfinite counts, origin/journal,
Save/reopen/grouped undo/underlying media and worker-owner early completion.
INT64_MAX-adjacent positions, timestamp limits and interruption/recovery are
checked. Initial fixture68412 failed on incorrect journal assumptions; exact
handle terminated before inspecting/correcting expectations. Written568/committed
512 and retained56-frame suffix are now explicit, with capture reason separate
from checkpoint reason. No durability implementation/threshold was changed.

All build handles46048/71296/10236/99585/6667/49559 and tests68412/18184/97512 are
terminal before subsequent source changes. Full Linux Debug29/29 passes46.81s;
Release oracle passes25partition/boundary workflows and zero RT violations.
Linux Debug/SAN/Release and Windows headless core/tests all compile/link.
SAN28/29passes91.61s, including punch; timeline's combined select/prepare refusal
is preserved with actual executable/full logs/source hashes as observation21.
Original branch/snapshot unknown; no memory diagnostic or punch-cause inference.

[Contract](51-punch-capture-foundation.md),[ADR-039](decisions/039-prepared-punch-capture-range.md)
and[evidence](../tests/results/M2/2026-10-06-punch-capture-foundation.json) retain
all21observations and original native receipts. All24borrowed inputs/heads and
92frozencontracts match, no F/Q/C/N promotion, equalizer writes, dependency/license
addition, native/VM test, host-policy change, purchase or publication.

Next reproduce controller-published selection before GUI poll, then latency-aware
musical locator preparation/persistence and UI/native punch acceptance. Raw-frame
capture is not a full musical-locator or overlapping-take workflow. Auto monitoring,
loop/take lanes/comping and allremaining M2/full DAW/X004/X005/Windows/all-Europe
requirements stay in scope. Sustained native gate remains independently open;
full goal active/incomplete.

## 2026-10-06 continuation: published selection before UI tick

Core punch work is committed6d2bf26. All prior handles are terminal before a
deterministic GUI fixture completes Open without Qt event processing. Exact88849
fails selection of an already published ID; original executable/input hashes are
frozen before product edits. Selection now polls current canonical state and the
timeline/inspector redraw shares its captured snapshot. Canonical IDs/content
remain, and selection never starts audio.

Exact94446 terminates with a known downstream test-only row-count mistake3vs2,
then fixture expectation is corrected. Exact82263 passes before all three build
handles82075/32871/56680 start and terminate. Debug three desktop groups pass7.62s;
Release deterministic fixture passes; serial full ASan/UBSan/LSan57394 passes
29/29in66.30s. No source edit/rebuild overlaps a live related test/build. Punch
executables/Windows core are unchanged; no engine/schema/durability change.

[Contract](52-published-track-selection.md),
[ADR-040](decisions/040-published-selection-snapshot.md) and
[evidence](../tests/results/M2/2026-10-06-selection-before-ui-poll.json) retain all
21observations. Original21branch/admission state unknown, so the deterministic
related defect does not prove its exact cause. All24borrowed inputs and92frozen
contracts match, no F/Q/C/N promotion, equalizer writes, new dependency, signing
expense, host-policy change or publication. No native/VM test in this checkpoint.

Next serial owned PipeWire default-recording regression after the engine punch
change, then latency-aware musical locator preparation/persistence and desktop/
native punch. Full punch/Auto/loop/takes/comping and sustained/physical/Windows/
professional/import/profile/all-Europe gates stay required. Full goal incomplete.

## 2026-10-06 continuation: default native recording and committed signing policy

Exact28857 is terminal exit0 before subsequent edits. At1fd9e9e, the serial plain
owned PipeWire20-second normal fixture verifies30,720,000raw/1,920,000output samples
with zero difference, no missing frames and intact float headroom. Joined raw
journals/origins/hashes/alignment and Save/reopen pass. All three callback roles
have complete wall/CPU/resource coverage and unchanged finite gates pass; no
current-cycle overruns, maximum capture queue1, all33worker phase pairs complete.
No RTallocation/free/blocking-lock hits. All64source pins, actual executable and
11processing library hashes remain exact after termination. Default routes remain
unchanged and owned nodes/links retire; zero pre-existing links means populated
prior-link preservation is not exercised.

[Native receipt](../tests/results/M2/2026-10-06-punch-default-native-regression.json)
qualifies only ordinary/default full-range recording after punch infrastructure.
No punch window, physical device,1800-second sustained or Windows qualification.
All21historical observations remain, with original21artifact and prior20receipt
hashes verified unchanged. All 92frozencontract projections remain exact and
F/Q/C/N unpromoted.

The owner signing-budget constraint remains in force. Read-only equalizer review
now observes committed public28e6f47/Studio70b0d14 policy/build/setup files; all24
borrowed DSP/profile/editor inputs remain exact against newHEADs and snapshots.
[Updated budget review](49-windows-signing-budget.md) records both newHEADs and
changed path inventories. No platform code/driver/archive/setup dependency
imported, equalizer write/branch change, purchase, VM or DAW publication.

Next implement a prepared per-lane latency-aware musical punch locator plan with
bounded postroll and exact origin/alignment/fault tests, then persistent state and
desktop/native punch workflows. Auto monitoring, loop/takes/comping, sustained
performance and every frozen/full-product/Windows/import/profile/all-Europe gate
remain required. Full goal active and incomplete.

## 2026-10-06 continuation: per-lane latency-aware punch preparation

Previous goal turn progressed through66ee15a, committed scoped native evidence and
latest equalizer signing-policy review. No prior owned handle remains live before
new source edits. The full goal, worktree, punch contracts and frozen matrix are
reread, without changing scope.

Control-only musical project-frame locators derive per-track raw windows from
declared input latency, checked overflow and bounded required postroll. Caller
options/specs stay unchanged. Fresh bindings admit separate windows and publish
each first-captured sample's origin; aggregate origin is earliest regardless of
binding order. All new offsets preflight before publication. Full-block file/live
monitoring, aliased raw safety, per-lane completion and native-join/disk retirement
stay intact; no RTallocation/free/blocking-lock/IO/log/newqueue or schema/durability
change. Ordinary unwindowed completion reason remains unchanged.

Independent delayed mono/stereo samples prove timeline alignment across28latency/
boundary/partition workflows, plus25existing raw workflows. Tests cover separate
origins, automatic postroll, real disk workers, grouped undo/Save-reopen, original
media/journals and interruption/recovery before/between/inside/after lane windows.
One initial build56069 stops on the fixture's incorrect Clip.frames member; exact
handle is terminal before correcting to existing lengthFrames. Corrected40994 and
first debug33481 pass before final count/admission checks. All subsequent four
builds94975/71496/64633/18452 terminate0 without warnings. Release11,079,850-check
oracle passes; serial Debug27515 passes29/29 in27.93s, SAN25936 passes29/29 in75.36s.
Windows headless compile/link supplies no runtime/Qt/native/installer evidence.

After every build/test is terminal, serial owned native95926 terminates0 on the
20s ordinary/default full-range workflow:30,720,000raw/1,920,000output samples exact,
joined origins/journals/hashes/alignment/Save-reopen and all complete finite timing
gates. Maximum owner3.129286ms wall/3.125028ms CPU, no cycle overrun, maxcapturequeue1,
all33disk phase pairs complete. Default routes unchanged, owned routes retired;
no prior links present. Source64/executable/library pins verified after termination.
No competing owned CPU experiment or compiled-source edit during native execution.
This is no native punch, physical,1800s or Windows qualification.

[Contract](53-latency-aware-punch-locators.md),
[ADR-041](decisions/041-per-lane-musical-punch-preparation.md) and
[evidence](../tests/results/M2/2026-10-06-latency-aware-punch.json) preserve all21
observations and actual original21/prior20receipt hashes. The prior default-native
executable is frozen before rebuilding. Equalizer24inputs/observedheads match
before/after;92frozencontract projections exact, onlyP004implementation notes/
evidence extended, no F/Q/C/N promotion. No dependency, equalizer write, signing
expense, host policy/default-route change, VM, purchase or publication.

Next version/persist desired project-frame locators, expose recording controls
and admitted postroll, and qualify owned native per-lane punch origins/alignment.
Beat/tempo conversion, manual punch/Auto monitoring, stop/continue modes, looping,
take lanes/comping and every original sustained/physical/Windows/full-product/
import/profile/all-Europe gate remain required. Full goal active/incomplete.

## 2026-10-06 continuation: canonical punch settings and desktop recording

Schema1.4 persists desired project-frame punch locators; schemas1.0–1.3 default
punch off without changing existing identities or rewriting on Open. Strict
boolean/integer/key/overflow checks, transactional edits, retained disabled
locators and undo/redo cover exact state through INT64_MAX. Recording journals
and alignment remain unchanged.

The desktop provides a labeled range dialog and toggle, uses canonical settings
at the recording barrier and displays admitted latency postroll. Initial enable
or Open suggests an arm only when empty; subsequent clearing stays empty.
Prepared/busy generations reject settings changes, incompatible canonical
locators retire their owner, and Stop clears the prepared-end display. Explicit
routes, joined receipt handoff and grouped attachment remain required. Codec,
controller and desktop tests use synthetic endpoints with real graph readers and
disk workers; they establish no physical or native punch qualification.

Initial full Debug passes29/29in26.04s. Visual inspection found clipped validation
feedback; its original screenshot is retained and the layout fix passes both
text-bounds and screenshot inspection. Final desktop/discovery checks pass4/4
in7.98s. An existing optimized Unix crash-fixture pipe-write warning was fixed by
checking the readiness write, preserving its original warning log. Detailed final
sanitizer/optimized build and test results are in the
[evidence](../tests/results/M2/2026-10-06-project-punch-controls.json).

[Contract](54-project-punch-controls.md) and
[ADR-042](decisions/042-project-punch-settings.md) retain all21historical
observations. All24borrowed inputs still match current equalizer HEADs; no
acceptance/quality/reference or F/Q/C/N projection changes among92families.
The owner's signing-budget constraint remains in force; Windows development
uses the documented direct user-mode plan without a purchased certificate.
No new dependency, equalizer write, policy/default-route change, native callback,
VM, signing expense or publication occurred in this checkpoint.

Next qualify owned native per-lane punch boundaries/origins/alignment, then
manual/Auto/tempo/loop/take/comping workflows. Sustained performance, physical
latency, Windows native/Qt/installers, import/profile/all-Europe and all frozen
professional-suite requirements remain open. Full goal active/incomplete.

## 2026-10-06 continuation: owned native punch origins and alignment

Previous turn75444f6 is progress: canonical schema1.4 punch settings, desktop
controls and final29-group sanitizers pass. All previous owned handles are
terminal before this turn's compiled fixture changes. The objective, current
worktree and required next native gate are reread; full scope remains unchanged.

A separate explicit native punch target uses production owners and independent
playback-anchor/lane-clock observations. Its32 source channels simulate declared
latencies4097/0/41/200; desired nonaligned window48150..144164 is captured and
attached exactly while full preroll/postroll file/live monitoring continues.
First build73912 terminates1 on fixture-only use of RecordingResult fields rather
than its existing spec; corrected only after termination. Corrected Release64449
and SAN64753 terminate0 without warnings/errors. Optimized oracle76151 passes;
ASan/UBSan/LSan oracle18577 passes with real readers/disk workers and no diagnostic.

After both are terminal, serial owned native7717 terminates0:3,072,448 raw and
296,248 stereo samples exact, output peak3.6612954, separate nonaligned origins,
per-lane final journals/hashes/alignment, original-media preservation and grouped
undo/redo/Save-reopen. Owner maximum2.031224ms elapsed/2.027194ms CPU; all roles
have complete wall/CPU/resource/cycle coverage, finite gates pass, cycle overruns0
and instrumented RTallocation/free/blocking-lock hits0. Defaults unchanged,
owned routes retire; zero pre-existing links means populated-link preservation
is not exercised. All67source/actual-executable/11library pins verified after
termination. No competing owned CPU test or compiled source edit overlaps native.

A stricter supervisor check subsequently requires complete native cycle coverage;
actual evidence replays successfully and11 altered receipts are refused. Original
supervisor and actual native executable are frozen before this verifier-only
change. Production source remains unchanged. [Contract](55-native-punch-qualification.md),
[ADR-043](decisions/043-native-punch-playback-anchor.md) and
[evidence](../tests/results/M2/2026-10-06-native-punch.json) retain all21historical
observations; no prior cause or resolution inferred. All24borrowed equalizer inputs
match before/after,92frozencontracts remain exact, onlyP004implementation/evidence
extends. No dependency, equalizer write, signing expense, host/default-route policy
change, VM or publication. Full goal active/incomplete.

Next integrate and qualify native desktop canonical punch Prepare/Record/Stop/
joined grouped attachment, then interruption/fault, manual/Auto/tempo/loop/take/
comping workflows. Physical/backend latency, sustained/non-flat performance,
Windows native/Qt/installers and every professional/import/profile/all-Europe
requirement remain required.


[Saved input latency controls](60-input-latency-controls.md) now preserve distinct
per-track declarations in strict schema1.5 and accepted selected/shared preparation.
Stable-ID Undo/Redo, passive Save/reopen, prepared/active retirement, zero-frame
source trimming and desired musical punch attachment have bounded acceptance.
Debug29/29, ASan/UBSan/LSan29/29, Windows core compilation and a short isolated
32-track native desktop run pass. Every raw/output sample and lane origin matches;
callback timing gates pass and four existing links/default metadata are preserved.
All24 borrowed inputs and92 frozen contracts remain unchanged/unpromoted. Prior23
observations plus two initial assertions remain retained (25total). Next implement
manual punch/Auto monitoring, then tempo/loop/take lanes/comping; all physical,
sustained, native Windows, localization, fault/recovery and full-suite gates remain.


## Recording-only Auto monitoring (2026-10-06)

Previous goal implementation turn was **progress**: saved declared input-latency
controls, required-check PR merges and verified source backups. The branch-protection
request independently verified live rules without mutating the working source.
This turn adds explicit saved Auto during recording, strict schema 1.6, desired
sample-boundary input/file selection independent of delayed raw windows, continuous
EQ history and mixed armed-mode UI labels. [Contract](61-auto-recording-monitoring.md),
[ADR 047](decisions/047-recording-only-auto-monitoring.md) and
[receipt](../tests/results/M2/2026-10-06-auto-recording-monitoring.json) identify actual scope.
Thirteen synthetic partition workflows, four Auto interruption/recovery cases,
strict old-schema rejection, passive UI save/undo and high-frame preflight pass.
Debug and ASan/UBSan/LSan pass 29/29; Windows core compilation passes without a
native claim. The isolated 32-arm native desktop test passes with eight Auto arms,
3,072,448 raw and 480,000 stereo samples exact, continuous EQ receipt replay, zero
RT allocation/free/locks and complete short wall/CPU/resource/cycle gates. Two
external links/default metadata are preserved. All 152 launch pins remain stable;
12 altered receipts fail. The prepared Auto screenshot is retained in source.
No new failure occurred; all 25 historical observations remain preserved.

Cubase's record-running policy motivates this bounded workflow; other monitor
policies and complete Bitwig Auto transition behavior remain required. All 92
frozen contracts retain their F/Q/C/N acceptance projection and remain unpromoted.
The 24 borrowed equalizer inputs and retained snapshots match; both equalizer
checkouts remain read-only. No dependency is adopted and no release is uploaded.
Next: sample-boundary manual punch commands during running playback, bounded
capture-resource/writer handoff, ordered Stop/fault recovery and grouped admission;
then armed/stopped/tape monitoring and loop/take/comp workflows. Native Windows,
physical/sustained timing, X004/X005 and European translation/review/UI gates remain
open. The full goal remains active and incomplete.


## Deferred capture start for manual punch (2026-10-06)

The previous goal implementation made **progress** by adding the deferred capture
primitive and completing builds; the intervening branch-protection request read
back the live rules without source changes. This continuation revalidated that
work and completed acceptance. [Contract](62-deferred-capture-start.md),
[ADR 048](decisions/048-deferred-capture-publication.md) and
[receipt](../tests/results/M2/2026-10-06-deferred-capture-start.json).
Prepared pools now publish their exact raw start once from audio without mutating
preparation metadata. Disk workers consume immutable resolved snapshots and retain
prefilled full/partial slabs, including a capture completed before job creation.
Exact samples, origins, alignment, journals, grouped Undo/Redo and passive Save/reopen
pass. Unresolved and mismatched job specifications fail before media creation.
Existing fixed-start owners explicitly refuse deferred bindings. No project/journal
schema change or new dependency is introduced.

Debug and ASan/UBSan/LSan pass 29/29; focused groups pass 5/5. Windows core and
media cross-builds pass; native Windows remains open. The isolated fixed-start
32-track native regression passes with 3,072,448 raw and 480,000 output samples
exact, zero instrumented RT allocation/free/locks and complete short timing gates.
Two external links/default metadata are preserved and owned routes retire. All
152 native pins remain stable; twelve altered receipts are refused. This verifies
regular-path regression only: no deferred native/manual-punch qualification.
All 25 historical observations, 13 retained failure artifacts, 24 borrowed inputs
and 92 unchanged/unpromoted frozen contracts remain. Equalizer sources are read-only.
The full goal stays active and incomplete.

Next implement bounded reliable manual-punch command/acknowledgement and prepared
take slots with checked per-lane raw starts/postroll, continuous mix/EQ, off-audio
writer retirement, repeated takes and verified grouped attachment. Then qualify
native/desktop/Windows and ordered Stop/fault recovery. All sustained/physical,
full reference monitoring, loop/take/comping, import/profile, packaging and
European translation/review/UI gates remain required.


## Runtime manual punch in the engine (2026-10-06)

Previous goal turn was **progress**: deferred capture publication, full acceptance,
protected PR #7 and verified source backups. This turn implements the owner that
uses those pools while the same mix/read generation and continuous EQ keep running.
[Contract](63-manual-punch-engine-owner.md),
[ADR 049](decisions/049-manual-punch-slot-lifetime.md),
[receipt](../tests/results/M2/2026-10-06-manual-punch-engine-owner.json).

Up to eight outstanding globally admitted slots can be replenished after joined
retirement; 64 command credits guarantee non-lossy FIFO receipts. Logical B/E and
per-lane B+L/E+L are separate. Earlier finished lanes remain complete while other
lanes capture postroll. Control-owned storage is not read by audio, queued immutable
pointers retain lifetime credits, and audio drops references before publishing
retirement. Construction/disk/project edits/reclamation stay off callbacks.

Three repeated-take workflows match an independent continuous nonflat/flat oracle
exactly at 256/127/31-frame partitions, with aliases, four delays, in-block windows,
EQ changes, late writers, grouped Undo/Redo and Save/reopen. Nine interruption
variants retain independently exact durable raw prefixes; twenty additional takes
are joined/reclaimed/replenished concurrently with an audio thread. Exact final
boundary receipts and empty delayed Stop are covered. Instrumented allocation,
free and blocking-lock hits remain zero in both callback threads. Native audio
and native Windows execution are not claimed by these synthetic workflows.

The initial output assertion and subsequent journal-fixture assertion are retained
as observations 26 and 27, including original executables/source/logs and separately
identified debugger replays. Original unlogged terms are not reconstructed as
original observations. The first replay/code inspection exposed incomplete-lane
retirement accounting; the second exposed a fixture comparison of runtime versus
recovery pool sizes. The earlier 25 observations and sustained failures remain.
P004 implementation evidence extends; all 92 frozen acceptance/quality/reference/
F/Q/C/N projections remain unchanged and unpromoted. All 24 borrowed equalizer
inputs are audited; no equalizer source edits or new dependency are introduced.

Next enforce production off-audio worker/group ownership, errors and empty-take
policy automatically, then qualify an owned native adapter and desktop manual
controls. Finite-generation/FIFO scheduling is not the full indefinite/loop/seek/
quantized recording product. Windows runtime/Qt/installers, physical/sustained,
full reference monitoring, loop/take/comping, X004/X005 and European translation/
review/UI gates remain open. The full goal remains active and incomplete.

Final Debug30/30 (27.74 s), ASan/UBSan/LSan30/30 (74.65 s) and Windows media
compilation pass. No native runtime, desktop manual control or sustained/physical
qualification follows from this checkpoint.

## Manual recording disk/group ownership (2026-10-06)

The previous implementation goal turn was **progress**: manual-punch engine
ownership, scoped synthetic acceptance, protected PR #8 and verified source
backups. The intervening branch-protection request revalidated the existing
protected workflow; it did not count as implementation progress. This turn adds
[production disk/group control](64-manual-recording-control-owner.md),
[ADR 050](decisions/050-manual-recording-control-lifetime.md) and
[receipt](../tests/results/M2/2026-10-06-manual-recording-control-owner.json).

The framework-independent owner starts workers only from published exact capture
configs, automatically joins consumers before capture reclamation, preserves
inactive checkpoint/origin/identity/hash evidence and exposes bounded groups.
Application reply credits survive bridge-to-control transfer until explicitly
consumed. Eight unconsumed groups apply backpressure. Grouped canonical edits,
Undo/Redo and Save/reopen do not reset the prepared playback/EQ generation.
Empty, failed, canceled and independent partial prefixes remain explicit.

Three synthetic real-reader/writer repeated-take workflows have zero output
oracle difference and float peak2.14813, with zero instrumented callback allocate/
free/blocking-lock hits. Tests include delayed/completed-before-service startup,
aliasing, EQ changes, early/late empty/mixed takes, construction/active/retired disk
errors, Stop/device loss/cancel, pool exhaustion, reply/result pressure and twenty
concurrent production control/audio slot retirements.

Observation28 retains the original active-write-failure combined assertion,
frozen executable/source/logs, preserved original project/journals and separate
debugger replay. A valid initial nonfinalized zero-frame checkpoint has no origin;
validation now permits that case while keeping positive-prefix/finalized origin
checks. Original unlogged individual terms remain unknown; replay evidence is
separate. All prior27 observations, including sustained native failures, remain.
All 92 frozen contracts/projection and24 borrowed equalizer inputs remain unchanged;
no schema change, new dependency, equalizer mutation or F/Q/C/N promotion.

Next: an owned native PipeWire manual-recording adapter with control-worker joins,
route cleanup, exact continuous output/origins, error/prefix recovery and native
deadline gates. Then integrate canonical desktop manual controls, including fast
monitor edit/Undo binding verification. Finite prepared transport/FIFO is not full
indefinite/loop/seek/quantized record-mode parity. Independent Windows native/Qt/
installers, sustained/physical, full monitoring, tempo/take/comping, imports,
profiles and all-Europe translation/review/UI requirements remain open. The full
goal remains active and incomplete.

Final Debug31/31 (28.86 s), ASan/UBSan/LSan31/31 (94.26 s) and Windows media
cross-compilation pass. No native, desktop manual-control, sustained or physical
qualification is inferred from these functional checks.

Observation29 retains the first protected PR9 Linux CI failure (30/31 pass),
tested commit4bd9606, downloaded test logs and exact source. The concurrent test
logged only a failure flag, so its original callback/oracle status is unknown.
Remote executable and test project were not uploaded by that workflow and are
unavailable; do not claim they were retained. A separate local diagnostic under
one-CPU affinity passed and does not identify the remote cause. Source inspection
found a 0.2ms callback sleep for256 frames at 48 kHz (about27x accelerated) and a
200000-frame finite horizon. The functional fixture now uses nominal256/48000
cadence and a2000000-frame horizon, with first-status diagnostics and unchanged
strict Running/output/zero-RT checks. This corrects an unsuitable functional
workload; the exact original remote failure subtype remains unproven. All29
observations remain retained, and native sustained timing gates remain open.

Current cadence-qualified Debug31/31 (28.86 s), ASan/UBSan/LSan31/31 (77.09 s)
and Windows acceptance compile pass. Earlier results and CI failure remain in
the dated receipt; neither observation28 nor29 is erased.

Protected PR review additionally identified an unserviced late-cancellation gap:
the initial owner skipped all unstarted consumers when canceled, even if their
retired pipes contained positive accepted raw prefixes. The owner now starts and
drains those bounded prefixes after callback join, retaining finalized media and
verified checkpoints while still marking the group/lane Canceled and refusing
implicit/partial canceled adoption. Already running consumers keep their existing
checkpoint cancellation policy. Zero-frame late lanes still create no fake job.
New real-writer tests cancel before any service for two valid lanes and a mixed
valid/delayed-empty group, verify every retained sample/checkpoint, recover the
prefix independently, and preserve the saved original. This is a review finding,
separate from the29 retained runtime/CI observations.

Final late-cancellation Debug31/31 (28.99 s), ASan/UBSan/LSan31/31 (76.63 s)
and Windows media cross-build pass. Previous qualification/failure evidence remains
retained; native/runtime/desktop manual recording gates are still open.

## Native manual recording checkpoint (2026-10-07 UTC)

The previous implementation turn made progress through manual disk/group ownership,
protected PR9 and verified backups. The intervening protection request only
revalidated remote settings; it did not make implementation progress. Current
checkout and the specific pending native-fixture build handle64094 were checked;
that handle is terminal0. Implementation then continued from its existing changes.

[Native adapter](65-native-manual-recording.md), ADR051 and the dated receipt
qualify complete repeated and late-serviced takes on owned routes. The one
serialized control owner performs disk service away from audio/GUI; actual native
callback joins precede raw finishing, worker joins and result reclamation. The
32-arm source/master sink fixture keeps one nonflat graph alive across three
manual windows, canonical grouped Undo/Redo and replenished preparation. Early
and late native runs each have exact raw/origin/journal/output evidence, floating
headroom, zero callback allocate/free/blocking-lock hits and all finite native
wall/CPU/resource/current-cycle gates. User defaults/two pre-existing external
links survive; all owned nodes/links are removed. No physical/sustained claim.

Broader regression found observation30: a combined desktop export sample/file-hash
assertion. Its original executable/source/logs are retained; original hashes and
project were unavailable. Separate delayed replay proves identical audio with
different PEAK timestamps. Observation31 preserves the first correction's RF64
repeatability assertion, executable/source/logs; its missing original iteration/
hashes remain unknown. Separate initialized RF64 replay proves an unintended
PEAK allocation by the disable command when peak_info was initially absent.
[Default exports](66-repeatable-export.md)/ADR052 now disable PEAK only for WAV
and keep RF64's default absence, with cross-second sample/byte/header regression.
All31 observations and prior sustained failure uncertainty remain retained.

Next: adapter-specific interrupted/faulted native manual take acceptance and
checkpoint recovery, then canonical Qt manual controls with bounded service-worker
messages and rapid monitor edit/Undo binding checks. Windows native/Qt/installers,
indefinite/loop/seek/quantized transport, full monitor/take/comping, physical/
sustained, imports, profiles and European translation/review/UI gates remain open.
All 92 frozen contracts stay unpromoted; the full goal remains active/incomplete.

Final local Debug31/31 (32.49 s), ASan/UBSan/LSan31/31 (82.66 s), both
new sanitized synthetic manual oracles and Windows media cross-compilation pass.
The optimized current-tree native fixture is byte-identical to the executable
used by both retained native runs; the unrelated export processor is not linked
into it. No Windows execution, desktop manual-control, sustained or physical
qualification follows from those checks.

## X006 and recording-fixture CI observation32 (2026-10-07 UTC)

The owner added lower-budget studio targeting with arbitrary finite track counts,
including studios with substantial hardware. [X006](67-track-scalability.md) and
ADR053 define no fixed product/license ceiling on total project tracks, separate
resource/real-time/hardware admission and staged model/parser/graph/media/UI/freeze
and Linux/Windows qualification. Current256-track limits remain implementation
gaps. Six explicit owner acceptance workflows are unverified; the frozen92-contract
projection and status axes are unchanged. This extends the full goal.

The first protected PR11 CI run37551952667 on106cccfd4931a33a4d7be401a366da111abf9ea9
passed30/31 tests but recording-recovery reported only the combined assertion
"Concurrent ten-second capture incomplete". Original correct/completion/extent
terms were not logged. Downloaded original test logs/full run and exact Git source
are retained; remote executable/project were not uploaded and are unavailable.
Implementation code on that head was unchanged from passing PR10. Do not claim
that a later pass or local binary identifies the original individual term/cause.

Source inspection found a100us sleep per127 samples at 48 kHz (2.646ms of audio):
about26x nominal requested throughput, with platform-specific Windows1ms pacing.
The functional concurrent writer test now sleeps for its actual block/sample-rate
duration outside the marked callback, keeps strict complete480000-frame/raw/
journal/zero-RT assertions, logs the failure terms and preserves failed projects.
This corrects an unsuitable synthetic workload; it does not qualify native timing,
prove the original remote subtype or resolve sustained recording failures. All32
observations remain retained. See [receipt](../tests/results/repository/2026-10-07-track-scalability-ci-observation.json).

Native manual fault/recovery remains the immediate implementation task; X006
scaling, independent Windows/runtime/installers and every other full goal gate
remain required. No F/Q/C/N promotion, dependency or schema change.


## Native manual fault/recovery checkpoint (2026-10-07 UTC)

Progress: opt-in actual native manual-adapter failure/recovery fixture, independent
nonflat output/raw oracles and diagnostic route retention. All nine synthetic modes
have scoped acceptance; finite native successes are recorded separately. Observations
33–40 are retained, including an original per-channel512-frame delay and an owner
13.88ms predominantly-CPU callback overrun at512/48k. Native early Stop and retired
hash recovery remain unqualified; a later passing unserviced-cancel run does not
resolve the original alignment failure. See [contract](68-native-manual-fault-recovery.md)
and its dated receipt for exact per-attempt sources/binaries and historical links.
Next: bounded per-channel buffer/clock and stage timing diagnostics, then bounded
canonical Qt manual controls. No production/schema/dependency change or F/Q/C/N
promotion; all92 frozen contracts and X004/X005/X006/Europe/Windows remain required.
The full goal remains active and incomplete.

## Native manual port/stage diagnostics (2026-10-07 UTC)

Progress: bounded source/recorder marker traces and manual GNU linker stage
instrumentation, transparent processing tests, Debug/sanitizer synthetic checks
and two scoped native successes. Original 41's startup source-buffer verifier
refusal is preserved before its classification correction. Twenty-three altered
traces are refused against both synthetic and native references. Finite native
retired hash recovery now has evidence; early Stop and earlier channel-delay/CPU
causes remain open. See [contract](69-native-manual-port-tracing.md) and receipt.
Next: bounded off-GUI manual control ownership with priority Stop/Cancel and
actual-widget qualification. No production/schema/dependency or F/Q/C/N promotion.
All 92 frozen contracts, independent Windows, X004/X005/X006 and Europe remain
required. The full goal remains active and incomplete.

Review R3 reproduced acceptance of a corrupted retained whole-bridge record;
the strengthened verifier now checks whole-callback cost/coverage/ranking,
retention/uniqueness, clock time/rate and retained sums. Original reproduction
and first verifier are preserved; all six observed traces pass the new gates.

## Priority manual Stop/Cancel checkpoint (2026-10-07 UTC)

Independent one-generation atomic signals terminate audio while real disk startup
is held, with immediate new-command/preparation refusal and reliable terminal
replies. Synthetic pre-first-callback and 32-arm held-startup workflows pass Debug,
Release and ASan/UBSan/LSan; Windows media compilation passes. Finite owned native
Stop/Cancel each have 97 callbacks, complete 32-channel advancing correspondence,
25,408 raw/recovered and 98,304 mixed samples verified, zero RT allocation/free/
locks/late cycles. Fifteen altered receipts are refused for each native reference.

Original 42's checkpoint-filename test error and43's observer teardown failure were
frozen before correction; original missing terms remain missing. All 43 observations
remain retained and earlier alignment/CPU/sustained causes unresolved. See
[contract](70-manual-priority-interruption.md) and receipt. No parity promotion.

Next: bounded serialized Qt manual controller with reliable replies/results,
canonical adoption, defined late-Cancel policy, shutdown and actual-widget monitor
edit/Undo tests. X006 remains planned with current 256 limit; all92 contracts,
X004/X005, Europe and independent Windows/installer/physical/sustained gates stay
required. The full goal is active/incomplete.


Review corrected interruption between verification lanes and a position-only
producer race, with deliberate original 44/45 regressions. The first final-source
native Stop (original 46) failed raw/source 23 markers by one256-frame quantum;
all 1,651 lane17 saved samples match that extra delay. Original files/routes/traces
and independent analyses are retained. It is not qualified or compensated. A
separate final Cancel passes195 callbacks and full32-channel/raw/output gates;
this does not explain Stop or earlier delays. Final Debug/Release/sanitizer2/2
and Windows media compilation pass. All 46 observations remain visible.
Next diagnose original 46 before completing Qt manual ownership/late Cancel/UI.
Full92 contracts and X004/X005/X006/Europe/Windows/sustained gates remain open.

## Native buffer acquisition checkpoint (2026-10-07 UTC)

Previous user-question turn was a status restatement; this continuation makes
production and acceptance progress. [Native buffer leases](73-native-buffer-acquisition.md)
replace the Linux convenience getter with synchronous readiness checks, certified
mono F32 extents/chunks and explicit native object return after processing. Every
logical channel remains represented when unavailable. Production callbacks gain
no allocation, locks, waits, logging or clocks; no DSP, persistence, default-device
or equalizer working-tree changes are made.

The held source23 startup test now exercises actual production deferral with zero
SDK dequeues and no test API suppression. That run and ordinary early Stop/Cancel
pass all original raw/recovery/output,32-channel current-cycle, ownership, RT,
priority and finite deadline/cycle gates. Each verifies37,696 raw and recovered
samples and99,328 nonflat stereo output samples. Local Debug38/38 pass in44.06s;
Release helper and explicit ASan/UBSan/LSan helper tests pass. Hosted native-off
checks do not execute the new C++ helper test. The pinned1295-payload archive
verifies all three runs after relocation, refuses60 changed evidence cases and
retains the original receipt unchanged without native replay.

Ordinary repeated-take failure48 is retained before diagnosis with its full298-file
project/log/source-build evidence. All96 lane files /1,557,696 raw samples match
expected timing. Its452,608 saved stereo prefix samples independently match the
original nonflat prepared graph oracle exactly. The owner exceeds the unchanged
80% callback budget and ends after a native cycle; the sink subsequently detects
a skipped cycle. Its CPU-stage cause remains unobserved, and the full target,
Save/reopen, grouped Undo/Redo, recovery and sustained timing are not qualified by
this failed run. Original46/37 missing IO/queue terms and earlier failures remain.

The [receipt](../tests/results/M2/2026-10-07-native-buffer-acquisition.json) pins
compiled sources, exact native builds, complete original generated media/logs,
SDK lifetime assessment, analyses, test logs and unchanged24-input reuse audit.
All 92 frozen projections remain at
`8f82e44e0eac1facada5474c5a864a64cdd63947595cd73ef738c1c928c50d28`,
with zero F/Q/C/N promotion. Current256-track limit and X004/X005/X006, Europe,
independent native Windows/physical and full parity requirements remain open.

Next implementation task: bounded repeated-take stage observation to locate its
CPU outlier while preserving the strict original gates; finish actual manual Qt
controls and late-Cancel / monitoring Undo acceptance afterward.

## Canonical monitoring feedback and bounded repeated takes (2026-10-07 UTC)

Previous goal turn made production progress through merged PR18. This checkpoint
fixes the [actual monitoring widget mismatch](74-recording-monitor-feedback.md)
and completes one original full repeated native recording workflow.

A real Qt dropdown change after Prepare but before GUI refresh was rejected by
the accepted-prefix barrier while remaining displayed. The original failing UI
source/build/logs were preserved before the production correction. Rejected input
now restores canonical mode immediately; polling checks actual widget data too.
Off→Post-EQ, Post-EQ→Auto and Auto→Off regressions preserve model revision, clean
state and prepared mode without activation or job creation. Subsequent accepted
edit, Undo/Redo and Save pass. Local Debug38/38 pass in44.81s, including475 actual
Qt checks and all existing controller/preparation regressions.

One opt-in native repeated-stage run passes32 armed tracks, three replenished
windows, mixed monitor modes and declared input delays. All1,557,696 raw samples
and960,000 nonflat stereo samples match exactly; original media, Save/reopen and
grouped Undo/Redo pass. All1,875 owner bridge calls have joined stage coverage,
with original RT /80% deadline /current-cycle /owned-route gates unchanged. The
owner's1,159,207ns wall maximum joins its same-clock1,124,448ns bridge observation;
inclusive mix/EQ dominates this measured interval. The existing production
libraries are hash-identical throughout this diagnostic experiment. Its test-only
measurement cost is included, not removed.

Original48's timing failure is not reproduced or resolved by this later pass;
its CPU-stage cause remains unknown and sustained performance is unqualified.
Runtime failures remain48. The original337-payload archive verifies relocation,
all96 persisted raw asset/oracle hashes and22 altered evidence refusals without
audio replay. A reader-development field-name error is separately retained before
correction; no original generated data is changed. The24-input equalizer audit is
identical before/after and all92 frozen projections remain unpromoted at
`8f82e44e0eac1facada5474c5a864a64cdd63947595cd73ef738c1c928c50d28`.

Next actual feature: serialized Qt manual-recording controller and Play / Punch
In-Out / next take / Stop-Cancel / retained group adoption, including late Cancel
through finalization and canonical intent/Undo tests. Native Windows, physical,
sustained, X004/X005/X006 and Europe remain open; the current256-track ceiling is
unchanged. Continue the full goal without waiting for repeated passing short runs
to substitute for these missing workflows.

## 2026-10-07: manual desktop worker and late cancellation

Previous goal turn: progress. PR19's merged tree/source backup was verified by
fresh bundle restore and byte comparison; track scalability remains planned.
This turn adds the production serialized manual worker and closes the core late
Cancel gap at application handoff.

One worker owns native endpoint construction, explicit activation/routes, prepared
take slots, punch submission, disk service and native/reader/disk joins. A16-command
FIFO reserves64 reliable control completions. Audio replies retain64 application
credits; eight aggregate engine/application results enforce backpressure. Stop/
Cancel uses the shared generation signal independent of queued commands. Stale
intent cannot replace the active graph; explicit result consumption does not edit
canonical history or Save. Closed results remain immutable and consumable.

Cancel now reaches still-owned core results during writer/verifier finalization
and after Stop. takeGroup's cancellation acquire observation commits handoff of
its next receipt; previously transferred receipts remain unchanged. Positive
unserviced prefixes drain into retained media. Canceled groups refuse implicit/
partial adoption, with original failures/checkpoints/media retained.

Actual functional tests cover8 repeated two-lane takes on one prepared nonflat
graph, exact raw/reliable replies, grouped history/Save/reopen,16 queued commands
during held construction,64 control/audio receipts,8 aggregate result slots,
priority callback stop independent of a held control worker, late Cancel and joined
Close. Core tests additionally cover held real writer hash/join and actual verifier
with32 independently captured lanes. No callback allocation/free/blocking lock
was observed. The unchanged-core synthetic regression original/source/executable
hashes/media/logs were retained before diagnosis; it is not a new native failure.

Debug39/39,45.07s; affected ASan/UBSan/LSan3/3,6.38s with leak detection; Windows
media/controller/test compilation passes. No actual-widget/native-controller/
Windows-runtime/sustained qualification follows.24 reviewed equalizer inputs are
unchanged. All 92 frozen contract projections remain unchanged/unpromoted.48 runtime
observations, original48 CPU cause, X004/X005/X006, Europe and platform/installers
remain open. See docs/75-manual-desktop-worker.md, ADR060 and the dated receipt.

Next implementation: wire actual Qt manual transport/take widgets, canonical
prefix barrier, parameter following, explicit grouped adoption/recovery and Close.
Keep the full frozen goal active.

PR20 review additionally found acknowledgements queued during the last held Close
could be accepted but stranded after worker exit. The new unchanged-source
regression records1 control/2 audio/1 group still present; source/executable hashes
and original media/logs were frozen before correction. Final publication drains
that prefix under its publication mutex; the same regression records0/0/0 with
older snapshots unchanged. Final Debug39/39,46.29s and focused controller
ASan/UBSan/LSan1/1,1.03s pass; Windows controller compilation passes. Earlier
core sanitizer/evidence archives remain unchanged. Runtime/native count remains48.

## 2026-10-07: actual manual recording desktop workflow

Previous continuation: progress by observing the exact pending build's terminal
failure and current implementation/planned track limits. The frozen full goal and
X006 resource-admitted track target remain intact; no fixed product cap is adopted.

The dedicated Qt panel now connects canonical-prefix preparation, explicit persistent
input/master routes, Play, prepared next takes, Punch In/Out, priority Stop/Cancel,
retained group previews and explicit adoption/Keep. Fixed-range/locator and manual
workflows occupy separate recording tabs. Close drains native/reader/disk ownership,
requires an explicit preview decision, then runs the normal project save prompt.
The worker remains usable when Review/Cancel aborts Close.

Canonical EQ revisions follow the existing graph through bounded event queues.
Partial acceptance, audio application and coalesced desired models remain distinct;
Undo/Redo and selected-track inspector changes preserve canonical preparation.
Structural inventory/layout/matrix/arm changes refuse safely. Adopted clips affect
canonical history and are heard after reprepare; the live original timeline is retained.

Actual-widget synthetic acceptance covers three two-lane takes on one graph, exact
raw capture, float headroom, persisted routing and Ready hotplug, group adoption/
Undo/Redo/Save/reopen, failed file verification/retry, priority Cancel during held
finalization and Close Review/Keep choices. Dependent controls now disable at
admission, including attachment Retry, before a timer can lag behind intent. Four
development regressions and the original compile failure retain source/log/project
evidence and executable hashes; the fourth runtime failure also retains local
executable bytes. Earlier executable bytes are unavailable and not claimed.
These are synthetic development failures, not new native timing observations.

Debug40/40,47.83s; affected ASan/UBSan/LSan3/3,15.29s with leak detection; Windows
media/controller/test cross-compilation passes. The900×700 offscreen manual panel
was visually inspected.24 reviewed equalizer inputs and all92 frozen projections
are unchanged; zero F/Q/C/N promotions. Native runtime count remains48 and original48
CPU cause is unresolved. See docs/76, ADR061 and the dated M2 receipt/archive.

Next concrete task: exercise this actual StudioWindow/controller workflow on owned
PipeWire source/sink routes, preserving the user's hardware/default playback. Cover
repeated takes, parameter audio application, Stop/Cancel, input/output loss, failed
adoption/recovery and Close with exact timing/raw/output evidence. Independently
implement/qualify Windows audio and equivalent desktop workflows. Continue full
frozen parity, X004/X005/X006, all-Europe localization and installers; none is complete.

PR21 review identified an unrelated rejected Save could clear pending attachment
state while its verification IO continued. An isolated replay of the reviewed panel
reproduces the failure; its only source adaptation renames the private uint64 member
to match the current header, with current supporting project controller. Source,
adaptation, executable, generated project and log are retained; this is not claimed
as an unmodified original binary or a native timing experiment. Attachments now use
caller IDs and separately retained IO-completion/command-rejection receipts. Manual
and fixed-range adoption use them. The held-IO actual-widget regression passes.

Final Debug40/40,48.08s and affected ASan/UBSan/LSan4/4,16.19s pass. Earlier source
evidence remains retained. An intermediate sanitizer run of the older fixed-recording
widget workflow timed out waiting for512 captured frames; original source/log/executable
are retained, but its auto-cleaned project is unavailable. Cause remains unresolved;
passing later tests do not resolve it. Future timeouts now log worker state and retain
their project. No native observation/parity counts are changed.

## M2 native manual desktop and neutral buffers (2026-10-07)

Previous goal continuation: **progress** (PR21 merged and source backups verified).
This checkpoint adds actual owned-native Qt recording acceptance and corrects an
observed valid native silence buffer being rejected. Exact original62 metadata
and an isolated old-source regression establish that mechanism; earlier missing
metadata remains missing. Five final short release workflows pass on3/32 inputs.

Debug40/40,48.20s and affected ASan/UBSan/LSan3/3,4.31s pass. An extra actual native
sanitizer run fails on a512-frame active clock skip after30,464 frames; original
source/executable/media/logs/clocks are retained, no memory diagnostic is reported,
and no passing native-sanitizer or causal claim follows. Its original routing
snapshots are unavailable. All71 native observations and prior missing artifacts
remain visible. Independent archive replay verifies original53's entire exact
raw/output prefixes while retaining its original failure status.

The frozen92-contract projection, equalizer inputs and full target are unchanged.
Next: X006 coordinated track-resource admission and media/UI scaling, native Windows
recording acceptance, and separate diagnosis/sustained qualification. The current
256-track ceiling and all F/Q/C/N, X004/X005/X006/Europe/full-product gaps remain.
See [contract and limits](77-native-manual-panel.md) and the dated M2 receipt.


## M2 X006 resource-admitted large projects (2026-10-07)

Previous goal turn was **progress**: protected PR22 qualified actual short native
manual desktop workflows and retained original sanitizer observation 71's active
clock gap/source CPU cause unresolved. This turn starts concrete X006 model/parser/
mix and desktop scaling rather than promoting a track count to full acceptance.

[Implementation](78-resource-admitted-projects.md)/ADR063 removes fixed total-track
validation/parser/mix/replacement-mask and Add Track ceilings. Trusted state,
encoded-byte, parser-staging and DSP payload policies cannot be overridden by a
project file. Checked charges, indexed immutable preparation, efficient history
differences/order and schema 1.7 retain IDs, explicit refusal and migrations.
Charges do not establish allocator/RSS or combined old/new graph/history bounds.

Core audio-track workflows pass at 257/512/1024/4096: grouped edit, Undo/Redo, Save/
reopen/previous snapshot, low-budget transactional refusal, exact matrix/headroom
over two synthetic 16-frame blocks, high-ordinal replacement/event and zero RT
allocations/frees/blocking locks. Actual Linux Qt controls open 4096, edit the last
stable ID, add 4097, Undo/Redo and Close/Save/reopen. Full Debug **42/42, 82.25 s**
passes. Seven affected sanitizer groups pass on unchanged production sources;
the separate normal/large desktop cases then pass **2/2, 58.25 s**. Large Close/
Save measures 3,058 ms Debug and 16,536 ms under sanitizers. The 4,096-track
synthetic graph requires about 1.64 GB of configured prepared payload, highlighting
per-track event queues. No sustained or physical throughput is inferred.

Original slab16/slab64 fixture errors, stale-test launch after a failed build,
legacy minor/track-limit assertions, logger compile error, initial GUI compound
assertion and sanitizer ten-second Close timeout remain retained. Missing original
GUI subterms/unsaved state and exact original Close-worker timing are not restored
retroactively by later instrumentation. The large GUI case now has a separate
bounded test and scoped wait; ordinary waits/deadlines remain. Actual predecessor
reader refusal passes on a 257-track schema 1.7 project with snapshots unchanged;
its 4096-track attempt stopped earlier at the old 4 MiB bound and is kept separate.

Windows core cross-build passes compilation/linking only. All 24 reviewed equalizer
inputs remain unchanged. The 92-row frozen projection hash remains
`8f82e44e0eac1facada5474c5a864a64cdd63947595cd73ef738c1c928c50d28`, with zero F/Q/C/N
promotion. All 71 existing native observations and historical unresolved causes
remain; this turn adds no native audio run. The [dated receipt](../tests/results/M2/2026-10-07-resource-admitted-projects.json)
and archive preserve exact sources, executable hashes, generated no-media projects,
logs and original failures.

Next implement shared bounded media handles/cache/read-ahead and virtualized views/
meters, combined snapshot/history/old-new graph admission and configurable desktop
policies, then large recording/adoption workflows. Recording 256-arm/channel
assumptions, structural 64-operation batch and 256-command/32 MiB history policy,
freeze/bounce, richer/parallel graphs and sustained Linux/Windows profiles remain.
The full frozen suite, X004 imports, X005 profiles, native Windows/UI/install and
European language delivery remain required. X006 is partially implemented, and
the full goal remains active and incomplete.


### Configured admission review correction (2026-10-07)

Automated review of the first X006 published head found two controller preflight
calls still using default state admission. Both now receive `admission.state`
before committing a gesture. A dedicated 32 KiB refusal fixture reproduces the
original defect for routing and structural changes: later history refusal left
an unrelated gain gesture committed, so Cancel could not restore it. Original
source/executable hashes, saved projects and both red observations remain. The
fixed case preserves canonical/history/gesture state and saved projects. It is
a policy-propagation test, not an above-64 MiB project stress measurement.

Fresh full Debug43/43,84.35s and affected controller ASan/UBSan/LSan2/2,0.90s pass.
The earlier nine affected sanitizer groups retain their earlier source scope; no
new all-nine sanitizer run is claimed. Branch protection refused an initial merge
while the valid review conversation was unresolved; no admin bypass was used.
[Review receipt](../tests/results/M2/2026-10-07-resource-admission-review.json) pins
the corrected source, original red fixture and qualification. All24 equalizer
inputs remain unchanged, all92 frozen contracts remain unpromoted and native
observations stay71. The full goal and remaining X006 stages remain incomplete.


## M2 X006 shared media pool/cache (2026-10-07)

[Shared media checkpoint](79-shared-media-cache.md)/ADR064 adds one resource-admitted
asset registry, handle pool and decoded source-frame cache per serialized read owner.
Live/offline readers share exact clip coordinates; audio consumes existing bounded
slabs. Immutable prepared asset indices avoid per-lane whole-session admission.

File-backed257/1,024-track mixes share one asset/handle and decode three pages;
96 distinct assets stay within two handles. Independent matrix/source oracles
compare every output sample exactly, with Linux descriptor bounds, page/reopen/
cancellation/refusal and per-occurrence nonfinite tests. Original old-reference
assertion source/executable hashes/log remain; old fixture cleanup removed original
project/media, and later fixtures do not fill that evidence gap.

Full Debug44/44,86.94s and affected media/playback/
export/recording ASan/UBSan/LSan7/7,74.44s pass.
Fresh Windows headless media compile passes; no Windows runtime/UI/native or
sustained capacity follows. All24 reviewed equalizer inputs are unchanged and all92
frozen F/Q/C/N contracts are unchanged/unpromoted. Native observations remain71;
original71's clock gap/source CPU cause and historical failures stay unresolved.
[Receipt](../tests/results/M2/2026-10-07-shared-media-cache.json) retains exact sources,
logs, generated media/projects and qualification. Next virtualize track/timeline/
meter views, measure combined resource envelopes and expose desktop resource
controls; large recording/adoption, freeze, scheduling, sustained/platform profiles,
X004/X005/Europe and every remaining full-suite milestone remain required.


### Export registry policy review correction (2026-10-07)

Review found that export could raise overall memory but could not configure its
default16MiB media registry. ExportSettings.mediaCache now reaches the shared
reader for both track and mix exports, charged inside aggregate memory. A real
API fixture requests1 registry byte: original adapter wrongly publishes both
files; fixed adapter returns ResourceLimit and leaves destinations absent. Raising
the configured registry to32MiB yields exact output against the independent EQ
reference and unchanged Save/reopen state. This verifies propagation/refusal, not
an above-default asset-inventory stress workload. Original source/executable
hashes/log/project/media and wrongly published outputs are retained.

Fresh full Debug45/45,127.81s and affected export
ASan/UBSan/LSan2/2,5.11s pass. The initial
seven sanitizer groups retain their initial checkpoint scope. Fresh Windows media/
export compile/link passes, with no Windows runtime/UI/native claim. No native
run, dependency, schema change, equalizer change or92-contract promotion.
[Review receipt](../tests/results/M2/2026-10-07-shared-media-export-policy.json)
records the correction; full X006 and the frozen goal remain incomplete.


## M2 X006 model-backed lists and viewport timeline (2026-10-07)

[Viewport/list checkpoint](80-virtualized-session-views.md)/ADR065 adds stable-ID
models for track/arm/destination/media/clip selectors and viewport interval queries.
Actual offscreen Linux Qt8192-track navigation/edit/Undo/Redo/Save/reopen retains
raw/canonical state. The recorded viewport paints2rows/2clips; a10000-clip
inventory paints304matches after631interval-node visits.20vertical positions and
passive polling reuse the snapshot index. These are workloads, not capacities.

Final full Debug46/46,109.03s and affected
ASan/UBSan/LSan6/6,202.54s pass. Original paint
storage assertion, one intermittent desktop recording-start timeout and the
split-fixture publication/viewport race remain retained with exact source and
executable hashes, logs and generated project/media. The pixel check now compares
exact RGBA; the split fixture awaits the new clip rectangle. A deterministic regression reproduces Record enabled with missing required routes;
the GUI now reconciles route widgets before readiness and gates button/action/Start
on every required input/monitor selection. Missing-input/output negative checks
and actual capture/adoption pass. Exact route state at the two original timed-out
clicks was not captured, so their precise cause remains unproven. The initial large sanitizer case exceeds the generic10-second Open wait; its
originals remain retained. A measured rerun uses an explicit60-second large-load
allowance; this does not qualify interactive load responsiveness. Original71 clock/source CPU cause
also remains open; no native run is added.

No engine/schema/dependency or equalizer change; all24 reviewed inputs and92
frozen F/Q/C/N projections are unchanged/unpromoted. GUI indices/rebuilds still
scale with inventory, Qt signed-int rows need paging, and aggregate resource
admission, meters/waveforms, accessibility, native Windows GUI and sustained
profiles remain. Next measure/admit combined GUI/history/state/old-new graph
resources and expose trusted desktop policies, then large recording/adoption,
freeze/bounce/scheduling and all remaining frozen/X004/X005/Europe/Windows work.
[Receipt](../tests/results/M2/2026-10-07-virtualized-session-views.json) retains scope.


## M2 X006 configurable Undo resources (2026-10-07)

[History admission](81-history-resource-admission.md)/ADR066 replaces fixed
Undo retention with trusted configurable command/payload/workspace policies.
Checked charges include retained/active data and declared canonical/candidate
work. The control worker preflights before committing unrelated gestures.
Rejected reductions preserve both stacks; new edits report oldest retirement.
The desktop dialog reports usage and correlated acceptance/refusal, persists
accepted preferences, and distinguishes preference-write failure from runtime
policy acceptance. Project schema1.7 and existing defaults remain compatible.

Final full Linux Debug48/48,134.77s; affected ASan/UBSan/LSan11/11,276.39s;
Windows core/media cross-build passes without a GUI/native runtime claim.
Core512-track/400-group full Undo/Redo/Save-reopen and actual Linux Qt512-track/
300-action Undo/Redo/settings/refusal/retry/save/Open workflows preserve stable
identities. German grouping input is qualified; translations and all-Europe
review remain open. The8192-track viewport regression still passes.

An original unresolved dialog-library link failure is retained with its source
and log; both omitted fixture links were corrected and the full build passes.
The initial peak-reporting regression fails0/1: active gestures did not publish
admitted workspace. Exact source/executable hashes, log and owned project remain;
successful Begin/update now publish their checked peak, and final tests pass.
An intermediate full build was deliberately interrupted for preference callback
review; no preference-failure test ran before the handler correction. The final
actual settings failure/retry fixture passes.

[Receipt](../tests/results/M2/2026-10-07-history-resource-admission.json) and its
verified22,622,839-byte/1757-entry archive retain originals and final evidence.
All24 equalizer input snapshots and92 frozen F/Q/C/N projections are unchanged;
no parity promotion, dependency/schema change or native audio run. Observations
remain71; original71 clock/source CPU cause remains unresolved.

These are declared owned/work charges, not pre-admission of every allocation,
allocator/RSS bounds or complete aggregate process admission. Next implement a
combined policy for controller retained snapshots/saved/IO models, inspector/GUI
indices and old/new graphs, then scale recording arms/adoption independently of
hardware channels. Full X006, all frozen milestones, Windows, X004/X005 and Europe
qualification remain required. The goal stays active and incomplete.


## M2 X006 history review corrections (2026-10-07)

PR26 review identified two valid defects. With the initial worker publication
held, the dialog read default limits rather than configured options; applying
could overwrite unedited custom bytes. With an active gesture and oldest-command
retirement, a successful preflight admitted40,000bytes but the later counter
published37,409bytes. Both deterministic original cases fail0/2 and retain exact
source/executable hashes, logs and owned roots.

Initialize the first immutable controller snapshot synchronously from trusted
options. Return read-only preflight charges and record them only after accepted
parameter/route/monitoring/structural/attachment work succeeds. This preserves
pre-eviction work without changing counters on refusal. Startup with the worker
paused preserves configured and non-MiB-aligned byte limits; the accepted peak is
now40,000bytes. Existing defaults, stable IDs, saved state and all frozen scope
remain unchanged.

Final full Linux Debug50/50,131.85s and affected ASan/UBSan/LSan13/13,276.76s pass;
Windows core/media cross-build passes without GUI/native runtime qualification.
The512-track/400-core-group and300-GUI-action workflows and8192-track viewport
regression remain passing. The final [review receipt](../tests/results/M2/2026-10-07-history-resource-review.json)
supersedes the first checkpoint's compiled-source qualification; its verified
10,504,496-byte/704-entry archive preserves both review failures and final data.
The original checkpoint receipt/archive remain historical evidence.

All24 reviewed equalizer inputs and92 unpromoted frozen contracts remain unchanged;
no native audio run, dependency/schema change or resolution of original71 clock/
source CPU cause. Full aggregate snapshots/GUI/IO/old-new graphs and allocator/RSS
admission, recording scaling, sustained/native Linux/Windows, frozen parity,
X004/X005 and Europe qualification remain required. Next implement combined
snapshot/GUI/graph resource admission. The goal stays active and incomplete.

## X006 retained immutable Session ownership (2026-10-07)

[ADR067](decisions/067-retained-session-resources.md) and
[ownership/admission scope](82-retained-session-resources.md) add shared accounting
and move-only leases for each unique immutable controller Session block, retained
until the last reader releases it. Saved revisions, in-flight saves and barriers
borrow current state. Edit/Undo/Redo publications are admitted before gesture
commit/canonical mutation; full-budget Cancel borrows the starting publication.
Unknown-size Open reserves trusted maximum state bytes before worker decoding.
Snapshot policies have correlated runtime commands; the real Undo resources dialog
shows locale-formatted live reservations and configured byte-exact startup limits.

The [receipt](../tests/results/M2/2026-10-07-retained-session-resources.json) preserves
the first successful focused scope plus final sources, executable hashes, logs and
owned generated project/media bytes. Full Linux Debug52/52,139.51s
and affected ASan/UBSan/LSan15/15,314.38s pass.
Windows core/media cross-build passes; no GUI/native Windows runtime qualification.
512-track refusal/release/retry, active Cancel, Undo/Redo, Save/reopen, paused Save
and shared barriers pass; the8192-track viewport regression remains passing.
The verified archive is11,320,389bytes/736entries.

All24 reviewed equalizer inputs and92 unpromoted frozen contracts remain unchanged.
No live native audio/VM run, new third-party dependency, schema change or resolution
of original71 clock/source CPU cause. This snapshot ledger does not cover canonical
state/history, GUI indices/projections, audio graph overlap, parser/IO buffers or
Qt/allocator/RSS. Next share leases across those owners and expose combined trusted
resource settings; continue large capture admission, freeze/bounce, prepared
scheduling and sustained Linux/Windows qualification. All frozen parity, X004/X005
and Europe gates remain required. The goal stays active and incomplete.

### PR27 attachment review correction

The [final review receipt](../tests/results/M2/2026-10-07-retained-session-review.json)
retains a deterministic full-budget512-track attachment failure: completion tried
a third publication (3,342,324bytes required,2,227,903available) while the verified
proposal remained owned. Unchanged model revision now reuses that proposal and its
generated clip IDs; changed revisions still merge/admit current edits. Original
source/executable hash/log/project/take bytes remain in the supplementary archive.

Final Linux53/53,142.86s and targeted attachment/controller/manual UI
ASan/UBSan/LSan5/5,29.64s pass. Windows core/media cross-build
passes without GUI/native runtime qualification. This final compiled-source receipt
supersedes the initial52/15 scope; that earlier evidence remains historical.
The supplementary CRC/all-entry-byte verified archive is
8,496,931bytes/685entries. All 92 contracts,
24 reviewed reuse inputs, original71 unresolved cause and full X006/frozen scope
remain unchanged. Next coordinate canonical/history/GUI/graph/IO resource leases.

## X006 shared controller memory checkpoint (2026-10-07)

Previous goal turn was **progress**: PR27 is merged as `df80c96a5418`, with final
compiled-source qualification, retained original review failure and verified
source/bundle restore. This turn adds [ADR068](decisions/068-controller-memory-resources.md)
and [shared controller ownership/policies](83-controller-memory-resources.md).
Canonical state, history/active gestures, unique snapshots and declared edit work
share a parent; persistent growth/shrink transfers existing credits without a new
reservation after mutation. The actual scrollable desktop dialog exposes atomic
parent/snapshot policies, live usage and preference persistence/failure/retry.

The [receipt](../tests/results/M2/2026-10-07-controller-memory-resources.json) records
Linux Debug **55/55, 145.39s**, affected ASan/UBSan/LSan **18/18, 334.35s**, and
Windows core/media cross-build. New 512-track root/UI refusal, full-budget Cancel,
513-track growth/Undo/Redo, retained-reader survival and desktop retry pass.
Existing 8192-track/10000 sparse-clip UI regression passes. Original focused
fixture oracle/sequencing failures and their sources/executable hashes/logs/owned
projects remain preserved; final inputs match between Debug and sanitizers.
The CRC/all-entry-byte verified archive is **13,830,965 bytes / 978 entries**.

All 24 reviewed reuse inputs and 92 unpromoted frozen-reference contracts remain
unchanged. No native audio/VM run, new dependency or schema change. Original 71
clock/source CPU cause and earlier unresolved causes remain open. These declared
charges do not bound exact allocations/RSS, expanding trials/command payloads,
GUI projections/indices, graph old/new/tail, caches or parser/IO overlap.
Next lease actual GUI projections/indices and expose retryable GUI admission;
then coordinate graphs/cache/IO, measure allocations/RSS and sustained platforms,
and continue larger capture/adoption, freeze/bounce and scheduling. The 256
recording-input implementation cap, native Windows, X004/X005 and Europe gates
remain required. The full goal remains **active and incomplete**.

## X006 shared GUI payload checkpoint (2026-10-07)

Previous goal turn was **progress**: PR28 merged as `b2f35b06313e`, with final
controller-memory qualification and verified source/bundle restoration. This turn
adds [ADR069](decisions/069-gui-memory-resources.md) and
[GUI ownership/refusal workflows](84-gui-memory-resources.md). Selected-track
projections, list/decorations and timeline interval/query arrays lease credit from
the controller parent. Old/new displays are staged together; refusal retains a
complete previous view, pauses stale editing and offers resource settings/retry.
Matching inventories reuse admitted indices, and timeline candidate scratch is
prepared before paint/hit queries. Save remains available for committed state.

The [receipt](../tests/results/M2/2026-10-07-gui-memory-resources.json) records exact
final compiled inputs, executed Debug/sanitizer scopes, Windows core/media
cross-build, original compile/UI failures and owned generated project/media bytes.
New 512/513-track display, selection, full-budget Save/refusal/retry and last-owner
release workflows pass; existing 8192-track sparse and 10000-clip viewport gates
remain required. Timer-stopped initial monitoring/arming fixtures preserve their
original product failures and corrections. An earlier Close timeout was a fixture
timer sequencing error, preserved separately. No hardware route is activated.

The initial 24-input equalizer audit passed. Before publication, four review-only
DSP references changed as both equalizers lowered their profile-wrapper post-gain
minimum to −60 dB. New committed snapshots and exact whole-file delta proof are
retained in `reuse/reviews/2026-10-07/`; the borrowed equations and other 20 inputs
are unchanged. Refreshed input/integrity/isolated CLI checks pass. All 182 compiled
inputs remain identical between final Debug/sanitizer/current sources. No equalizer
source was modified by this work. Final Debug **57/57, 145.16s** and affected
ASan/UBSan/LSan **22/22, 341.79s** pass; Windows core/media cross-build passes.
All 92 frozen F/Q/C/N contracts remain unchanged and unpromoted. No native audio/VM
run, schema change or dependency addition. Original native observation 71 clock/
source CPU cause and earlier unresolved causes remain open. Declared GUI weights
do not cover all Qt/control/caller temporaries or exact heap/RSS; graph/cache/IO,
commands and expanding trials still need shared admission. Next admit prepared
graph/media-cache overlap and safe retirement, then IO and measured allocations/
RSS, paging, larger capture/adoption, freeze/bounce, scheduling and sustained
Linux/Windows workloads. The 256 recording-input implementation, native Windows,
X004/X005 and Europe gates remain required. Full goal and X006 stay incomplete.

## X006 shared prepared execution checkpoint (2026-10-07)

Previous goal turn was **progress**: PR29 merged as `dba9815c1c8f`, with exact
compiled-source GUI qualification and verified source/bundle restoration. This
turn adds [ADR070](decisions/070-shared-execution-memory.md) and
[execution ownership/admission scope](85-graph-memory-resources.md). Prepared
DSP/playback pools, reader bindings/decode buffers, shared cache registry/pages
and WAV output buffers share the controller parent. Nested owners are charged
once; old/new graphs retain credit until control retirement after the last audio
borrow. Desktop playback, fixed/manual recording and export receive that parent.

The [receipt](../tests/results/M2/2026-10-07-graph-memory-resources.json) records
Linux Debug **58/58, 147.09s**, affected ASan/UBSan/LSan **29/29, 214.63s**, and
Windows core/media cross-build. All **183 compiled input hashes** match across
final Debug/sanitizer/current sources. New 512-track overlap/refusal/retirement,
reader/cache rollback/retry, shared cache last-owner release, exact live/offline
128.0 float headroom and concurrent export ownership pass. Actual desktop full
parent Prepare refusal, policy raise/retry and Stop release pass without activation.
Existing 8192-track/sparse UI, event/timing/quality and persistence gates pass.
The CRC/every-entry-byte verified archive is **17,356,908 bytes / 1,797 entries**.

Original preparation-member compile and fixture Json compile failures are retained,
as are small fixture local-budget/missing-media, missing export directory/undrained
capture, and early disabled Stop fixture sequencing failures. Exact sources,
executable hashes/private copies, logs, GDB diagnosis and owned generated files
were preserved before corrections. These failures do not establish native causes.

All 24 reviewed equalizer input hashes and 92 unpromoted frozen F/Q/C/N contracts
remain unchanged. No native audio/VM run, dependency or schema change, source write
to equalizers or resolution of original observation71 clock/source CPU cause.
These conservative weights do not bound all allocations/RSS, CPU throughput,
capture/writer/other IO, parser/command/expanding trials or Qt overhead. Local
128 MiB playback/export allowances remain independently configurable through core
APIs; coordinated desktop graph/IO policies are still needed. Continue that policy
work and capture/IO admission, measured allocations/RSS, paging, larger recording/
adoption, freeze/bounce, prepared scheduling and sustained Linux/Windows profiles.
The 256 recording-input implementation and all native Windows, X004/X005, Europe
and frozen product gates remain required. The full goal and X006 remain incomplete.

## X006 coordinated execution policy checkpoint (2026-10-07)

Previous goal turn was **progress**: PR30 merged as `12b49d846667` with a
verified restored source backup. This turn adds [ADR071](decisions/071-coordinated-execution-policy.md)
and [86](86-execution-memory-policy.md). New desktop playback/fixed/manual/export
preparation samples trusted parent policy; recording envelopes use immutable
prepared usage. Raw pools, bridge bindings, aggregate manual banks, declared
writer/hash/journal workspace and monitoring-off scratch now share that parent.
Dynamic leased acknowledgement arrays remove the remaining 256-lane assumption
in desktop playback parameter receipts. No callback ledger work is added.

The [receipt](../tests/results/M2/2026-10-07-execution-memory-policy.json) records
full Linux Debug **58/58, 161.05s**. Initial
affected sanitizers passed **32/33**; the new 512-lane bulk control fixture hit its
old five-second wait. Exact-executable GDB captured bulk model preparation, zero
submissions and no engine error. Its test-only large-workload allowance is now
30 seconds; smaller workflows keep five. The changed fixture passes current
Debug **1/1** and ASan/UBSan/LSan **1/1, 6.76s**,
completing 33 qualified sanitizer workflows across those explicit scopes. The
other 182 compiled inputs are identical to the original full scopes; all current
183 hashes match the two targeted retry snapshots. This is not a real-time
deadline/performance qualification. Windows core/media cross-building passes.

Actual Qt 512-track file/EQ Play verifies exact 128.0 float samples/peak, full-parent
refusal/raise/retry, explicit output-intent editing, exact execution release on
Stop, Save/reopen and zero parent credit after Close. New capture/writer/bank/bridge
refusal, retry, cancellation, overlapping ownership and retirement tests pass,
including the existing callback allocation/free/blocking-lock audit. The verified
archive is **26,766,142 bytes / 2,603 entries**, with CRC and every
entry's bytes checked. Original compile, allowance-versus-usage fixture errors,
early null route selection, terminal zero-meter expectation, pre-routing baseline
assumption and sanitizer deadline failure remain retained with exact sources,
executable hashes/private copies, logs and GDB diagnosis/reproductions.

The owner is adding equalizer localization in other chats. The audit flags both
equipment editor inputs, and exact observed source/diffs are retained for impact
review. The DAW already uses contextual Qt editor translation; no equalizer source
tree is written or unreviewed runtime/catalog integration claimed. Reviewed
snapshot integrity and the 92 unpromoted frozen contracts remain intact.

No native audio/VM run, new dependency/schema or resolution of original native71
clock/source CPU cause. Copied owner Sessions, parser/recovery/other IO, transient
work, allocator/RSS/CPU, bulk parameter preparation/cancellation responsiveness,
paging, 256-arm/packed-input adaptation, freeze/bounce, scheduling and sustained
Linux/Windows profiles remain open. Continue these tasks and review stable
equalizer localization runtime/catalog/installer changes. Full X006, frozen parity,
X004/X005, native Windows and European language qualification remain required.
The full goal stays active and incomplete.


## X006 execution policy review corrections (2026-10-07)

PR31 review found two valid accounting errors: duplex bindings were charged twice
in the local envelope, and early standalone preparation omitted reader/cache and
bridge declarations. Shared off-RT queries now make preflight and construction
agree; raw capture pools count once. Exact declared occupancy admits recording,
a budget one byte below the complete total refuses before media hashing/job
creation, and an exact inactive retry preserves the saved project. Actual prepared
lane format validation still precedes media hashing if a later caller Session has
changed. Trial metadata and copied owner Sessions remain outside this gate.

The separate [review receipt](../tests/results/M2/2026-10-07-execution-memory-review.json)
retains the original preview source and static review findings. No pre-fix runtime
reproduction is claimed for those findings. Current full Linux Debug passes
**58/58, 150.03s**, and affected ASan/UBSan/LSan passes **33/33, 249.27s**;
all **183** compiled input hashes match current sources across both final scopes.
The original preview receipt/archive and its earlier failures remain immutable.
The separate review archive is **21,712,785 bytes / 1,580 entries**, with CRC
and every entry byte verified.
The Windows core/media cross-build passes; this is separate from native
runtime/GUI qualification. The latest equalizer editor snapshots/diffs, including
the numerical chart-direction delta, remain retained for a distinct localization
UI adoption gate; neither source checkout was written.

No new native audio/VM run, dependency/schema, frozen parity promotion or resolution
of original native71. Continue copied state/IO/transient admission and measured
allocator/RSS, larger recording arms, freeze/bounce, scheduling and sustained
Linux/Windows workflows. Native Windows, X004/X005 and Europe qualification remain
required. Full X006 and the DAW goal remain incomplete.


## X002 contextual desktop and preview delivery checkpoint (2026-10-07)

The [localization foundation](87-desktop-localization.md) now provides an actual
Settings language/format dialog, embedded contextual catalogs, explicit fallback
and expanded/RTL test locales. English and 32 nonempty drafts are available;
each draft covers 8/525 messages, with zero native-reviewed/fully UI-qualified
languages. The Europe register remains 143 planned work items. Numerical timelines
and equipment charts retain their direction, and GUI language/format preferences
do not alter project IDs/state or rendered audio samples.

Current full Linux Debug passes **61/61, 157.49s**; affected ASan/UBSan/LSan passes
**6/6, 31.32s**. All 379 captured source/resource/test inputs match those scopes.
Ten isolated catalog corruption/refusal cases pass. Original fixture compile,
wrong-context-value and dirty-prompt timeout failures remain retained. The
content-addressed evidence capsule is **83,382,228 bytes / 647 physical entries**,
with 8,832 logical input names, CRC and every selected byte verified. Exact hashes
are in the [receipt](../tests/results/X002/2026-10-07-desktop-localization.json).
The read-only reuse audit now matches all 44 inputs at the stable public/premium
localization revisions; the equalizer checkouts remain unchanged.

No native audio or VM execution, new dependency, project schema or F/Q/C/N promotion.
All 92 frozen contracts and original native71 clock/source CPU questions remain open.
Native Windows, language completion/review/UI/help/installers and full X004/X005/
X006 remain required. The owner prioritizes useful installable previews and easy
setup: a bounded Ubuntu26.04 amd64 DEB builder checks clean tested inputs, derives
runtime dependencies and pairs exact source. Fresh-machine installation/audio/
upgrade/remove and Windows runtime qualification remain the next delivery gates.
The DAW goal remains active and incomplete.

## Useful Linux preview packaging/runtime checkpoint (2026-10-08 UTC)

Previous goal turn was progress: contextual desktop localization and the first
local DEB/source preparation, with all 92 frozen F/Q/C/N rows still unpromoted.
This continuation corrects a valid packaging review: canonical build-source binding
and exact installed executable/icon/desktop/notices/provenance verification.
The owned upgrade attempt also found that random Git hashes break same-day Debian
version ordering. Frozen increasing UTC preview sequences now order versions;
updates check the previous package version without a downgrade override.

[Runtime evidence](../tests/results/X007/2026-10-08-preview-runtime.json) retains
62/62 Linux Debug, changed Python/desktop 3/3, the prior matching compiled
sanitizer inputs, actual CLI refusals and original fixture/setup errors. Hosted
Qt6.4.2 passes 58/58 non-native tests and the required Windows core cross-build
passes; neither is native Windows qualification. A signed Ubuntu Base derived
owned rootfs resolves dependencies without a compiler/Qt SDK, upgrades/removes/
reinstalls the sequenced candidate while preserving project/media bytes, and
runs the installed GUI as UID/GID1000 with CapEff0. Unicode project save/reopen
and 128-frame float WAV export exactly match golden bytes. The candidate's
701-file corresponding source archive is independently byte verified.

This is scoped shared-kernel container/Xvfb evidence, not complete desktop/menu
or installed-app native recording qualification. No host package/audio changes,
VM tests, equalizer writes or binary release upload occurred. Native observation71
clock/source CPU causes remain open. Full Europe, X004/X005/X006 and every frozen
parity family remain incomplete. Next concrete preview gate is installed-app
recording/EQ/playback on owned routes and complete desktop integration; native
Windows and its installer follow. The goal remains active.

## Installed preview audio checkpoint (2026-10-08 UTC)

This continuation is **progress** toward useful previews. The exact installed
Ubuntu candidate now has an owned-source **10.28-second** raw recording through
the actual GUI, explicit routing, verified attachment, live EQ **−12/−6/Undo**
with independently measured output, normal save/quit/reopen and a ten-second
float WAV export. An independent direct-form I oracle differs by at most
**2.8422e−14**; reopened exports are byte-identical and all six owned project/media
files are preserved. See [workflow evidence](../tests/results/X007/2026-10-08-installed-preview-workflow.json)
and the updated [candidate guide](90-preview-guide.md).

The earlier failed short capture and fixture failures are retained. Successful
capture has 2,048 leading silent frames; exact suffix comparison does not qualify
complete source coverage/startup alignment. Rejected-clock diagnostics and
first-valid-input policy remain implementation work. Private read-only-rootfs
Xvfb/PipeWire evidence shares the host kernel and does not qualify physical audio,
real-time deadlines, complete desktop installation or native Windows. Owned
sessions retired normally; host defaults/links and equalizer repositories were
preserved. No compiled production inputs changed or binary release was uploaded.

Next concrete task: bounded first-fault timing/input diagnostics and a tested
recording startup/alignment policy, followed by complete desktop and native
Windows preview installation/workflows. All 92 frozen contracts, original native71
and full X004/X005/X006/Europe requirements remain open. The full goal is active
and incomplete.

## Recording first-fault preview checkpoint (2026-10-08 UTC)

Previous turn was **progress**: installed Ubuntu preview workflow qualification
and protected PR33/source backup. This continuation implements a fixed-size
first-fault receipt independent of lossy meters, exact clock/buffer/rate reasons,
control/callback ownership distinction and contextual GUI explanations. Backend
diagnostics preserve later writer/take-verification errors. Normal retirement
retains the receipt and raw prefix; Stop/completion do not fabricate failures.

Linux Debug passes **62/62, 190.40 s**; extra origin/processor-overflow/writer-priority
assertions pass **2/2, 1.42 s** with unchanged production inputs; affected
ASan/UBSan/LSan passes **5/5, 20.10 s**. The private production-owner reproduction
observed clock 29 cycle 1→2 with position 0→0 instead of 1024, same 48 kHz/duration 1024
and no xrun/discontinuity flags. It preserves 1,024 raw frames and reports the
precise position mismatch after joining. See [the contract and evidence](91-recording-fault-diagnostics.md).

This diagnoses the reproduced audiotestsrc route; old opaque/native71 causes and
2,048 leading silent frames remain open. No host packages/routes, VM or equalizer
working trees were changed. The read-only audit found four changed working inputs
per equalizer; review/adapt them separately before the next reuse-dependent
localization/profile milestone. There are now 539 contextual keys, still only 8
translated per non-English draft and zero reviewed/fully qualified languages.

Next: qualify the next package/source pair, persist failed-job diagnostics and
observe native acquisition validity to choose a tested startup/alignment policy.
Complete desktop integration and native Windows previews follow. All 92 frozen
contracts and full X004/X005/X006/Europe requirements remain incomplete; the goal
stays active.

## Installed fault-diagnostic preview checkpoint (2026-10-08 UTC)

This continuation is **progress** toward useful previews. Protected PR34 passed
hosted Linux **58/58, 112.38 s** and Windows core cross-build, then merged the
tested tree. The next local Ubuntu DEB/source pair is
`0.1.0~preview.20261008013908.97a307fcf2ba`; all **710** tracked corresponding-source
files were independently verified. Normal package-manager upgrade in the owned
runtime rootfs succeeded without a downgrade override.

The actual installed GUI displays the first rejected and previous recording clock
details, saves a verified 1,024-frame raw prefix and attaches its asset/clip. Its
finalized journal preserves end reason6, zero rejected frames and timing origin.
Normal Save/Quit and retirement complete; host default metadata/existing links
remain identical. The [separate receipt](../tests/results/X007/2026-10-08-installed-recording-fault.json)
retains original GUI screenshots, stderr logs, take and package/upgrade metadata in a
CRC/byte-verified **2,086,164-byte** capsule. Prior receipts/takes remain untouched.

This new candidate's diagnostic-failure test does not qualify a sustained successful
recording/EQ/reopen/export workflow; the earlier preview's evidence keeps its own
scope. The [guide](90-preview-guide.md) lists both local candidates and installation
commands. No host package/audio, VM, equalizer writes or binary release upload.
Shared-kernel Xvfb/private PipeWire evidence remains separate from complete desktop,
physical/real-time audio and native Windows qualification.

Next concrete task: portable failed-job fault state, then native input acquisition
and a tested startup/alignment policy, followed by new normal installed recording
and complete desktop/Windows preview workflows. Pending equalizer adaptations are
still separately reviewed work. Every frozen F/Q/C/N contract and full Europe/
X004/X005/X006 remains incomplete; the full goal stays active.

Review identified that the initial action logs contained only shell stderr and
that host graph inputs were kept private. An [additive supplement](../tests/results/X007/2026-10-08-installed-recording-fault-supplement.json)
retains five actual completed command execution records and before/after redacted
canonical fingerprints covering all default metadata and link objects. Its
6,747-byte capsule leaves the original receipt/archive unchanged. The standalone
`tools/verify_installed_fault_evidence.py` replays capsule integrity, command/exit
records and route-fingerprint equality without private dumps or a live namespace.
This is evidence replay, not a new GUI test or reconstruction of omitted host values.

## Portable recording diagnostics checkpoint (2026-10-08 UTC)

This continuation is **progress** toward useful previews. Single-track first
faults now have immutable, identity-bound, schema1.0 sidecars published after
native/writer joins. Reopening or moving the project shows saved details for
attached and unattached jobs through Review recordings. Optional metadata errors
cannot invalidate the raw take; storage failure reports separately and retains
the original writer error. No project/checkpoint schema change or callback I/O.

The latest Linux Debug cohort passes **63/63, 179.68 s**; affected
ASan/UBSan/LSan passes **7/7, 31.02 s** with matching source/resource inputs. Two
private production-owner checks preserve verified 1,024-frame prefixes with
successful/refused publication and unchanged host routes. Windows storage TUs
compile only; native Windows runtime remains open. Three original compile
failures and a correction for control observations preceding callback drain
are retained, without rewriting the observation or existing raw evidence.
See [the contract and receipt](92-portable-recording-faults.md).

Four new messages bring Qt catalogs to 543 keys, 538 finished English entries
and five unfinished numerus entries. Each of 32 non-English drafts still has
eight translations; zero native-reviewed/fully UI-qualified languages. Pending
equalizer inputs remain separately reviewed work, with no equalizer changes.

The prepared97a DEB predates these sidecars. Next: a new qualified package/source
pair and installed reopen workflow, then native acquisition/startup alignment
and complete desktop/native Windows previews. Original native71 and 2,048 leading
silent frames, multi-track durable diagnostics and every full frozen F/Q/C/N,
X004/X005/X006 and Europe gate remain unresolved; the full goal stays active.

## Installed portable-error preview (2026-10-08 UTC)

The next local Ubuntu 26.04 amd64 package/source pair is
`0.1.0~preview.20261008034000.743392ece10a`; all **719** corresponding-source files are
independently verified. Normal owned-rootfs upgrade and the actual installed GUI
fault/verified raw prefix/Save/Quit/reopen/historical review pass. Both app exits
and launcher exit0; original raw take, journal, sidecar and project bytes are
unchanged on reopen/review. No recording endpoint or audio preview is started by
the historical error row. Host default/link fingerprints match.

The [separate receipt](93-installed-portable-fault-preview.md) retains exact
commands/exits, screenshots and original bytes in a 2,304,204-byte capsule. The
original package/evidence scopes remain unchanged. Initial hosted Linux passes
**59/59, 110.37 s** and Windows core cross-build only. A review's normal-Stop concern is
checked with explicit assertions: existing production guard already excludes
normal Stop; added Debug **2/2, 1.45 s** and sanitizer **2/2, 3.69 s** checks pass with unchanged
production inputs. Final protected checks for the added assertions are separate.

This is **progress**, with a local installable candidate and matching GPL source,
not a binary release upload or full DAW completion. Next: native acquisition/
alignment and this candidate's normal installed recording/EQ/reopen/export,
then full desktop/native Windows previews. No equalizer, VM, host package or
audio configuration change. All full frozen parity/Europe/X004/X005/X006 and
original native71/2,048-frame startup-silence gaps remain open; goal stays active.

## Current installed candidate: normal workflow (2026-10-08 UTC)

The unchanged `743392e` Ubuntu candidate now passes a new actual installed normal
workflow: **540,672 raw frames / 11.264 s**, normal Stop/verified attachment,
live EQ −12→−6 dB and Undo, Save/Quit/reopen, and two byte-identical ten-second
WAV exports. The independent direct-form I EQ oracle differs by at most
**2.842170943040401e−14**. Live output has 73 stable −12 dB and 22 stable −6 dB
windows in the expected order. Normal Stop produces no error sidecar/storage
warning; original WAV/journal/project bytes are preserved. Both app exits and
launcher exit 0, owned nodes retire, and host routes are unchanged.

See [the scoped workflow and immutable receipt](94-installed-normal-preview.md).
The 2,605,032-byte capsule retains all nine actual action/probe commands, original
bytes, graphs, screenshots and independent checks; all 101 logical entries and CRC
are verified. The owner has the DEB/source/instructions/checksums in Downloads.
The exact 719-file package source and previous receipt scopes remain unchanged.

Protected PR36 merges tested `c686f40` as `9c8df5c`, trees identical. Final required
hosted Linux passes **59/59, 139.63 s**; Windows core cross-build passes. A verified
source ZIP/Git bundle and full restored-byte/fsck check back up that source;
Windows transfer remains pending a destination. No binary release upload or host
package/audio/VM/equalizer change.

This is **progress** and a useful local Linux preview. Native acquisition/
alignment remains the next concrete implementation task: 2,048 initial raw zeros
are retained, exact periodic suffix equality cannot detect missing whole periods,
and original native71 remains unresolved. Complete desktop/physical/RT and native
Windows, broader install/recovery, translations and full frozen F/Q/C/N/
X004/X005/X006/Europe gates remain open; the full goal stays active.

## 2026-10-08: bounded input-start observation after local preview delivery

Protected PR37 merged the installed normal-workflow source/evidence as
`bd7e547c9a1b95832d3631574949809f73746375`, with identical tested tree. Required
hosted Linux passes 59/59 in139.99s; Windows core cross-build passes only. A new
ZIP/bundle restores that exact main tree, full Git fsck and every source byte.
No Windows transfer or release upload was performed.

The [new input-acquisition observations](95-input-acquisition-observation.md)
complete three private 96,000-frame production-owner runs. One native EMPTY
2,048-frame lease precedes an exact nonperiodic source suffix; direct runs retain
4,096 encoded silent frames. All callback clocks/lease ownership/RT counters pass
within this diagnostic scope. Replay refuses33 altered claims. Original startup
symptoms remain visible; no raw trimming, latency fix or installed-binary change.

Next: input latency observations and explicit recording-start alignment, then
native Windows workflow/installer previews. Source and local packages are useful
previews; full-suite and platform/language gates remain incomplete.

## 2026-10-08: native Windows desktop toward useful previews

Native WASAPI single-track recording and deferred output playback now connect to
standard desktop factories. Stable device/channel route identities survive friendly
name changes; fresh preparation rejects invalid/stale/duplicate/cross-device/rate
choices. Project creation offers its sample rate, validates before I/O and prepares
its initial EQ below Nyquist. Windows monitoring remains Off-only, with unavailable
choices visible and Off selectable for portable projects. Windows duplex/punch,
conversion and independent clocks remain in scope.

Native MSVC 19.44.35228.0/Qt 6.12.0 Release with matching MSVC libsndfile passes
8/8 checks in limited interactive Windows session 1. Actual empty-project main
launch and normal close return exit 0. UI audio-control tests use synthetic
endpoints; no native GUI audio or compiler-free installer workflow is established.
The native tests exposed a late disk failure after processing Complete. A new
held-writer fixture reproduces the old timeout; independent raw storage-fault
handling now retains the original error, other full takes and failed-lane recovery
on Linux and Windows without rewriting the processing terminal status.

[Checkpoint and original failures](98-windows-desktop-foundation.md) include a
331-file verified developer code/resource closure and a 67-payload hashed capsule.
Current catalogs have 546 source keys, 256 partial draft translations and no
language promotion. The 18 changed upstream equipment/localization inputs still
need review/adaptation before delivery. The existing Ubuntu preview files remain
available; no replacement package or binary release upload is claimed here.

Next: actual native GUI recording/attachment/playback/live EQ/Undo/save/reopen/WAV
with owned Windows audio, then a compiler-free local installer/source pair and an
independent clean-install clone. Original capture's SDK gap, native71, startup
alignment, physical/sustained gates and full F/Q/C/N requirements remain open.

## Native Windows desktop workflow checkpoint — 2026-10-08

The [actual native one-track desktop workflow](99-windows-desktop-workflow.md)
records 480,000 mono frames from an explicitly selected owned WASAPI loopback
channel, acknowledges live EQ/Undo, finalizes/attaches, plays through the selected
native output with two live edit/Undo receipts, saves/reopens passively and exports
WAV through the desktop dialog. Independent source regeneration, raw and saved EQ
export comparisons have zero maximum sample error. Native playback has zero
residual across 479,936 matched frames; unmatched tail is below threshold and the
unselected stereo output is silent. Nine evidence mutations are refused.

The accepted source has a silent lead-in. An original non-silent run exposed an
unisolated 480-frame startup attenuation and is retained without promotion.
The 77-payload capsule contains original build/export/sample refusals, final
native results, code/resource hashes, media and independent checks; no executable
or credential helper. MSVC/Qt/UCRT runtime was supplied by the developer harness.
PR 41's foundation changes merged after the corrected hosted checks passed.

Next: review the 18 pending upstream equipment/localization inputs, prepare and
qualify the Windows compiler-free installer/source pair on an independent clean
clone, and investigate non-silent startup. Windows monitoring/duplex, conversion,
physical inputs, sustained scheduling, original capture gap and native71 remain
open alongside the unchanged full-suite scope. Existing Ubuntu preview unchanged;
no Windows binary release uploaded.

## Reviewed localization/editor refresh — 2026-10-08

[Checkpoint 100](100-localization-reuse-refresh.md) adapts 22 changed reviewed
equalizer inputs and 66 retained TS catalogs, preserving immutable original
DSP/equipment provenance and both equalizer working trees. Context-mapped drafts,
standard actions, QLocale script/territory selection and signed RTL numeric input
now pass six focused Linux groups and native MSVC/Qt localization/editor/export
tests. Catalog regeneration preserves unfinished translator text/plurals/comments;
13 altered-catalog cases are refused. English plus 33 drafts have 564 keys, each
draft translates 95; no native-review/full-UI promotion or all-Europe coverage.

The first native controller run timed out at line 463; its unchanged rerun passes.
Both results/dispatcher timeouts remain visible, cause unisolated. The Windows
media oracle now rejects all nonfinite raw/export/native-channel samples before
reductions; twelve rehashed NaN/infinity cases supplement the nine prior evidence
mutations. PR42 review is resolved through that tested fix, without bypassing
required checks. The original native startup discrepancy remains open.

The Windows runtime deployment is staged; a full independent pristine-template
clone has been created with no backing disk. It is not an installer qualification
yet. Next: compiler-free Windows installer/source pair and clean-clone normal
launch/record/EQ/save/reopen/export/removal, then continue full-suite milestones.
The existing Ubuntu preview remains available; no binary release upload.

## Useful preview delivery checkpoint (2026-10-08)

The reviewed equalizer localization/editor refresh and actual rendered RTL meter
fix merged through protected PR43; required Linux and Windows cross-build checks
passed at the corrected head. Windows native Qt checks are recorded separately.
The existing local Ubuntu recording/EQ/save/reopen/WAV preview remains available.
Windows per-user installer/source preparation now has bounded input refusal
tests, fixed-file removal and independent signed runtime setup; clean installed
main/shortcut/audio/uninstall tests are the next gate. This is progress toward
X007, not full-suite or installer qualification. See [checkpoint 101](101-windows-installer-preparation.md).

## Windows installable workflow preview checkpoint (2026-10-08)

Protected PR44 merged installer preparation and review corrections after both
required checks passed. The source-paired Windows 11 x64 candidate now has scoped
clean-clone installation, runtime bootstrap, shortcuts, actual-main/dependency
launch, exact-file removal/reinstall and project/settings preservation evidence.
A test-only production-UI/native-controller workflow uses installed DLLs; its
unchanged retry passes independently checked raw/live-output/export samples.
The original 480-frame observer discontinuity remains retained and unisolated.
See [checkpoint 102](102-windows-installed-workflow-preview.md) and X007 receipt.
This supports a useful local Windows preview alongside the Ubuntu DEB, with no
public binary upload. Physical/sustained audio, monitoring/duplex, broader
installer/language/release gates and full F/Q/C/N parity remain open. The full
goal stays active and incomplete. Next: physical format/record/play diagnostics
and startup/discontinuity qualification.

## Installed preview evidence review checkpoint (2026-10-08)

The Windows candidate's exact native source inputs now match its frozen Git
commit, including the native fixture source. A new unchanged installed workflow
records the fixture's own module paths and pinned executable identity; the five
Qt/platform/media DLLs load from the installed slot. Independent samples again
pass with zero raw/export errors and measured playback residual. Ten mutations
refuse stale sources, SDK-loaded dependencies and incorrect fault claims.
The expanded capsule preserves all original payloads and both failed inspection
harnesses. The original observer discontinuity remains unisolated. The clean
clone is shut down and temporary transfer infrastructure is removed.
This strengthens the local preview evidence without changing installer bytes
or claiming physical, sustained or full-suite qualification. The next concrete
implementation remains actionable device-format diagnostics for recording and
playback, followed by native physical workflow qualification.

## Device-format control checkpoint (2026-10-08)

Shared typed WASAPI admission now explains rate, device and channel conflicts
before the GUI enables Play/Record. Input and monitor conflicts create no active
endpoint or disk job; authored incompatible routes survive Save/reopen. Stop
remains usable, and matching explicit choices can play and record. PipeWire
keeps its rate-negotiation behavior. See [checkpoint 103](103-device-format-diagnostics.md).
Linux focused/full UI, port admission, localization/catalog and affected
sanitizer checks pass. Independent Windows MSVC/native Qt tests pass focused/full
UI, localization and port admission in limited interactive session 1 with owned
fake endpoints. The first Linux test timeout and its explicit readiness-wait
correction are retained. Native source hashes match the frozen code commit.
The clone is shut down; originals and the clean template remain preserved.
These are source/control improvements; existing installers retain earlier
frozen code. Native rate conversion, drift adaptation, physical/sustained audio,
Windows monitoring/duplex, installer refresh and full frozen parity remain open.
Next: owned non-silent native startup/timing observations and correction, then
physical recording/playback qualification. The full goal remains active.

## Direct native startup checkpoint (2026-10-08)

Two direct Windows render runs reproduce alteration in frames 0–479 without the
DAW mixer/EQ. Actual SDK lease samples match independent source regeneration;
later loopback samples match exactly. A defined silent-lead comparison has no
observed alteration. This narrows the defect to the SDK lease through loopback
path without identifying an OS/driver component or adding a workaround.
Native hashes, media, timing, process identities and exits are retained and
independently recomputed. Linux synthetic poisoning/ramp cases and output/route
contracts pass. See [checkpoint 104](104-windows-direct-startup.md). The owned
clone is shut down. Next: explicit startup scheduling with project/native timing
separation and a device-period experiment, followed by physical qualification.
Existing installers and all full-suite gates remain unchanged. The goal is active.

## Native startup scheduling checkpoint (2026-10-08)

WASAPI now owns one admitted device-period silent interval before content, with
separate native/content queue coordinates and immutable timing in controller
snapshots. Source/DSP frames and parameter receipts do not advance through startup.
The owned endpoint's 480-frame interval preserves direct noise and a one-sample
impulse exactly; an Immediate control retains the original first-480-frame failure.
Prepared cancellation processes no source and is not promoted to completion.
The integrated production desktop's non-silent take, live EQ/Undo, save/reopen and
WAV export pass independent raw/export/native sample checking. Native full UI,
controller and timing tests plus Linux affected/sanitizer checks pass. See
[checkpoint 105](105-windows-startup-scheduling.md). The clone is shut down.
This qualifies startup on one owned native endpoint, not arbitrary hardware,
capture discontinuities, sustained audio or full parity. Next: refresh and qualify
the exact local installer/runtime, then repeat/seek/end-boundary and physical
workflows. The full goal remains active and incomplete.

Modernizing the older playback probe additionally qualifies native/content/end-
slack accounting at normal completion. Active cancellation reveals altered
final loopback samples and is independently refused despite SDK success.
The retained failure remains open; next investigate the native Stop boundary,
then refresh the installer. Prepared cancellation and non-silent startup evidence
remain separately qualified on the owned endpoint.

## Direct native Stop/end checkpoint (2026-10-08)

Startup scheduling and localization inventory refresh passed protected hosted
checks and merged through PR #48 at `c2622b4cf518cb962807cf0cff886eaa3abf4c79`.
Two new direct renderer active-Stop runs preserve the captured prefix exactly;
the production mixer/EQ probe's altered-tail failure remains unresolved. A new
non-silent ending reveals 64 source frames absent from loopback despite the
drained flag. No fidelity threshold is relaxed or native cause asserted. Exact
native sources/media/exits and independent analyses are retained and recomputable;
the owned clone is shut down. See [checkpoint 106](106-windows-stop-boundary.md).

This is progress toward reliable installable previews, not completion. Next:
bounded native end guard with separate queue accounting and exact final-frame
evidence; production EQ lease tracing at Stop; then installer/runtime refresh.
Existing local Linux/Windows installers remain available at their earlier source.
The full DAW goal remains active and incomplete.

## Native end-guard checkpoint (2026-10-08)

A device-period native-only end guard now follows normal Finish without source,
DSP, project or receipt advancement. Total/native callback/guard extents are
distinct, finite source mapping excludes both boundary intervals, and Stop/fault
paths remain interruptible. Same-binary direct controls reproduce 64 missing
frames with Immediate end and preserve the entire non-silent source with guard,
including one-frame and 31-frame ranges. Guard cancellation is not completion.

Native production playback matches all 192,000 source frames; the real desktop
workflow matches all 480,000 raw/export/playback samples. Native main normal close,
UI/controller and 28 timing checks pass, alongside Linux focused/sanitizer checks.
The production-EQ active-Stop sample failure still reproduces and remains strictly
refused. Source-pinned native evidence is retained; see
[checkpoint 107](107-windows-end-guard.md). No installer qualification or parity
promotion is inferred. Next: installer refresh/installed runtime with limitations,
then actual production lease tracing at Stop. The full goal stays active.

## Installed Windows refresh checkpoint (2026-10-08)

The startup/end-guard source is now paired with a refreshed local Windows setup
and GPL/dependency sources. New-slot install, payload hashes, shortcuts, actual
main normal launch/close, removal/reinstall and project/settings preservation pass
on the independent installer clone with its existing Microsoft runtime. It does
not establish fresh OS/runtime bootstrap acceptance.

All three installed native workflows are refused for SDK packet discontinuities:
the first preserves a full raw take then faults in the playback observer; two
unchanged repeats preserve partial raw takes. Every saved raw sample matches the
original source. All failures and final cleanup are retained, not replaced with
success. See [checkpoint 108](108-windows-installer-refresh.md).

The Ubuntu recording preview remains available. The refreshed Windows artifact
is delivered locally as an installation/UI preview with experimental native audio
and explicit abrupt-Stop/capture limitations. No public binary upload or parity
promotion. Next: bounded native packet/lease telemetry and installed capture
continuity qualification, plus the independent production-EQ Stop trace.
The full frozen goal remains active and incomplete.

## Native capture lease trace checkpoint (2026-10-08)

PR #51 merged the refreshed installer evidence after protected checks passed.
Optional capture metadata now records bounded SDK lease and callback timing,
device positions/flags, separate QPC domains and explicit observation loss.
Native same-binary ten-second attempts have actual exits 0, 2, 2: one full take
and independently verified EQ export, two partial takes refused for SDK gaps.
All preserved raw samples match the original deterministic source exactly.
Portable queue/audit/sanitizer tests and native unit checks pass; retained media,
negative results and trace recomputation are backed by corruption/claim refusals.

One failure includes a lease longer than the reported device period; another
follows a long event wait without a held lease. This establishes packet gaps
reported before processing, without isolating a driver/VM/scheduling cause or
qualifying installed recording. A cable comparison stopped before audio when
the interactive session changed; its child exit remains unknown. Further native
work stopped to preserve visible equalizer activity. The refused task was
terminal, but its unregister was not confirmed before access was lost and the
VM was observed off. See [checkpoint 109](109-windows-capture-trace.md).

Next: pre-admitted packet copy and SDK release before DSP, with exact metadata,
pointer lifetime and continuity evidence; use the independent installer clone
for further native checks. Existing local preview/source pairs are unchanged;
production-EQ Stop and all full-suite gates remain open. The goal stays active.

## Capture packet ownership checkpoint (2026-10-08)

PR #52 merged with protected checks passing. Capture now prepares a resource-
admitted reusable packet copy, releases the native SDK lease before processing,
and retires storage after join. Failed releases never reach DSP; silent/invalid
backing and exact flags/timestamps retain existing strict input semantics.
Poisoned SDK reuse through the real raw/EQ pipeline proves the consumer does not
depend on released backing. Focused Linux and sanitizer checks pass 3/3; the
actual Windows SDK adapter and packet tests cross-compile successfully. Historical
v1 evidence and all original failed exits remain independently recomputable.

V2 traces explicitly record release before processing; synthetic order/type
controls keep v1/v2 timing separate. A Windows MSVC unit job is added with no
endpoint/GUI dependency; its execution remains pending. No native endpoint test
or installer refresh is claimed for this change. See [checkpoint 110](110-windows-packet-release.md).
Next: isolated full native build and same-source bounded v2 capture comparisons
on the installer clone, then exact installed qualification. The goal stays active.

## Hosted native recording build checkpoint (2026-10-08)

The packet-ownership MSVC gate passed 3/3 at `cfa5b5b` and `ac11b80` and is now
required by strict main protection. Source PR #53 remains open: the compound
route-admission review is fixed in source but its inactive endpoint fixture has
not yet executed. Full production recording and fixture code cross-compile.
The hosted gate is extended with hash-pinned, codec-disabled shared libsndfile
1.2.2 to compile that path and run existing disk recording/recovery, WAV export
and resource tests; this expanded gate passed 6/6 at `58a23cb` on hosted MSVC 19.51.36260.0.
Retained [native results](../tests/results/X007/2026-10-08-hosted-native-recording/receipt.json)
include 255 recording/recovery checks and 1,062 export checks with raw headroom
and live/offline comparisons. Six of 37 configured tests ran; no audio endpoint,
GUI or installer executed. The 57.17-second Windows recovery suite gets a
180-second CTest observation budget; assertions are unchanged and Linux retains
60 seconds. That timeout adjustment awaits checks. No endpoint or installed
qualification is inferred from the hosted pass.

Host reboot/resource contention interrupted full independent clone preparation.
The clone disk remains unverified. TPM and firmware copies are independently
byte-verified; the clone is prepared with 8 GiB RAM, CPU/disk caps and isolated
networking and remains off. The same suspended verifier resumed after host load
fell, in a systemd-managed 16 MiB/s, idle-priority scope. Continue this exact
verifier before native route/capture comparison while keeping local VM load low. Existing previews stay unchanged and
the full frozen-reference goal remains active.

## Output preparation rollback checkpoint (2026-10-08)

PR #53 merged with all three required checks and Socket checks passing. The
reviewed input-credit leak is fixed by tentative owner staging; actual capture
endpoint admission/comparison remains pending. The same pattern in playback
output selection is now corrected: SDK preparation refusal returns interleaver
credit, and a successful inactive preparation commits both owners together.
The real control owner, with only SDK boundary symbols injected, reproduces
the old failure and passes the fix, repeated refusal/retry/replacement and full
retirement on Linux Release and ASan/UBSan. MinGW cross-compilation also passes;
its MSVC test addition is pending.
See [checkpoint 111](111-output-admission-rollback.md). No audio/GUI/installer
qualification or frozen parity status is promoted.

Frozen native comparison builds now require independent fresh directories and
verified compiler/dependency/input/binary identities. Scripts parse, but neither
build nor endpoint runner has executed in the unbooted new clone. Continue the
existing capped verifier, then native capture and production-EQ Stop qualification.
The verifier is currently suspended to reduce disk activity following the owner's
report. Disk capacity, available RAM and unused disk swap do not show a shortage;
the earlier crash remains undiagnosed. No new local VM or heavy build was started.
The complete professional DAW scope stays active and incomplete.

## Production output sample trace checkpoint (2026-10-08)

PR #54 merged after all required checks passed, including seven hosted native
MSVC tests. The output-credit rollback is qualified within its SDK-injected owner
scope. A new optional, resource-admitted production render trace now retains actual
post-EQ lease samples and native release results. Its SDK-free tests pass Linux
Release and ASan/UBSan; the Windows fixture and trace test cross-compile. Synthetic
analysis rejects missing/corrupted metadata and identifies altered sample banks,
observer tails and unobserved committed extents without gain normalization.
See [checkpoint 112](112-production-render-trace.md). Hosted MSVC execution of
the addition and actual native endpoint/Stop qualification are still pending.

No VM is running. The existing image verifier is suspended; newly copied disk
equality is not inferred. The owner's VM budget is one VM at a time, brief runs,
and the verifier suspended during each run. Existing previews remain unchanged,
and the full frozen Bitwig/Cubase/Linux/Windows/localization goal stays active.

## Import feasibility and resource budget checkpoint (2026-10-08, later)

PR #55 merged after all required checks passed. Eight hosted MSVC unit/synthetic
tests passed at exact head `0ab4dbb`; the actual production render trace fixture
compiled without endpoint activation. Its native log and receipt are retained at
`tests/results/X007/2026-10-08-hosted-render-trace`. Native active-Stop and capture
qualification remain pending; existing preview binaries are unchanged.

The first X004 RPP structural library preserves source bytes and unknown state,
requires shared admission and supports cancellation. Linux Release/sanitizers pass
and Windows cross-build passes. Hosted execution of this addition is pending.
It is not native compatibility or semantic track import; see
[checkpoint 113](113-rpp-structural-inspection.md).

All VMs remain off. The same clone comparator resumed at 8 MiB/s while no VM runs;
equality is still unverified. Follow the owner's latest policy: one local VM at a
time, brief tests, check before boot and suspend the comparator during each test.
Current available RAM/free disk and negligible swap use do not show swap shortage.
The complete professional DAW goal remains active and incomplete.

## Import worker checkpoint (2026-10-08, later)

The RPP structural component at `2f81b97` passed nine hosted native MSVC
unit/synthetic tests, including its 174 structural checks. Native writer/corpus
and semantic compatibility remain unqualified. Exact native logs are retained.
PR #56 merged after all required checks passed.

A separate C++ inspection process now loads a selected plain file with shared
payload admission, captures a structural byte inventory and streams a bounded,
versioned SHA-256 report. Child-process tests cover cancellation/refusals,
provenance and untouched source. See [checkpoint 114](114-import-inspection-worker.md).
Linux Release/sanitizers and Windows cross-build pass; hosted native execution
of the worker remains pending. GUI integration, hard process/sandbox policies,
persistent opaque state and semantic conversion remain gates. Existing previews
are unchanged; no local VM was started for this work.

## Desktop import inspection checkpoint (2026-10-08, later)

PR #57 merged after all required checks passed. Its exact hosted MSVC revision
`8d65af6` passed 129 worker-process checks and ten selected tests from 41
configured tests, without activating an audio endpoint or local VM. Logs and
source/runner identities are retained separately from the new desktop work.

[Checkpoint 115](115-desktop-import-inspection.md) adds a single-flight Qt parent
with shared admission, independently captured source/hash, strict report
validation, actual child retirement and a read-only File-menu outline. Linux
Release and corrected ASan/UBSan controller/GUI/worker checks pass. A minimal
probe identifies the initial sanitizer Qt signal-lookup failure with `-fno-pie`;
that failed run is retained, and `-fPIC` rebuilding passes. Windows cross-build
passes; Windows Qt execution and refreshed installed packages remain open.

The outline changes no canonical project/source bytes. Persistent opaque-source
bundles, source-writer/version corpora, semantic mapping, conversion, OS sandbox
and the complete X004 adapter program remain required. Existing end-user
installers remain unchanged. All local VMs stayed off; the existing independent
clone comparison remains pending under its 8 MiB/s cap. The full frozen-reference
professional DAW goal remains active and incomplete.

## Saved inspection checkpoint (2026-10-08, later)

PR #58 merged at `4bc422c` after all required checks passed. Its exact prior
head `e391ee9` has retained hosted evidence: 74 Linux tests, ten selected native
MSVC tests of 41 configured, 129 worker-process checks and Windows cross-build.
No Windows Qt controller or installed workflow is inferred from those checks.

[Checkpoint 116](116-inspection-bundles.md) adds portable exact-byte inspection
Save/Open, shared retained protocol admission and preservation of existing
destinations. Library/controller/UI failure and relocation evidence is separate
from native semantic import, source-version corpora and platform/install gates.
34 catalogs now contain 606 source keys; no new reviewed/qualified languages.
All local VMs stayed off. The same capped independent-clone comparison remains
pending. The full frozen-reference DAW goal is active and incomplete.

## Scoped Windows bundle qualification and recording CI observation (2026-10-09 UTC)

Exact `939850c` passes100 actual MSVC bundle checks,174 structural checks and129
worker-process checks. All75 Linux tests and Windows cross-compilation pass.
The native job fails its separate recording-recovery test: QueueFull produces a
392192-frame prefix rather than480000 frames. Original failure artifacts and
exact source/runner identities are retained; the cause is unknown without writer
phase timings. A single unchanged native job rerun passes all eleven selected
tests, including255 Windows recording checks and480000 frames. It is separate
evidence, not an explanation of the original failure. PR59 merged through all
required checks at `e23e3dc`.

[Checkpoint117](117-recording-ci-observations.md) adds existing bounded
disk-worker wall/occupancy diagnostics to the concurrent synthetic recording
fixture without changing capture policy or relaxing its acceptance criteria.
Linux Release passes277 recording checks and the existing timing helper test;
new Windows execution is pending. No Windows Qt, installed bundle workflow,
native endpoint, sustained capture, semantic import or full-parity status follows
from the scoped bundle success. No local VM started; the full goal remains active.

## Native-writer corpus checkpoint (2026-10-09 UTC)

PR60 merged through required checks at `40926f1`. Exact prior head `d1eafaeb`
passes75 Linux and twelve selected MSVC tests, including256 instrumented recording
checks and complete bounded phase pairs. Its47.8653604-second synthetic producer
is not native timing evidence or an explanation of the retained earlier failure.

[Checkpoint118](118-native-writer-import-corpus.md) adds seven original projects
actually saved/reopened by unmodified REAPER7.82/Linux through public APIs, exact
runtime/source/media hashes and matching native property observations. Original
PCM and authoring code use GPL-3.0-only; proprietary runtime/assets remain private.
An isolated, display/audio/network-free fresh reproduction completes under a
32MiB individual-file bound after a retained8MiB default-theme extraction refusal.
The real inspector preserves original bytes and matches native TRACK/ITEM counts;
properties remain unverified. New hosted Windows inspection is pending, and a
Windows source writer, semantic IR/conversion, independent renders and all other
native/exchange formats remain gates. No local VM or user audio route changed;
the existing comparator was paused when another testing VM was observed active.
The full functional/quality/content/native parity goal remains active/incomplete.

## Source-property intermediate model (2026-10-09 UTC, later)

PR61's corrected exact `96ffb4e` passes76 Linux tests and13 selected MSVC tests . The original Lua CRLF-default checkout hash failure is retained;
explicit LF fixes it without altering frozen fixture bytes. MSVC structural
inspection passes6,013 corpus checks; Windows writer/semantic conversion remains
unqualified.

[Checkpoint119](119-import-intermediate-properties.md) adds a resource-owned
framework-independent C++20 import representation with stable source-property IDs,
separate original gain layers, exact opaque bytes and explicit missing/ambiguous/
unsupported state. Linux Release/ASan/UBSan each pass885 new checks and five related
Release tests pass. MinGW compilation is separate from pending native execution.
The next task is isolated property protocol, independent parent validation and
read-only loss preview, then approved media/new-project mapping and aligned render
workflows. Existing installers/equalizers/user audio remain unchanged, no local
VM started, and the full frozen-reference DAW goal remains active/incomplete.

## Source-property desktop checkpoint (2026-10-09 UTC)

PR62 merged at `20321cc0a50813e7641641048a17d49a6b009739`. Its repaired exact
`5704601` passes77 hosted Linux tests and14 selected MSVC tests, including890
property checks. The original line-evidence review and correction remain retained.
Those results qualify the original model, not the following protocol/UI changes.

[Checkpoint120](120-import-property-preview.md) adds a versioned isolated property
report, independent complete ownership/shape/token/evidence validation, read-only
Properties/Original source tabs and portable persistence with old-v1 compatibility.
Seven selected local Release tests and six ASan/UBSan import tests pass; leak
detection is disabled.569 boundary checks include seven original writer projects,
seven malformed/ambiguous fixtures and28 corruption refusals. The original
same-line token-swap failure is retained before correction. MinGW compilation
passes; new hosted native and Windows Qt/install gates remain separate. Catalogs
contain669 source keys,3,135 draft translations, no native-reviewed/fully-qualified
language. No local VM or user audio/equalizer/installer changed. Next: approved
media roots/missing choices and opt-in new-project conversion with aligned renders.
All four parity axes and the full frozen-reference goal remain active/incomplete.

### PR63 review/platform follow-up

The initial exact `1bb2afd` passes the Windows cross-build and new Linux property
boundary test but has two retained failures: native Windows defaults its witness
reader to cp1252 (fixed by explicit UTF-8), and Linux manual-recording-control
refuses a command with insufficient diagnostic context (cause unknown). Its
original archives/logs remain in checkpoint120 evidence. A test-only refusal
context adds no policy/oracle change; a local65,141-check pass does not explain
that failure. The UI summary now excludes missing/invalid unavailable entries,
caches the count on report change, and passes90 Release and90 ASan/UBSan checks.
Corrected protected hosted checks remain pending; Windows Qt/install/native
conversion and all full-parity axes remain incomplete. No local VM started.
