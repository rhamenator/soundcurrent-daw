# X006: track scalability for recording studios

Owner requirement, 2026-10-06 (America/Detroit). Planning; not implemented.

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
| Session validation | 256 tracks, 4096 assets (`src/session.cpp`) | Resource-budgeted object admission and efficient stable-ID lookup; remove the fixed project track ceiling |
| Project parsing | 256 tracks and master track routes (`src/project_store.cpp`) | Versioned large-project admission with checked byte/object/nesting limits and streaming or partitioned state as needed |
| Prepared mix | 256 track routes (`src/mix.cpp`) | Prepared dynamic route/processor arrays with checked aggregate memory/event/CPU bounds |
| Live replacement mask | Fixed 256-element track mask (`src/mix_playback.cpp`) | Allocate an admitted mask per prepared graph outside callbacks |
| Recording owner | 256 armed tracks/packed native inputs and bounded aggregate capture budgets | Scale arm descriptors independently from backend port capabilities; prepare capture against actual IO/reserve needs |
| Readers/UI/history | Bounded open asset references and snapshots | Shared bounded media cache, virtualized views and measured history/snapshot memory; avoid one open file/widget/copy per project track |

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

Next architecture implementation within M2: audit and replace track-indexed fixed
arrays and256 validators together under versioned admission. Do not merely raise
one constant, leave parser/UI/recording limits behind, or bypass real-time safety.
The current native manual checkpoint includes short actual desktop workflows
with3/32 armed inputs; this does not qualify larger projects or sustained workloads.
X006 remains an implementation task. The frozen92 contracts remain unchanged
and unpromoted. The initial planning checkpoint retained32 observations; the
[current native checkpoint](77-native-manual-panel.md) retains71, including an
unresolved active clock gap and source CPU outlier in a sanitizer run. See also the
[CI recording-fixture observation](../tests/results/repository/2026-10-07-track-scalability-ci-observation.json).
