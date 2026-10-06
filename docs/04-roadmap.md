# Staged backlog, dependencies and exit criteria

Status: early M1 implementation; session/state foundations exist, full feature workflows remain open. Windows and all-Europe localization now have [cross-milestone gates](08-platforms-and-localization.md). No fixed completion date or claim that combined parity is a small extension to the EQ is justified. This is a substantial multi-subsystem product; deliver useful milestones without shrinking its target.

Requirement IDs and milestone assignments live in [parity.json](../research/parity.json). The table below is the dependency graph and measurable gate for each stage. When a row contains several subfeatures, create child issues during M0 rather than treating the row title as a completed feature. Bugs and accepted gaps remain traceable to that row.

## Milestones

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
