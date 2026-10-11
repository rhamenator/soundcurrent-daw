# Constant-stretch transient observations and open quality gates

This bounded experiment follows [checkpoint168](168-stretch-rate-quality.md).
The [contract](../experiments/stretch-transient-quality/contract.json) and runner
were committed at `d54dd878` before execution. No production DSP, cache identity,
project state or UI changed. The compiled worker from `20a062d` was reused only
after checking its executable hash and all 309 unchanged compiled source inputs.

## Actual experiment

Nine original stereo sources cover 44.1, 48 and 96 kHz: isolated impulses,
100 ms harmonic attacks and the same attacks over a continuous harmonic bed.
Each has `2*rate+1` frames and three annotated events, with a seven-frame physical
channel delay and gains 1.5/−1.125. Four existing constant stretch ratios per
source (1/2, 1, 3/2, 2) produce 36 actual terminal worker renders. Pitch is zero,
formant preservation off, integer source origin and no extra context.

The preregistered causal 32-frame RMS detector measures original and output
landmarks inside −75/+120 ms windows. Its threshold is 10% of the local maximum.
Boundary crossings and background already exceeding the threshold are unresolved.
Expected onset is the exact rational scaled original detected onset, separately
from the nominal source annotation. The timing gate is strictly less than 1 ms.
Physical interchannel delay is recorded without pretending a propagation delay
should scale with duration. This does not qualify microphone-group phase.

| Observation | Actual result |
| --- | --- |
| Renders, duration, finite PCM | 36/36 complete; exact ceiling duration; all finite |
| Unity | Nine byte-exact PCM renders; identical source/output detector results |
| Channel/event timing | 216 observations; 56 resolved; 52 pass, **four fail** |
| Unresolved timing | **160**, including all 72 attack-over-bed observations |
| Largest resolved absolute drift | **1.564625850340136 ms** |
| Impulse timing | 25 resolved of 72; 22 pass, three fail |
| Attack timing | 31 resolved of 72; 30 pass, one fail |
| Silent-family pre-echo | 36 pass, **108 fail** at −40 dB |
| Background-bed pre-event energy | 72 diagnostics; excluded from silent-floor gate |

The four timing failures are half-duration renders: three impulse observations
at 44.1 kHz (+69, −89/2, +107/2 frames), and one 48 kHz attack (−99/2 frames).
The pre-echo window is 20 ms ending 1 ms before the mapped raw marker, relative
to the following 100 ms event RMS. Neither failures nor unresolved results were
removed, reclassified, or used to loosen thresholds. These measurements expose
limits of this constant mode and detector. They do not invalidate the separate
annotated protected-core prototype, and that prototype's narrow passes do not
qualify this wider constant-mode gate. Q-STRETCH remains open.

## Independent retained-audio audit and resources

[Retained evidence](../tests/results/M2/2026-10-11-stretch-transient-quality/README.md)
includes all 45 original/rendered WAVs and every observation in a 16,510,292-byte
archive. Its standard-library verifier independently recomputes detector and
energy measurements from PCM, without importing the experiment's numerical
implementation or rerunning DSP. It reproduces all four failures, 160 unresolved
results, 36/108 pre-echo outcomes and nine unity identities. Passing this audit
establishes reproducibility of these results, not a passing quality gate.

Execution took 58.43 seconds, serial children, nice 10 and ionice class 2/level 7.
Each worker has a 256 MiB ceiling, ten-second processing deadline and fifteen-
second observation deadline. Bank limits are 180 seconds and 128 MiB owned data.
The archive expands to 54,892,297 bytes; auditing limits archive size to 24 MiB,
expanded data to 64 MiB and each member to 4 MiB. No VM, audio endpoint, playback
interruption, installer, new shipping dependency or equalizer change occurred.

## Next implementation task

Deliver the first versioned loop-recording/take-lane state and acceptance workflow,
then comp selection with grouped edit/undo/save/reopen evidence. Keep this bank
as a failing quality baseline for later mode/landmark work. Do not delay useful
recording previews by endlessly adding optional DSP diagnostics. Full marker/
tempo editing, other rates/layouts, music/listening and native Windows quality
remain required, along with the full M2–M11/X004/X005 and European F/Q/C/N scope.

## Portable source audit review

The audit now carries a bounded source proof: 411 original commit/tree/blob
payloads, 4,101,542 bytes expanded in a 1,194,851-byte ZIP. Both source commit IDs
and all 309 declared code inputs plus runner/contract/helper/observer paths are
resolved and checked in memory. No local historical Git objects, parent closure,
object import or signature-authentication claim is needed. An owned minimal
repository missing both commits fails with the prior audit and passes the new
one; mutating a blob and rebinding packet metadata still refuses. Original PCM
and outcome capture bytes are unchanged, and all known failures remain visible.
