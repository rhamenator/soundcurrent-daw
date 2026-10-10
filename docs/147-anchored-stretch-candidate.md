# Anchored alternative: exact scheduling and measured acoustic limits

2026-10-10 UTC. Frozen baseline unchanged. The preceding goal turn made progress:
PR87 merged normally as a08eb0e with matching tested tree7d67b179, all four
protected gates and110 Linux/42 native core/12 Qt tests. This increment advances
from its original exact planner to actual alternative-processor evidence.

## Implementation and provenance

The original [C++ scheduler](../experiments/anchored-stretch/render_probe.cpp)
uses the framework-independent normalized map to drive exact processing-centre
output coordinates. It splits97/512-frame chunks at all interior anchors, records
each exact inverse position and retained fractional residual, and supplies source
up to floor(inverse(output))+inputLatency. Quantization of the vendor input cursor
is explicit; marker geometry is unchanged and no fractional anchor is rounded.
This frozen bank contains integer marker positions; arbitrary fractional acoustic
alignment remains unqualified.

The already evaluated [Signalsmith Stretch source](https://github.com/Signalsmith-Audio/signalsmith-stretch/blob/a670068d9aeb64913331d5cc29337b19a457a7df/signalsmith-stretch.h)
(a670068d9aeb64913331d5cc29337b19a457a7df) and declared Linear0.6.4 source
(de55e6a50ffcf6f8f43f649692d94691c7025151) remain outside the shipping build.
Exact code/license/document/blob hashes and regular include wrappers are retained,
with both upstream MIT notices. No automatic CMake fetching or optional FFT/SIMD
source is enabled. The earlier symlink inference refused before compilation;
initial/corrected review notes and configure-only setup failure are retained.

A Linux GCC15.2.0 C++20 configure-only check first reported default8-channel48k
block5760/interval1440/inputLatency2880/outputLatency2880. The new actual renderer
uses explicit seed20261010 and no split computation. It seeks the actual first
inputLatency source frames; processing-centre output runs0..target, appending
exactly2880 zero final-lookahead frames, then flushing2880 outputs. Complete
source, generated pre-roll+output and the exact slice after2880 leading frames
are all retained. This named edge/latency policy is not new raw source context,
not an unexplained duration repair and not a shipping default. The candidate's
short-input `exact()` false/zero behavior is not used or interpreted as success.

## Frozen actual experiment

[The contract](../experiments/anchored-stretch/contract.json) was written before
rendering; its exact hash and the analyzer's pre-run hash bind the actual bank.
24 actual processes cover four families × three maps × two output chunks:

- Stereo impulses, eight-channel decaying attacks, eight-channel440/730Hz sustain,
  and eight coincident +/- attack channels for cancellation.
- Original32,768-frame48k sources and identity32,768 or uniform/nonuniform49,152
  outputs. Four known source events4096/12288/20480/28672. Nonuniform targets
  6144/16384/32768/43008; implicit endpoints remain exact.
- Attack/sustain gains and raw offsets0,3,…,21 frames remain independent of time
  map scaling. Coincident cancellation pairs have zero raw offset.
- One-thread512MiB address ceiling,10s child/60s bank. All24 exited0 with empty
  stderr in2.708s. Current native Windows processing/audio was not executed.

## Diagnostics and limits

The preregistered32-frame causal RMS detector uses first10% of annotated-window
maximum and original-source bias, retaining exact mapped bias and independent
physical channel delay.432 observations are window-resolved;176 meet the strict
<48-frame diagnostic. A resolved window is not universal audible ground truth.
The source/identity control and onset interpretation remain separate from
perceptual/listening acceptance.

| Map / signal | Onset observations | Timing diagnostic passes | Maximum calibrated error | Maximum arrival-offset change |
| --- | ---: | ---: | ---: | ---: |
| Identity impulse | 16 | 16 | 0 frames | 0 frames |
| Identity attack | 64 | 64 | 0 frames | 0 frames |
| Identity cancellation | 64 | 64 | 0 frames | 0 frames |
| Uniform impulse | 16 | 16 | 15 frames | 0 frames |
| Uniform attack / cancellation, each | 64 | 0 | 138.5 frames | 0 frames |
| Nonuniform impulse | 16 | 16 | 32 frames | 0 frames |
| Nonuniform attack / cancellation, each | 64 | 0 | 146.5 frames | attack1 frame; cancellation0 |

The same preregistered diagnostics show:

- 420 selected group phase observations;155 within the declared0.0001-degree
  numerical diagnostic. Maximum selected source/output relative-phase change
 0.3635129734 degrees. These are known synthetic carriers, not arbitrary group proof.
- 24 cancellation pairs;20 within1e-6 numerical sum. Maximum pair residual
 0.0002989508212, versus exact zero in the original PCM.
- 432 pre-echo/event RMS ratios;144 within the declared−40dB diagnostic. This
  diagnostic cannot qualify listening or make ambiguous events a pass.
- 12 partition comparisons;four exact, maximum sample difference2.4332959652.
  Input-chunk policy belongs to a future algorithm identity, not an invariance claim.

These observations reject this particular adapter/configuration as a shipping
solution for attack and phase-preserving group workflows. They do not prove the
library can never satisfy a different documented configuration. Exact geometry,
finite output and impulse timing alone do not promote P008/P011 or Q-STRETCH.

## Retention and next concrete implementation

[The archive](../tests/results/M8/2026-10-10-anchored-stretch/README.md) retains all
complete PCM/source/outcome/contract observations, candidate notices and earlier
setup/admission failures. The original archive exceeds32MiB; the explicit64MiB
storage allowance retains149 members /37,351,357 bytes without altering signal,
processing or quality criteria. Standard-library inspection passes20,896 checks,
including independent rational scheduling, exact latency slice, cancellation and
partition samples. It does not rerun NumPy phase/onset measurement or DSP.

[ADR106](decisions/106-transient-protected-warp-next.md) chooses an original
transient-protected map mode as the next implementation. Represent user-anchored protected spans explicitly
with unity local slope and stretch the remaining real-source intervals. Keep exact
source markers, separate algorithm/edge/chunk identity, full PCM and fixed gates.
Only supported acoustic cases then advance to a production adapter and editable
marker/reviewApply/Undo/save/reopen/cropSplit/shared playback-export workflow.
General material, pitch/formants, grouped comp, native installations/recovery,
all remaining milestones/import/equipment and all-Europe F/Q/C/N remain required.
No VM, equalizer or physical audio change; the full DAW goal stays active.

## Build-source binding repair and actual reproducibility

PR review identified that the initial archive bound the renderer/runner/analyzer
but lacked build-time planner cpp/header hashes. The original observation is
retained with that limitation. An actual fresh low-priority build records142
compiler/library/source inputs before and after compilation, with identical
postflight hashes and exit0 in9.714s. It reproduces the exact original renderer
SHA256bfb6db94…8487954.24 additional terminal renders under the same frozen
contract finish in2.779s; all72 complete WAV references,24 reports/schedules and
all diagnostics repeat exactly. Both48 actual process outcomes /144 full WAV
references resolve retained complete bytes. No audio is regenerated by inspection.

The verifier binds retained planner cpp/header and renderer source to the actual
preflight receipt, checks its full before/after equality, preflight/receipt hashes,
executed-binary equality and repeat runner/report/PCM identities. In-memory
negative injections refuse either planner source mismatch. Current-source native
Windows candidate rendering remains unexecuted and all quality limits stay open.
