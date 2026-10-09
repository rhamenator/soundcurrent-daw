# Staged backlog, dependencies and exit criteria

Status: early M1 implementation; session/state foundations exist, full feature workflows remain open. Windows and all-Europe localization now have [cross-milestone gates](08-platforms-and-localization.md). No fixed completion date or claim that combined parity is a small extension to the EQ is justified. This is a substantial multi-subsystem product; deliver useful milestones without shrinking its target.

Requirement IDs and milestone assignments live in [parity.json](../research/parity.json). The table below is the dependency graph and measurable gate for each stage. When a row contains several subfeatures, create child issues during M0 rather than treating the row title as a completed feature. Bugs and accepted gaps remain traceable to that row.

## Milestones

X007 [easy installation](88-easy-installation.md) now has explicit fresh-install,
upgrade, failure recovery, removal, localization and source-delivery workflows.
Begin its runtime/support inventory alongside native backend work; native Linux
packages and a graphical Windows installer are deliverables, not developer build
instructions. M11 qualifies the completed installation paths.

| Stage | Depends on | Implementation backlog | Exit criteria |
|---|---|---|---|
| **M0 — reference and contracts** | Planning artifacts | Expand each reference family into options/defaults/failures; licensed reference workflow corpus; release/submodule pins; session schema and RT budgets; compare framework alternatives against M1/M4 | Every matrix row has owned child tasks, verified source/version or explicit uncertainty, acceptance fixture and failure cases. Qt-free core builds. Exact license/source inventory approved for selected dependencies. No parity claim at this stage. |
| **M1 — one-track vertical slice** | M0 subset: core/state/backend/media decisions | SLICE-001 described separately: mono capture, monitor/playback, in-process parametric EQ, GUI/undo gesture, save/reopen, WAV export | [First-slice gates](05-first-slice.md) all pass; synthetic and native Linux capture evidence; no automatic output-route changes; no partial export published. This is a recorder foundation, not the final product scope. |
| **M2 — dependable multitrack recording/editing** | M1 | Armed audio tracks, monitoring, alignment, punch/loop/takes/comping, grouped fades/edits/versions; media pool/relink; autosave/journal/recovery/migrations/backups | 32-track synthetic ten-minute run and 30-minute native declared-device run with zero unreported frame gaps; punch/loop boundary tests within one sample; linked eight-mic edits preserve offsets; process-kill/disk-full recovery retains durable prefix; portable archive restore on fresh path. |
| **M3 — MIDI and automation** | M2 core; M1 RT/event interfaces | MIDI ports/record/edit/overdub/retrospective; expression/MPE; tempo/signature maps; read/write/touch/latch/cross-over/trim; controllers; SMF import/export | Two-voice expression and sustain/seek/loop tests pass; ramps and event offsets within one frame; all automation punch-out modes demonstrated against reference; controller reconnect preserves IDs and avoids feedback; loss-report SMF round trip. |
| **M4 — professional mixer and monitoring** | M2, M3; plugin-latency fixture API before M6 | Buses/sends/sidechains/VCA; external inserts/instruments; PDC/latency change and low-latency monitor mode; control room, cues/talkback; meter/monitor EQ paths | 257/2048-sample latency graph including sidechains nulls below -120 dBFS; synthetic external loop then declared hardware qualification; four cue mixes and talkback cannot enter master export; monitor correction controls leave export unchanged; loudness corpus passes. |
| **M5 — performance and hybrid session model** | M3, M4 | Launcher/scenes/quantization/follow actions; performance/master capture; hybrid tracks; containers; probability/operators and drum/melodic pattern editors | Mixed audio/MIDI scene performance replay with tempo/automation and deterministic seeds; arrangement/launcher ownership correct at every transition; sample-exact quantization; nested route/preset save round trip; master recording survives transport jumps. |
| **M6 — plugin lifecycle and isolation** | M4 contracts; can run alongside M5 | Direct VST3/CLAP/LV2; scan/cache/blocklist; state and editor lifecycle; bus/latency/tail negotiation; child IPC/PDC/restart; missing placeholders; legacy bridge feasibility | ≥10 native plugins per supported format covering effects/instruments, state, editor, sidechain and layouts, plus SDK fixtures; injected scan/process crash/hang leaves recording intact; reopened state/routes/expression preserved; X11 and Wayland UI integration tested. Legacy-format gaps explicit. |
| **M7 — synthesis, effects and modular environment** | M5, M6 | Modulators/voice scopes, patch editor and compiler; original instruments/sampling/granular/spectral/drums; effects/convolution/microtuning; browser and presets | Polyphonic modulation independent per voice; sample-accurate feedback scheduling fixtures; declared 64-voice patch load within admitted CPU budget; category-level Q-SYNTH/Q-DSP tests; 100k-item index benchmark; no rights gaps in included samples/presets. Device catalog parity audited beyond names. |
| **M8 — advanced audio, analysis and interchange** | M2, M3, M6, M7 required processor families | Stretch/pitch/formants/segmentation/alignment; offline history; stem separation; batch/stem export; formats/dither/resample; DAWproject | Q-STRETCH/Q-PITCH/Q-STEMS evaluation completed versus reference fixtures; original edits preserved; cancellable jobs never publish partial files; DAWproject semantic round trip and loss reports from both references; RF64/BWF/format boundaries tested. |
| **M9 — orchestral composition and notation** | M3, M6; M7 instruments | Articulation/direction/attribute maps, exclusion groups/attack offsets; chord/scale/logical editors/macros; separate score/performance model, parts/layout/engraving; MusicXML and Dorico feasibility | Orchestral articulation fixture survives seek/loop/export; performed timing distinct from score quantization; conductor/parts, polyphonic voices, percussion/tuplets/lyrics print without layout collisions in corpus; MusicXML semantic diff; native Dorico unknowns resolved or prominently recorded. |
| **M10 — immersive/video/synchronization** | M4, M6, M8; M9 timing integration | Named/discrete layouts; surround panning/downmix; Ambisonics1–4OA/headtracking/binaural; bed/object/ADM; video/export/timecode/MTC/LTC; network sync; AAF/OMF | Synthetic 256-channel impulse routing + real qualified multichannel interface later; Q-SPATIAL corpus; ADM validators and rights/platform gate before Dolby claims; hour-long rational/drop-frame AV sync ≤1 video frame; AAF/OMF exchange loss reports. No expensive hardware required for first synthetic tests, physical claims await qualification. |
| **M11 — content, compatibility and parity qualification** | M2–M10 complete | Original content breadth; beta/companion workflow evaluation; native format access research; accessibility/performance, release/backup/recovery/security qualification; Linux and Windows packages; all-Europe translation qualification | Every F/Q/C/N result has evidence on Linux and Windows, and unresolved items are explicit. Full functional parity may be claimed only when all required F workflows and detailed suboptions pass; Q/C/N stated independently. Fresh Ubuntu/Fedora/Windows install/upgrade/remove works as normal user, no hidden route changes or missing licenses. Each language has separate translation, native-review and UI evidence; no empty catalog counts as support. |

M11 is a **qualification gate**, not a bucket for difficult functional work. Companion/vocal-synthesis and native-compatibility investigation needs funded child tasks beginning in M0; rights issues must be addressed before investing in implementation. Scope change requires an explicit product decision and matrix delta. Lack of access or hardware is not a test pass.

## Quality gates (proposed SoundCurrent thresholds)

These are acceptance specifications, not claims about reference products' measured outputs. Record sample rate, signal level, block size, hardware, build flags, algorithm mode and tolerances in each result. Separate deterministic signal tests from listening evaluations. Neither bit-identical proprietary DSP nor identical presets is required for a functionally equivalent musical result.

| Gate | Measurable criteria and comparison workflow |
|---|---|
| **Q-EQ** | Flat unity null ≤-140 dBFS for prepared float path; gain response ±0.05 dB across declared band/rate range; block partition difference ≤1e-7; no forced clipping of internal overs; coefficient changes use declared smoothing/crossfade with no unrelated state reset. Existing probe measures only a subset. |
| **Q-DSP** | Dynamics ratios/attack/release verified against specified curves; latency and finite tails exact; silence/NaN/denormal handling bounded; oversampling/aliasing specified per processor; blind level-matched reference listening on ≥20 rights-cleared excerpts before quality equivalence claims. Publish any inferior operating modes. |
| **Q-HOST** | Formats satisfy SDK thread/lifecycle tests; sample events/latency/layouts/state exact; crash/hang recovery bounded; no hidden buffer extra latency; deterministic fixtures null below -120 dBFS after alignment. Real plugins may be nondeterministic; compare statistical/state behavior instead. |
| **Q-STRETCH** | Beat/transient drift <1 ms in known markers; declared ratios0.5–2 on drum/vocal/mix corpus; no new clipping; ≥20 level-matched excerpts with blinded ABX/quality ratings. Failure of a material/mode remains a recorded gap; no algorithm-name equivalence. |
| **Q-PITCH** | Clean monophonic target within5 cents; formant independent edit verified; segmentation preserves original; blinded artifacts/naturalness evaluation across vocal/instrument corpus and large shifts; latency/mode disclosed. |
| **Q-SYNTH** | Voice-steal/release/sustain correctness; MIDI/MPE tuning tests; supported Nyquist-safe oscillator modes and alias/noise spectrum limits specified by device; 64-voice fixture performance declared; original sound-family coverage audited. |
| **Q-LOUDNESS** | Published EBU/BS.1770 test material agrees within0.1 LU and0.1 dBTP for supported layouts; gating/channel weights verified; meter state/reset/window duration explicit. Use current standard edition verified at implementation time. |
| **Q-RESAMPLE** | Ratio/rate fixtures retain exact intended duration; passband ripple ≤0.01 dB and stopband rejection ≥100 dB for high-quality mode; delay reported/compensated; seeded dither statistical tests; separate low-latency mode documented. |
| **Q-STEMS** | ≥30 rights-cleared multitrack mixes; compare separation SDR/SI-SDR with exact metric definitions, target leakage, transients and blinded listening versus frozen reference. Median SI-SDR no worse than reference by >1 dB as initial gate; publish failures. Runtime license does not establish model rights or quality. |
| **Q-SPATIAL** | Channel/normalization/order conversion and spherical-harmonic known-field tests; energy/panning/downmix verified; rotation orthogonality and orientation fixtures; binaural listening corpus/HRTF rights; device/headtracking latency measured separately. |
| **C-CONTENT** | Reference category inventory expanded to musical use cases and parameter ranges; original preset/sample/demo coverage per family; rights manifest complete; ≥3 complete production workflows using bundled-only content. Coverage is recorded independently of exact vendor library counts/GB. |
| **N-NATIVE** | Corpus from legally accessible native files; objects/automation/media/plugin state and rendered results diffed; explicit unsupported fields, version bounds and directional support; no full compatibility claim from a lossy exchange conversion. |

## Engineering release gates

- RT: allocation/free/lock/syscall instrumentation for host-owned callbacks, prefaulted buffers, worst-case event/graph swaps and queue overloads. Report worst case and99.9th percentile on declared hardware, not just average CPU. Target callback99.9% below60% of quantum and observed maximum below80% under declared workload; stress duration ≥30 minutes. A passing finite run is not a universal scheduling guarantee.
- Durability: process-kill and worker-kill tests at save/record boundaries, disk-full/device-removal/plugin-state failures, no-overwrite exports, hash-verified archive restore, schema corpus migration and bounded malformed-input tests.
- UI: keyboard access/focus/labels, undo and lock-safe wheel behavior, window fit1280x720 and HiDPI/multiple displays, no repaint-induced xruns. FFT display refresh is independent of audio-buffer frequency.
- Distribution: clean Ubuntu/Fedora/RHEL-family feasibility, desktop launcher, documented dependencies and normal-user setup. Release package contents/SPDX/license notices checked. No automated root or audio-server replacement.

## Equipment profiles

The owner added **X005 expanded equipment profiling and an editable profile library**, including microphone/speaker/amplifier curves, brand/family/model browsing, import and dirty-profile save prompts. [The contract](17-equipment-profiles.md) separates schema/editor/catalog work from M4 monitoring routing and explicit correction printing. A pinned equalizer snapshot now provides the offline Qt library/editor and 1,087 generated speaker corrections; engine profile state/preparation, control-room routing/printing, portability and full qualification remain open. No ongoing equalizer checkout is modified here. Linux/Windows and localization/portability gates apply.

## Work sequencing and next issue

The owner has now activated the [full implementation goal](../GOAL.md). Continue **SLICE-001**: S1/S2 state, S3 prepared processing, S4 capture/journal and S5 owned PipeWire capture/monitor foundations exist; S6a bounded read-ahead/take playback, S6b immediate EQ controls, S6c asynchronous Qt project editing and S6d production native playback ownership and S6e Linux desktop playback/live EQ/meters now exist; S6f native recording ownership/verified take admission and S6g first-track input/arm/Record/monitor/manual recovery now exist. S7 transactional offline export is next; remaining routing/discovery/overdub/platform gates remain open. Native S5 hardware/reprepare/Windows and default module-unload memory qualification remain open. During M0 resolve reference access and dependency pins; during M1 prove state/capture/processing/export together. High-risk tracks (host IPC, pitch/stem quality, notation, branded immersive delivery, native projects) get explicit feasibility experiments before broad subsystem development. X004 expands other-suite native import and requires source-version/corpus investigation now; later M8/M11 deliver and qualify full conversions. Estimates follow evidence. No planning-only stop instruction limits the active implementation goal.

S6f production recording ownership and typed verified raw take attachment now exist; [contract](21-native-recording-owner.md). Next implement input/arm/Record/monitor/recovery GUI and finalization/attachment before the close barrier, then S7 transactional export. No native Windows/hardware/deadline or full first-slice completion is inferred.

S6g desktop first-track recording and manual recovery now connect the production owner to explicit input/arm/Record/monitor controls and canonical verified attachment. [Contract](22-desktop-recording.md). S7 private shared-engine WAV export is next; S8 and all remaining M1/M2/native/platform gates remain open.

S7a adds the shared-engine offline export core and CLI with selected-range preroll, bounded tails, float headroom, cancellation and completed-file publication; [contract](23-offline-export.md). Next S7b connects a separate desktop job owner and Export dialog with canonical snapshot/revision, overwrite consent, progress/cancel and safe close. S7/S8 and all native Windows/physical/filesystem/route gates remain open.

S7b now connects the [desktop snapshot export workflow](24-desktop-export.md), including accepted-prefix capture, independent rendering, consent, visible progress/cancel and joined close. S8 must consolidate native recording/save/reopen/export/portability and routing acceptance; unresolved M1 hardware, Windows, filesystem/power-loss and native unload-memory gates remain open. Then proceed to M2 dependable multitrack foundations without shrinking the frozen full-product scope.


S8a now adds [portable per-channel routing intent and migration](26-project-routing.md), shared route/EQ undo and named endpoint placeholders. Consolidated native owned-node recording/live EQ/undo/save/relocation/reopen/static WAV export has sample-exact evidence, alongside debug/sanitizer and headless Windows builds. Remaining physical alignment, native Windows, deadline/module-unload, filesystem/recovery and platform gates keep M1/SLICE-001 incomplete. Next S8b resolves remaining acceptance/recovery work before M2 multitrack workflows; no frozen parity row is declared complete.

S8b persists Off/Post-EQ monitoring in schema 1.2, with shared semantic Undo/Redo,
explicit old-schema defaults and passive restore. Recording preparation uses an
accepted-prefix receipt after pending edits. [Contract](27-monitoring-preferences.md).
Next: bounded project recording-recovery discovery, then M2 multitrack foundations;
remaining M1/native/platform gates stay visible and required.

S8c now connects bounded passive discovery, per-job activity leases, scrollable
review and verified consent/copy on a separate I/O owner. [Contract](28-recording-discovery.md).
Continue M2 multitrack editing/selection/timeline, prepared shared-clock playback
and simultaneous capture while independent M1 platform/physical/durability gates
remain open; the frozen product target is unchanged.


M2a introduces [grouped stable-ID track/clip edits](29-multitrack-edits.md), scoped
count/byte-bounded mixed history and verified take-admission Undo. Developer CLI
persistence is available; next implement desktop selection/timeline, shared-clock
prepared graph and simultaneous overdub. Full M2 and independent M1 gates remain
open, with no frozen parity completion inferred.


M2b now exposes [desktop track/clip selection and timeline edits](30-desktop-timeline.md),
selected-track EQ/routes and immutable selected single-track preparation. Selection
never changes canonical order or retargets an owner; verified takes attach to the
captured ID. M2c next prepares one shared-clock multitrack live/offline graph, then
simultaneous capture/overdub. Full M2 punch/loop/takes/comping/fades, remaining M1
native/platform/fault gates and all frozen parity requirements remain required.


M2c1 now provides [shared-clock multitrack EQ/matrix playback and WAV export](31-multitrack-mix.md),
bounded fair read-ahead, explicit resource/layout admission and independent
32-track source-coordinate/mix oracles. The short corpus does not satisfy M2's
ten-minute synthetic or 30-minute native gate. Next integrate its prepared run
into the native owner/desktop, then simultaneous capture/overdub on one callback.
General buses/sends/sidechains/PDC, punch/loop/takes/comping/fades and full frozen
parity remain required, together with independent Windows/localization/M1 gates.

M2c2 connects [native/desktop shared-clock mix playback](32-native-mix-playback.md), explicit matching-layout preparation, output anchoring and all-lane revision acknowledgement. Next persist/edit the dedicated master matrix/output intent and implement one shared playback/capture callback for simultaneous armed tracks and overdub. No full M2 duration/capture/deadline or frozen-reference completion follows from short owned-node playback runs.

M2c3 persists [master layout/matrix/output intent](33-master-matrix.md), connects its editor, routing/history and shared playback/offline export. Next implement one native playback/capture callback for simultaneous armed tracks and overdub; full M2/reference/platform/duration gates remain open.

M2c4 adds [retained callback faults and bounded native timing evidence](34-native-timing.md). Preserve the unexplained completion/sink-gap and intermittent concurrent UI failure gates. Next implement one native playback/capture owner with explicit armed channel maps, a shared origin, raw bounded per-track pipes, off-RT writers and independent alignment/gap/finalization/recovery acceptance. Optimized declared-load/deadline, native Windows and full M2 remain required.

M2d1 provides the [backend-free shared playback/capture bridge](35-shared-playback-capture.md), explicit raw channel maps, Off/Post-EQ live replacements and per-track clock/prefix/failure retention. Short 32-track exact recording and mono/stereo recovery evidence do not satisfy M2 duration/native/platform gates. Next implement production native duplex ownership with upfront combined memory/spec admission, activation rollback, callback-before-writer join and independent results/errors; then desktop grouped armed-track/take handoff. Punch/loop/Auto/takes/comping and all other M2/frozen workflows remain required.


M2d2 connects [production PipeWire duplex ownership](36-duplex-recording-owner.md):
preflight before pool allocation, inactive explicit input/master routes,
activation-only writers, callback-before-disk joins and independent receipts/
errors/recovery. Next implement desktop accepted-prefix multi-arm/input maps and
grouped verified-take admission/history. Full M2 duration/load/physical/fault and
Windows gates remain required; no frozen family acceptance is marked complete.


M2d3 connects [desktop multi-arm recording and grouped verification](37-desktop-duplex-recording.md): accepted-prefix full-project preparation, immutable arms/range, explicit packed input/master routes, per-lane applied-revision receipts, independently retained writer failures and atomic verified group admission with one Undo/Redo and active Save/close ordering. Short three/32-arm owned-native GUI workflows have independent raw/output oracles. Next **M2d4**: implement the reproducible 32-track ten-minute synthetic P001 acceptance (timestamps, source/hash, stop/save/reopen, bounded failure/cancel evidence), then the separate 30-minute declared native device/load/alignment gate. Punch/loop/Auto/takes/comping and every remaining M2/full-product workflow remain required. No frozen F/Q/C/N status is promoted.

M2d4a supplies [the ten-minute 32-track synthetic duration workflow](38-recording-duration.md):
921.6 million exact raw samples, timing/alignment/hash/Save-reopen verification,
independent float output checks and bounded cancel/write-fault/pool-overflow/
owned-process-kill recovery. Default unity EQ/private device pacing are declared;
producer lateness is visible. Next **M2d4b**: extend the owned native source/duplex/
sink fixture for a declared 30-minute rate/quantum/device/load run with streaming
full-range source/output verification, complete callback timing coverage and
99.9th-percentile/max gates. Physical alignment, actual disk-full/filesystem/
power-loss/Windows and every remaining M1/M2/frozen workflow remain required.

M2d4b supplies [active native-link admission and bounded full-duration diagnostics](39-native-duration-qualification.md),
short native/desktop regression evidence and read-only failed-prefix verification.
The first declared30-minute run failed with capture-pool exhaustion around55s;
an instrumented120s sample run is exact but misses the maximum callback budget.
Next **M2d4c**: diagnose bounded disk worker phases/queue occupancy and callback
tails, qualify any justified checkpoint/burst-buffer change under the unchanged
32-track contract, then repeat the required30-minute native acceptance. Short
sample/timing passes do not waive earlier observations or promote F/Q/C/N axes.

M2d4c1 introduces [bounded disk-owner phase and queue observations](40-writer-backlog-diagnostics.md)
without new audio counters/clocks. A declared lane17 journal stall tests initiating
fault retention and exact full raw/common-output prefixes under the existing
32-track pool. This is evidence for the next measured storage-burst/worker-policy
decision, followed by the unchanged30-minute native sample/deadline acceptance;
no shorter run or injected reproduction resolves the original uninstrumented
failure or promotes frozen functional/quality/content/compatibility contracts.

M2d4c2 adds [admitted capture reserve and first checkpoint phases](41-checkpoint-burst-policy.md)
([ADR-031](decisions/031-admitted-capture-reserve-and-checkpoint-phases.md)). Capture
admission rounds whole independent pool slots up to the requested duration within
fixed token/per-pipe/aggregate bounds; playback blocks and queues stay separate.
Prepare captures the desktop choice, and only armed pools are admitted. Initial
checkpoint phases disperse nominal disk calls without increasing regular frame
spacing. Block quantization, scheduling and backlog limit phase separation; stalled
durability has no hard wall-time guarantee. Scoped native stall absorption/exhaustion,32-track copy recovery and120-second
full sample/timing gates now pass. Required30-minute native timing/throughput and
every independent reliability/platform/physical/product gap remain open.

M2d4c3 adds [supplemental native fixture CPU timing](42-native-callback-cpu-diagnostics.md)
after the post-reserve1800-second attempt stops around196seconds on a skipped sink
cycle. Diskqueues max1 and all retained raw/common-output samples are exact; owner
max wall20.768787ms fails the existingperiod budget. Optional thread CPU clocks and
fixed coverage/maxima supplement, never replace, full elapsed/current-period gates.
Full sink/max-clock flags are printed after joins. Production remains unchanged;
short CPU coverage does not establish original cause or long native qualification.


M2d4c7 adds [separate test-only processing stage intervals](46-native-processing-stage-diagnostics.md)
after a further unchanged long native miss. The exact full raw/common-output prefix
is retained and verified read-only; eighteen historical observations remain. GNU
ABI wrappers measure raw capture, EQ-driver and inclusive mix within the same
bridge callback without modifying production libraries. Actual wrapper-count/
bit-identical-output/RT and120s native sample/timing evidence qualify this scoped
diagnostic. Tails vary by stage; no single long-run cause is established. Next
obtain a matching stage snapshot at a long-run miss, then select a justified change
and repeat the unchanged full gate. Remaining M2 punch/loop/takes/comping, Windows
and every frozen full-suite requirement remain required and unpromoted.


M2d4c8 retains [paired individual call intervals/ordinals](48-individual-native-processing-calls.md)
in the separate diagnostic's selected callback snapshots, after a matching long
miss shows costly raw and EQ intervals plus a slow source on the same cycle.
All687,079,424raw/42,936,320commonoutput samples are verified read-only, originals
preserved, nineteen historical observations retained. Existing query pairs and
all production libraries remain unchanged; short sample/RT/timing and explicit
pairing/ranking/unknown tests qualify only the helper. Next matching long-run
individual-call evidence precedes a justified processing/placement experiment;
sustained native/physical/Windows and all remaining M2/full-suite gates stay open.

The sustained individual-call diagnostic now retains a twentieth failure with
matching raw/EQ intervals, exact media and unchanged originals; see
[the retained facts](48-individual-native-processing-calls.md). Its deadline gate
remains open. An independent [desktop-close fix](50-desktop-close-error-baseline.md)
qualifies historical-error handling and new Save failure/retry in Linux UI tests;
it does not resolve an unknown earlier close timeout or native audio performance.

Next M2 implementation: prepared punch ranges and sample-exact boundary splitting
for monotonic duplex capture. Verify partition-independent start/end, overdub
timeline alignment, raw-media preservation and failure/recovery before connecting
UI and native routes. Loop recording, take lanes, comping and the full M2 remain
required. Continue sustained native diagnosis independently without waiving its
current full-sample, elapsed/current-period or durability gates.

M2 now has a [prepared raw-frame punch foundation](51-punch-capture-foundation.md).
Boundary/partition, raw/aliased monitor, origin/alignment, Save/reopen/undo and
interrupted durable-prefix fixtures pass in Debug and optimized builds. The full
sanitized run retains a timeline selection/preparation failure (28/29 pass),
without assuming its cause. Windows core compile is not runtime qualification.
Next fix/reproduce pre-poll selection admission, then prepare latency-aware
musical locators and add persistent UI/native punch workflows. Full punch,
Auto monitoring, loop/take lanes/comping and the sustained native gate remain open.

The deterministic [pre-poll selection defect](52-published-track-selection.md) is
fixed with a single canonical snapshot for timeline/inspector redraw. Three
Debug desktop groups, Release and full29-group sanitizers pass. The previous
unidentified admission failure is retained independently; all21observations stay.
Next serial owned native default-recording regression, then latency-aware musical
locator preparation/persistence and UI/native punch workflows.

The serial plain20-second [native default-recording regression](../tests/results/M2/2026-10-06-punch-default-native-regression.json)
now passes exact raw/output, origin/alignment/Save-reopen and complete finite
timing gates after the punch addition. It selects no punch range and establishes
no native punch, physical, sustained or Windows qualification. Next prepared
per-lane musical locator windows must include declared input latency, bounded
postroll and independent origin/attachment/fault evidence before persistent UI
and native punch workflows. All21observations and92frozencontracts remain.
The [signing budget](49-windows-signing-budget.md) review now records committed
equalizer packaging changes while keeping direct user-mode Windows development
independent of a purchased certificate.

M2 now prepares [latency-aware project-frame punch locators](53-latency-aware-punch-locators.md)
with separate per-track raw windows/origins and bounded postroll.28new delayed-
signal workflows plus25original boundary workflows, full29Debug/29SAN groups and
Windows core builds pass; ordinary/default20s native regression remains exact and
within unchanged finite timing gates. This does not qualify native/UI punch,
physical latency, Windows runtime or sustained performance. Next persist desired
locators in versioned project state, add recording controls/admitted postroll and
owned native per-lane punch evidence. Full manual/Auto/tempo/loop/take/comping and
all frozen product requirements remain, with21historical observations retained.

M2 now persists [canonical project punch settings](54-project-punch-controls.md)
in schema1.4 with strict older-schema migrations and undo/redo, and connects
range/toggle/actual-postroll controls to prepared recording and grouped attachment.
Linux codec/controller/desktop tests qualify this bounded workflow. No native
punch, physical or Windows runtime gate is promoted. Next owned native per-lane
punch acceptance precedes manual/Auto/tempo/loop/take/comping. All21historical
observations,92frozencontracts and the full professional-suite scope remain.

The short [owned native punch workflow](55-native-punch-qualification.md) now
verifies32 differently delayed takes, exact boundaries/origins/timeline alignment,
full preroll/postroll monitoring and grouped undo/Save-reopen. Complete callback
elapsed/CPU/resource/cycle coverage passes unchanged finite gates. This does not
close native desktop integration, fault/interrupted punch, physical/sustained/
non-flat/Windows gates or full reference record modes. Next canonical native
desktop punch evidence precedes manual/Auto/tempo/loop/take/comping. All21prior
observations and92frozencontracts remain unpromoted.


## M2 native desktop punch checkpoint (2026-10-06)

[Production desktop/native punch](56-native-desktop-punch.md) now has short owned
32-track evidence: canonical locators/Prepare/Record/Stop, alternating monitoring,
live EQ applied-frame receipts, exact raw/full-output samples, grouped attachment
Undo/Redo and Save/reopen. The original control visibility failure and prior 21
observations remain retained. Complete finite native timing gates pass; all 92
frozen contracts remain unchanged and unpromoted. Next: native punch interruption
and route/disk-fault recovery, then input-latency controls, manual/Auto, tempo/loop,
take lanes and comping. Sustained, physical and Windows gates remain open.


## M2 native punch interruption checkpoint (2026-10-06)

[Native punch fault recovery](59-native-punch-fault-recovery.md) now verifies 32
delayed raw prefixes and new recovery copies under cancel, owned source/sink loss
and one disk-boundary exception. Grouped Undo/Redo, Save/reopen, retained initiating
error, complete short native timing and preservation of two existing external links
pass. Optimized and sanitizer synthetic counterparts qualify the oracle. These are
engine workflows; desktop fault/discovery, process-kill, empty-preroll, real disk-full,
sustained/physical/Windows and full reference modes remain open. Prior22 observations
and one initial fixture assertion are retained; all92 frozen contracts stay unchanged.
Next product implementation: saved input-latency controls/prepared-session behavior,
then manual/Auto/tempo/loop/take/comping, with remaining fault gates tracked separately.


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


Prepared recording-only Auto monitoring now selects live input at exact desired
punch boundaries, independently of delayed raw capture, before continuous track EQ.
[Contract](61-auto-recording-monitoring.md). Next: sample-boundary manual punch while
playback continues, then armed/stopped/tape monitoring and loop/take/comp workflows.
The full frozen scope, independent Windows and all-Europe gates remain required.


## M2 manual-punch capture prerequisite (2026-10-06)

[Deferred capture starts](62-deferred-capture-start.md) reserve pools before audio
activation and publish an immutable exact one-shot raw start. Late disk workers
bind to that start; unresolved and mismatched specifications are refused before
job creation. This supplies the capture primitive for manual punch during playback.
Next implement reliable sample-boundary command admission, ready take-slot credits,
per-lane delayed capture/postroll, continuous mix/EQ and safe writer/group retirement.
The current fixed-start native regression does not qualify a deferred native owner.
Repeated takes, desktop controls, ordered Stop/fault recovery and all frozen
reference, sustained, Windows and localization gates remain required.


## M2 runtime manual-punch engine checkpoint (2026-10-06)

[Manual-punch engine owner](63-manual-punch-engine-owner.md) now keeps playback and
EQ running across replenishable take slots with reliable bounded FIFO replies,
separate per-lane delayed raw windows and safe release-published retirement.
Synthetic real-reader/writer workflows verify exact repeated windows/output,
grouped Undo/Redo/Save-reopen, delayed/completed-before-worker startup, nine fault
prefix recoveries, exact end-boundary receipts, empty delayed stop and twenty
concurrent control/audio slot replacements. This extends P004 implementation
evidence without promoting its frozen F/Q/C/N requirements.

Next build the production off-audio recording control owner: start/join disk
workers, retain initiating errors, validate grouped results, enforce empty-take
handling and preserve pending/retired lifetime through Stop/fault and cancellation.
Then integrate and qualify the owned PipeWire adapter and canonical desktop
manual controls. Native/sustained/physical, independent Windows, broader monitoring,
loop/take/comping, imports/profiles and all-Europe/full-suite gates remain required.

## M2 manual recording disk/group ownership (2026-10-06)

[Production control owner](64-manual-recording-control-owner.md) now starts writers
from published exact capture configs, joins/reclaims retired consumers, retains
initiating errors, distinguishes empty/mixed/failed/canceled lanes, verifies
independent media/checkpoint results and applies bounded reliable reply/result
backpressure. Canonical grouped adoption/Undo/Redo preserves the active continuous
mix/EQ generation. Real reader/writer synthetic cases cover late startup,
interruptions, queue pressure and twenty concurrent audio/control retirements.

Next implement and qualify the owned PipeWire manual recording adapter, then
canonical Qt manual controls and their fast-edit/Undo binding checks. All29 retained
observations and92 frozen contracts remain retained/unpromoted; native sustained,
physical, Windows runtime/installers, complete monitoring, indefinite/loop/seek,
takes/comping, X004/X005 and all-Europe qualification remain required.

## M2 native manual recording checkpoint (2026-10-07 UTC)

[Native owner](65-native-manual-recording.md) keeps one graph running through
three replenished manual windows with32 arms, mixed monitor modes and four delays.
Early and completed-before-service synthetic/native cases verify96 raw recordings,
1,557,696 raw samples and960,000 output samples, exact origins/journals, six reliable
replies, grouped Undo/Redo and Save/reopen. Both finite native runs pass full
callback wall/CPU/resource/current-cycle gates and preserve external routes/defaults.
Default export header repeatability is corrected and tested across seconds for WAV
and RF64; original failures/replays remain separate.

Next qualify this adapter's Stop/cancel/late-cancel, input/output loss, active/retired
disk errors and independently recoverable raw prefixes, then integrate the bounded
Qt manual-control owner and fast monitor edit/Undo regression. Static-adapter and
engine fault tests do not establish new native fault scope. All31 historical
observations and92 unpromoted frozen contracts remain; full sustained/physical,
Windows runtime/installers, X004/X005 and all-Europe translation/review/UI are open.

## X006 studio track scalability (owner requirement2026-10-06)

Target lower-budget studios, including studios with substantial hardware.
[Track scalability](67-track-scalability.md)/ADR053 require total track counts to
grow under explicit resource admission, without a fixed product/license ceiling.
Physical IO channels and real-time graph throughput are separately qualified.

M2 must replace256 track validation/parser/mix/replacement-mask assumptions
together, with larger-project persistence, bounded shared media resources and
virtualized UI/history. M3/M4 qualify prepared multicore/plugin graphs; M4/M9
deliver freeze/unfreeze/bounce without losing originals. M11 verifies measured
modest/strong Linux and Windows workloads, sustained deadlines and overload/recovery.
257/512/1024/4096 synthetic projects are acceptance workloads, not product caps or
existing performance claims. Current native manual fault/recovery remains next;
no frozen-reference acceptance/quality/status changes or scope reductions.

## M2 actual manual desktop checkpoint (2026-10-07)

[Manual recording panel](76-manual-recording-panel.md)/ADR061 now connect the
canonical barrier and full-project EQ following to repeated takes, explicit routes,
Punch In/Out, priority Stop/Cancel, group preview/adoption/retry/Keep and joined Close.
Actual-widget acceptance uses a synthetic audio owner and real media/history/store;
Debug40/40 and affected ASan/UBSan/LSan3/3 pass. Windows controller/media cross-build
passes; the new desktop workflow has no native Windows qualification.

Next qualify this exact Qt/controller workflow on owned PipeWire routes, including
port loss and late finalization, then independently on Windows. Current finite
transport, capture admission,256 project-track limit and full frozen backlog remain
explicit. All48 historical native observations and92 unpromoted frozen contracts
remain; synthetic widget success does not resolve sustained/physical qualification.

## M2 native manual desktop checkpoint (2026-10-07)

[Owned native desktop qualification](77-native-manual-panel.md)/ADR062 adds the
actual Qt/controller fixture and fixes legitimate neutral-buffer rejection using
one prepared read-only zero plane. Five short release workflows pass, including
32-input repeated takes with exact raw/output oracles. Original observations49..71
remain retained; the additional native sanitizer run fails on an active clock skip.
Debug40/40 and three affected sanitizer tests pass. No full native sanitizer,
sustained/physical/Windows or frozen parity promotion follows.

Next implement X006 coordinated resource-admitted model/parser/mix and media/UI
scaling, preserving the current256-track gap until acceptance. Diagnose original71's
active clock skip separately and continue sustained/native Windows recording
qualification and every remaining full-suite milestone.

## M2 X006 resource-admitted project checkpoint (2026-10-07)

[First large-project implementation](78-resource-admitted-projects.md)/ADR063
removes fixed total-track validation/parser/mix/replacement-mask and desktop Add
Track ceilings. Trusted byte policies, checked charges, immutable preparation
indices and schema 1.7 preserve explicit refusal and stable IDs. Core fixtures
cover 257/512/1024/4096 audio tracks and actual Linux Qt controls add track 4097.
These are bounded synthetic state/processing/GUI workflows, not sustained native
recording or a universal real-time capacity claim.

Next implement shared bounded media handles/cache/read-ahead and virtualized
views/meters, then combined snapshot/history/old-new graph envelopes, configurable
desktop resource controls and large recording/adoption workflows. Recording
256-arm/channel assumptions, structural 64-operation batch policy and history
256-command/32 MiB payload policy remain separate work. Freeze/bounce, richer
serial/parallel graphs and Linux/Windows sustained and physical profiles retain
their staged dependencies. No F/Q/C/N contract is promoted, and original native
sanitizer observation 71's clock gap/source CPU cause remains unresolved.


## M2 X006 shared media checkpoint (2026-10-07)

[Shared media cache](79-shared-media-cache.md)/ADR064 uses one serialized asset
registry, bounded handle pool and decoded source-frame pages for all track readers.
Live preparation and offline rendering share it; callbacks still consume bounded
slabs. File-backed257/1,024-track exact-output workflows share one handle and
96 distinct assets stay within two. This is partial X006 media implementation,
not sustained/native capacity or completion of the media/GUI stage.

Next virtualize track/timeline/meter views, measure combined state/history/old-new
graph envelopes and expose trusted desktop resource controls. Continue large
recording/adoption, freeze/bounce, scheduling and sustained Linux/Windows profiles.
All92 frozen contracts and independent Windows, X004/X005/Europe remain required;
original native observation71 clock gap/source CPU diagnosis is still unresolved.


## M2 X006 viewport/list checkpoint (2026-10-07)

[Viewport/list design](80-virtualized-session-views.md)/ADR065 replaces retained
track/clip items with snapshot-backed selectors, checkable arm rows and visible
interval painting. Scoped offscreen Linux actual-widget regression workflows
exercise8192 tracks and10000 sparse clips. Combined memory/resource admission,
remaining meters/waveforms, Qt row paging, accessibility and native Windows UI
remain open. Preserve all92 unpromoted contracts and unresolved original failures.
Next measure/admit combined GUI/history/state/old-new graph envelopes and expose
trusted desktop policies; then large recording/adoption, freeze/bounce, scheduling
and sustained Linux/Windows qualification. Full X006 and frozen scope remain.

### X006 configurable history checkpoint

[ADR066](decisions/066-configurable-history-resources.md) and
[workflow/admission scope](81-history-resource-admission.md) specify configurable
Undo retention and declared state/candidate work admission. Scope includes stable
IDs, reductions/refusal/retry, unrelated active gestures and real desktop settings.
Next implement aggregate snapshot/GUI/old-new graph admission and larger recording
arms/adoption. This does not promote the frozen parity contracts or establish
sustained native/Windows/localization qualification.

### X006 retained immutable Session checkpoint

[ADR067](decisions/067-retained-session-resources.md) and
[ownership/admission scope](82-retained-session-resources.md) introduce leased
Session blocks, shared Save/barrier ownership and pre-admitted publications.
Qualify external-reader survival/release, unknown-size Open, active-gesture
refusal/Cancel and staged Undo/Redo. Next coordinate canonical/history and GUI
projection/index leases with graph old/new/tail and IO budgets; expose a combined
resource editor, measure RSS and sustained workloads, and continue larger capture,
freeze/bounce and prepared scheduling. This snapshot budget is one component;
full X006 and every frozen-reference contract remain required.

### X006 shared controller budget checkpoint

[ADR068](decisions/068-controller-memory-resources.md) and
[scope/workflows](83-controller-memory-resources.md) coordinate canonical,
history/active, unique snapshots and declared operation credit under one parent.
The desktop exposes persistent atomic parent/snapshot limits. Next lease actual
GUI projections/indices before graph old/new/tail, cache and IO overlap; measure
allocator/RSS and sustained Linux/Windows workloads, then continue larger capture,
freeze/bounce and scheduling. Full X006 and all frozen contracts remain open.

### X006 shared GUI payload checkpoint

[ADR069](decisions/069-gui-memory-resources.md) and
[scope/workflows](84-gui-memory-resources.md) lease selected-track copies,
list/decorations and timeline/query arrays from the project parent. Stage old/new
display ownership, refuse atomically, retain the previous view and offer resource
settings/explicit retry. Next admit prepared graph/media-cache overlap and safe
retirement, then parser/IO and measured heap/RSS; continue paging, larger capture,
freeze/bounce, prepared scheduling and sustained Linux/Windows workloads.
Full X006 and all frozen-reference contracts remain open.

### X006 shared prepared execution checkpoint

[ADR070](decisions/070-shared-execution-memory.md) and
[ownership/acceptance scope](85-graph-memory-resources.md) connect prepared graphs,
playback pools, readers, shared cache pages/registry and offline output buffers to
the project parent. Nested aggregate ownership is counted once; retirement waits
for control collection after the last audio borrow. Desktop controls and offline
workers receive the same trusted parent. Next coordinate capture/writer/other IO,
measure allocator/RSS, implement paging and larger capture/adoption, then
freeze/bounce, prepared scheduling and sustained Linux/Windows workload profiles.
No native Windows or full X006/frozen-contract promotion.

### X006 coordinated execution and recording ownership checkpoint

[ADR071](decisions/071-coordinated-execution-policy.md) and
[86](86-execution-memory-policy.md) make new desktop playback/fixed/manual/export
preparation follow trusted parent policy. Recording envelopes use prepared usage,
not configured allowance. Capture pools, bridge bindings, aggregate manual banks,
writer/hash/journal workspace and monitoring-off scratch now have declared leases.
Next account for copied owner Sessions and remaining IO/transient work, measure
allocation/RSS, scale arm descriptors independently of native ports, then implement
freeze/bounce and prepared scheduling with sustained Linux/Windows qualification.
Full X006 and all frozen-reference contracts remain open.

## Native Windows preview foundation (2026-10-08)

Original WASAPI prepared packet/SDK ownership code now executes in an independent
Windows26300.9550 VM. Native recording/recovery255 checks and export1,062 checks
pass, alongside affected Linux Debug and sanitizer gates. An interactive-session
48 kHz VM speaker loopback completes raw capture/save/reopen/non-flat WAV export;
an independent recurrence checks the actual nonzero samples. Earlier all-zero
SSH results and a453-frame resampling-clock fault are retained and excluded from
signal acceptance. A native-rate44.1 kHz input run preserves genuine silence.
[Checkpoint and exact limits](96-windows-capture-foundation.md): Windows GUI,
monitoring/playback, rate-conversion timing, installation, sustained/physical and
all full-parity/language gates remain open. Next connect the Windows device owner
and output renderer to existing Qt controls and qualify an installable workflow.

## X004 persistence increment (2026-10-08)

The read-only outline now has a bounded versioned exact-source/protocol bundle
and off-thread desktop Save/Open. Linux synthetic tests are recorded separately
from native writer/corpus or conversion qualification. See checkpoint 116 and
ADR083. Next: rights-cleared pinned-writer corpus and a first track/clip import
IR with explicit per-property loss, opaque state and preview before conversion.
Complete Windows Qt, filesystem breadth, crash/storage faults and installed
bundle workflows alongside it. The frozen parity milestones remain unchanged.

## X004 approved-media dependency (2026-10-09)

Checkpoint122 implements the admitted root/file primitive before GUI media lookup.
Next dependency chain: validated source references + explicit roots/replacements →
pinned audio metadata/content check → cancellable desktop missing-media workflow →
verified copies into a new transactional destination → extended timing/gain/fade/
rate/pitch model and loss preview → Undo/reopen/independent aligned render gates.
Native Windows/installed evidence and broader format adapters remain required.

## X004 pinned audio validation increment (2026-10-09)

Checkpoint123 implements bounded RIFF/RIFX PCM/float and WAVEX validation through
existing libsndfile virtual I/O on an approved handle. Next: independently admitted
child report + terminal deadline/cancellation → validated SourceFile identities +
explicit roots/replacements/portable mappings → desktop media checklist → verified
transactional copies/new-project semantic state/loss review → Undo/reopen/aligned
renders. RF64/W64/compressed/partial precision and broader sources stay in the full
adapter backlog. Linux/native Windows/provider/installed/Europe qualification stay
separate from this prerequisite; full milestone scope is unchanged.

## X004 desktop media check increment (2026-10-09)

Checkpoint124 implements explicit folder/replacement checks and independent child
report/lifecycle admission. Next dependency: verified transactional media copy
with unchanged-source/mapping/cancel/storage-failure evidence → extended destination
semantics/loss preview → Undo/reopen/independent aligned rendering → refreshed
installed preview pairs. Native Windows, OS containment, broader media/registered
formats and European localization remain parallel gates. Full scope is unchanged.

## X004 verified partial staging increment (2026-10-09)

Checkpoint125 implements bounded verified bytes into an exclusive owned destination
with actual core/child cancellation/refusal evidence. Next: versioned original
occurrence/format/replacement provenance → exclusive commit + interrupted-job
recovery → desktop owned copy lifecycle → extended destination semantics/loss review
→ Undo/reopen/aligned renders → installed preview pairs. Staging alone publishes no
Session asset. Native/storage/OS-containment/format/Europe gates remain required.

## X004 provenance/publication/recovery increment (2026-10-09)

Checkpoint126 binds original occurrence/selection/checked bytes, writes planned
intent, exclusively publishes a verified receipt and reopens owned data after
interruption without source approval. Next: frozen checked-row child request with
explicit destination and terminal deadline/cancellation → commit/planned/recovery
UI → persistent original/loss state + destination gain/fade/rate/pitch/timing →
conversion preview/Undo/reopen/aligned renders → installed preview pairs.
Multi-file/project atomicity, broader media/native/exchange families, Windows/
storage/OS-containment/Europe and full frozen parity gates stay required.

[Desktop checked-copy/recovery](127-desktop-checked-media-copy.md) now carries
frozen inspected/checked rows to admitted children and preserves uncertain
post-start outcomes until explicit recovery. Extend the owned import state and
destination semantic clip model next, then conversion approval/Undo/reopen/render
qualification. Refresh installed previews; native adapters and every independent
M1/M2/full-suite and localization gate remain required.
