# X006: track scalability for recording studios

Owner requirement, 2026-10-06 (America/Detroit). First model/parser/mix/desktop
implementation, 2026-10-07; full requirement remains incomplete.

## Product target

Serve lower-budget recording studios, including studios with substantial audio
hardware. Support any finite project track count that the configured machine can
store and operate within admitted resources, without a fixed product or license
ceiling. Track count grows independently of physical interface channel count.
Keep the full frozen Bitwig/Cubase target and Linux/Windows functional parity.

Distinguish total project tracks, currently executing processors and concurrently
recorded hardware inputs. A stereo interface can support a project with many audio,
MIDI, instrument, hybrid, folder and bus tracks. A larger interface enables more
simultaneous inputs; its driver/ports, aggregate capture bandwidth and prepared
memory are separate constraints. More expensive hardware does not establish that
an arbitrary plugin graph will meet a low-latency callback deadline.

Expose resource use and explain refused preparation in terms of the required and
available memory, IO, ports or measured processing budget. Preserve the project
and offer concrete choices: freeze/bounce selected tracks, render offline, reduce
processing load or explicitly reconfigure the buffer/device. Never silently omit
tracks, truncate media, change hardware defaults or increase latency. Offline
rendering may take longer than the song; real-time monitoring cannot.

## Current implementation limits

The current foundation **does not yet meet X006**:

| Area | Current bound / implementation | Required change |
|---|---|---|
| Session validation | Trusted state/validation payload budget; indexed stable-ID lookup (`src/session.cpp`) | Combined snapshots/history/graph envelopes and more track types/workloads |
| Project parsing | Schema 1.7; caller-owned encoded/parser/state budgets; bounded SAX preflight (`src/project_store.cpp`) | Shared large-media/cache workloads and streaming or partitioned state as needed |
| Prepared mix | Resource-admitted dynamic track routes and indexed EQ preparation (`src/mix.cpp`) | Measured aggregate memory/event/CPU bounds, richer graphs and scheduling |
| Live replacement mask | Prepared, charged dynamic mask (`src/mix_playback.cpp`) | Sustained/native qualification at large counts |
| Recording owner | 256 armed tracks/packed native inputs and bounded aggregate capture budgets | Scale arm descriptors independently from backend port capabilities; prepare capture against actual IO/reserve needs |
| Readers/UI/history | Shared media pool; snapshot-backed selectors/viewport interval painting; configurable history with declared operation-work checks | Sustained media, remaining meters/waveforms, GUI paging and measured aggregate history/snapshot/index memory |

Some other arrays of size256 address **channels within a bus**, not project
tracks. Audit their meaning before changing them. This requirement does not
silently make each backend, plugin layout, processor or operating-system handle
limit unbounded. Those capabilities remain explicit and separately versioned.

## Engine and project design

1. Keep dynamically sized canonical track descriptors with stable IDs. Validate
   counts and size arithmetic against explicit caller-owned admission budgets.
   Untrusted project files cannot raise their own trusted resource budget.
2. Compile admitted active graphs into fixed-capacity arrays/arenas off the audio
   thread. Capacity is chosen for that graph; no vector resizing, allocation,
   destruction, disk IO or blocking lock occurs in a callback. Safe graph
   replacement must account for the combined old/new/tail resource envelope.
3. Separate project inventory from execution. Freeze or deactivate processors
   only through defined semantics. Muting alone cannot skip sidechain dependencies,
   automation, MIDI/note releases, latent output, tails or future wake events.
   Keep originals/plugin state so frozen tracks remain editable through unfreeze.
4. Allocate work only for admitted processing and media windows. Stream media
   through bounded shared read-ahead/cache resources; preserve independent capture
   prefixes when storage cannot keep up. Closed/inactive tracks still remain fully
   represented in persistence, undo, import/export and recovery.
5. Schedule independent processing across prepared DSP workers when implemented.
   Admission accounts for dependency depth, per-worker work and synchronization,
   rather than assuming track count or core count predicts throughput. No generic
   blocking thread-pool dispatch inside the callback. Preserve sample timing,
   processor state, PDC and determinism under the documented quality envelope.
6. Virtualize track lists, timeline rows, meters and waveform detail. Bound GUI
   updates independently of project size; retain reliable controls/automation and
   drop only explicitly lossy visualization updates under pressure.
7. Version larger-project persistence when needed. Older readers must refuse
   unsupported state safely, preserving the original. New readers retain all IDs,
   tracks, routes, clips, automation and missing-media/plugin placeholders; no
   partial load or truncated Save presented as success.

## Staged work and exit criteria

| Stage / dependencies | Measurable acceptance |
|---|---|
| M2 model/parser/mix scaling | Cross the present256 boundary with257,512,1024 and4096-track synthetic projects; create/edit/group/Undo/Redo/Save/reopen and all stable IDs/routes preserved. Count growth comes from admitted resources, not a replacement constant. Test refusal under a deliberately small budget before canonical mutation. |
| M2 media/GUI scaling | Large sparse projects and shared assets remain responsive under recorded memory/handle budgets; virtualized rows/meters and streaming media. Exercise repeated add/remove, missing media, autosave/recovery and safe old-version refusal. |
| M3/M4 graph/host/worker scheduling | Serial vs parallel/offline results meet existing timing/sample/quality contracts, including buses, sidechains, PDC, events and tails. Verify zero callback allocation/free/blocking locks, safe concurrent retirement and unchanged original state. |
| M4/M9 freeze/bounce support | Freeze/unfreeze plugin and audio graphs, grouped edits and portable assets with exact timing/latency/state restoration. Failure/cancel retains originals and does not silently remove tracks. |
| M2/M11 platform/workload qualification | Linux and Windows measured profiles: modest machine and stronger workstation; separate total/sounding/recording counts, effects/plugins, rate/quantum, RAM, IO and disk layout. Test sustained real-time workloads and overload/recovery against existing strict cycle gates. Publish measured capacities, not a universal track-count guarantee. |

The numbers above are regression workloads, **not product ceilings or current
performance claims**. Extend them as evidence and hardware allow. Owned synthetic
routes allow high project counts without purchasing a large physical interface;
physical multi-input claims still require independent qualified hardware tests.

The [first implementation](78-resource-admitted-projects.md)/ADR063 crosses the
old boundary at 257/512/1024/4096 in audio-track core workflows and opens/edits/adds
track 4097 in the actual Linux Qt fixture. It removes the coordinated model/parser/
mix/mask and Add Track ceilings under trusted byte policies, with original failures
and predecessor refusal retained. Counts are scoped workloads, not ceilings.
The [shared media checkpoint](79-shared-media-cache.md)/ADR064 adds exact file-backed
257/1,024-track mixes with one handle and 96 distinct assets with two handles.
The [viewport/list checkpoint](80-virtualized-session-views.md)/ADR065 replaces
per-row items with stable-ID models and interval queries. Next implement remaining
meters/waveforms, GUI paging, combined snapshot/history/graph resource accounting
and large recording/adoption budgets.
The recording owner still has 256-arm/packed-input assumptions; these do not set a
total project track ceiling, and their separate qualification remains required.
The current native manual checkpoint includes short actual desktop workflows
with3/32 armed inputs; this does not qualify larger projects or sustained workloads.
Full X006 remains an implementation task. The frozen92 contracts remain unchanged
and unpromoted. The initial planning checkpoint retained32 observations; the
[current native checkpoint](77-native-manual-panel.md) retains71, including an
unresolved active clock gap and source CPU outlier in a sanitizer run. See also the
[CI recording-fixture observation](../tests/results/repository/2026-10-07-track-scalability-ci-observation.json).

The [history checkpoint](81-history-resource-admission.md)/ADR066 replaces fixed
Undo policies with trusted configurable command/byte/workspace limits, usage and
observable retirement. Refusal preserves canonical/history/active gesture state.
This is one input to aggregate admission; saved/IO/snapshot/GUI/graph ownership,
allocator/RSS and sustained workload profiles remain separate open gates.

The [retained-session checkpoint](82-retained-session-resources.md)/ADR067 now
accounts for unique immutable controller blocks across saved revisions, IO saves,
barriers and externally retained readers. Publications are admitted before edits
and Undo/Redo; full-budget Cancel reuses the gesture's starting block. Canonical,
history, GUI indices/projections, graphs and IO buffers still require combined
admission and measured sustained workload envelopes.
