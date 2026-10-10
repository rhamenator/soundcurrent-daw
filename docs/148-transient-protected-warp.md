# Original transient-protected warp: exact attack cores and explicit gap limits

2026-10-10 UTC. Production base a8fa5f1 (PR88); full frozen-reference goal remains
active. This implements the next original processing mode selected in ADR106.
It is an opt-in experiment, without editor, project-schema or shipping adoption.

## Named map and processing contract

The original framework-independent [planner](../experiments/protected-warp/protected_plan.hpp)
retains each user marker's stable ID and exact source/output coordinates. Its
separately named map derives start/anchor/end boundaries, with unity local slope
inside each protected span. Derived evaluation UUIDs are scratch identities;
semantic identity is the original owner plus boundary role. This is a change to
the map, not identical processing of the old linear-between-markers map.

The [contract](../experiments/protected-warp/contract.json) and analyzer were
frozen before the first build/render: protect256 frames before and2048 after each
user anchor; blend64-frame actual-source halos outside the copied core. Stretch
the connecting source gaps with unchanged pinned Rubber Band4.0.0 R3, offline,
512 input chunks, no internal threads, explicit interior halo anchors and exact
target duration. Unity gaps copy the complete original bytes. The two/eight
channel policies remain Together/Apart; Apart is not a phase-preserving group mode.

Planning charges the shared resource ledger before owning marker/span/map storage.
Overlap, reversal, collapsed one-domain gaps, source/output edges, too-short
nonunity gaps and configured payload/count/ledger limits refuse explicitly.
Rational geometry stays exact; the first renderer refuses fractional coordinates,
without rounding. The minimum64-frame gap is a geometry admission threshold,
not acoustic qualification of all gaps that short. This bank's source gaps are
at least2048 frames; other spacing, sample rates and channel counts remain open.

No allocation, disk I/O, processor construction or destruction in this experiment
belongs in a real-time callback. The processor runs in an isolated control-side
probe; integration with the existing supervised worker is the next step.

## Actual experiment and verification

Six families × identity/uniform/nonuniform maps =18 cases per bank: stereo
impulses and eight-channel short attacks, long6144-frame attacks, attacks over
a440/730Hz background bed, sustained440/730Hz carriers and coincident +/- pairs.
All sources are original synthetic fixtures. Input32,768 frames at48k; output
32,768 or49,152. User source events4096/12288/20480/28672; nonuniform desired
positions6144/16384/32768/43008. Physical channel offsets0,3,…,21 remain independent
of map scale. Full source/output and all five complete source/generated gaps stay
retained, including sustained beds and tails in the processed intervals.

Initial and final explicit builds bind compiler, supplied static libraries,
planner/renderer/analyzer and production/vendor input hashes before and after
compilation. Initial warnings are retained; the final build has empty stderr.
The scratch-ID bound and indentation fixes change the executable identity; all
216 complete WAV references and reports remain identical across both18-process
banks. They finish in9.209/9.127s, under10s child/60s bank and512MiB child address
limits. The actual CMake build adds another18 terminal renders and1,296 independent
full-assembly checks. All648 WAV references resolve66 complete retained wavefiles.

Two local CTests pass:35 geometry/admission requests (11 accepted,24 refused;
470 Fraction checks with returned ledger credit), and the actual18-case full-PCM
assembly oracle. The default-OFF `SC_BUILD_PROTECTED_WARP_PROBE` adds no install
target. Linux and native MSVC CI explicitly enable it; native qualification is
recorded on the exact PR source rather than inferred from cross-compilation.

## Acoustic observations and uncertainty

| Observation | Result | Limit |
| --- | --- | --- |
| Protected cores |72/72 byte exact across all channels | Says nothing about the stretched gaps. |
| Annotated impulse/short attack/long attack/coincident attack timing |312/312 exact calibrated onsets; no arrival-offset change | Synthetic user-marked events; general transient detection remains open. |
| Attack over background bed |96 observations:16 window-resolved,8 timing passes,80 unresolved | Eight resolved measurements miss by up to4070.13 frames. Background landmarks are not reliable event ground truth. |
| Silent-family pre-echo |216/216 at or below−40dB |192 continuous-background observations retain original baseline/output excess separately. |
| Selected core phase |168/168 within0.0001 degree | Copying known PCM is not group-processing qualification. |
| Selected gap phase |70/210 within0.0001 degree; all70 are identity | Nonunity relative phase changes reach85.6422 degrees. Grouped sustained microphone material is unqualified. |
| Coincident cancellation |120/120 numerical observations; whole-output residual0 | Does not imply phase preservation between physically delayed microphone channels. |
| Boundary diagnostics |288 source/output adjacent-sample observations | Maximum output jump0.05535; no perceptual gate or listening acceptance. |

The unchanged <48-frame and other diagnostics remain separate from Q-STRETCH,
listening, arbitrary material, production/native installation and full P008/P011.
The sustained-gap failure must not be hidden by copied-core passes. F/Q/C/N and
all-Europe completion status are not promoted.

## Retention and next implementation

The [bounded archive](../tests/results/M8/2026-10-10-protected-warp/README.md)
contains170 members /17,349,109 bytes, with34,745,441 uncompressed bytes. Exact
whole-file deduplication retains all54 actual local processes and648 complete WAV
references. GPL original code/fixtures and existing vendor GPL-or-later notices
are retained; there are no executable/installer or personal-audio payloads.
Independent standard-library inspection passes2,730 checks without DSP/FFT replay.

[ADR107](decisions/107-protected-warp-integration-scope.md) makes the next task a
versioned worker/state adapter and one editable user marker, with review/Apply,
Undo/Redo, save/reopen/crop/split and shared playback/export. Keep this explicit
transient-protected mode experimental; do not claim general or phase-preserving
group acceptance from these fixtures. Broader warp, automatic analysis, tempo
mapping, pitch/formants, grouped comp, other milestones/import/equipment, native
installation/recovery and all-Europe coverage remain required.
