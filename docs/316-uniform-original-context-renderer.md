# Render short uniform comp spans from original context

Date: 2026-10-15. Qualified core source: `d4729b9`.

## Implemented core

`CompUniformContextPlan` and `renderUniformContextCompSpan` provide a separately
identified producer, `soundcurrent.comp-uniform-original-context-v1`. They retain
the original uniform nominal analysis domain, including declared original context,
and extend that domain to include the current candidate and its actual ancestry.
Unrelated tracks do not influence the analysis domain.

Preparation checks every map knot for affine geometry, validates original source
and sample-clock bindings, and proves the current candidate slope against the
nominal map and baked speed history. It composes the candidate's exact fractional
coordinates onto the full context's intrinsic output grid. Input and output
rounding receive explicit pitch compensation. The final sampling grid applies
previously baked varispeed; nominal pitch and formant settings remain independent.
Protected/nonuniform geometry is refused by this producer.

The renderer studies and processes the full context with fixed 512-frame blocks,
then emits only the requested candidate through a bounded streaming buffer.
Explicit memory allocation depends on channel count and resampling windows, not
on the duration of the original context. Source reads and candidate writes are
bounded to 512 frames per callback. Polls cover planning, source reads, backend
passes, draining and candidate output. Nonfinite or incomplete input, duration
mismatch, cancellation and failed writes are errors; no silent repair is applied.
Foreign backend memory still requires the existing isolated-worker ceiling when
this producer is integrated. These callbacks are for worker/control use, never
for the audio or GUI callback.

The old uniform renderer and its identities are unchanged. The new library is not
yet selected by an owned job, paired selection or the application UI.

## Qualification

[Portable evidence and exact-source receipt](../tests/results/M2/2026-10-15-uniform-original-context/index.json)
retains test commands, build/test logs, executable/source hashes, raw candidate
PCM, reports and earlier probe outcomes.

- Release: new core workflow and existing uniform-render regression pass in
  14.69 seconds total. Debug: new core workflow passes in 24.19 seconds.
- Six fractional-history cases at 44.1, 48 and 96 kHz cover one and two baked speed
  changes, including original pitch restored by the second speed change.
- The exact 48 kHz, 4,054-input-frame regression is refused by the existing short
  renderer and succeeds with original context. No minimum window is waived.
- The streaming candidates match an independent full-context accumulator and
  separately chunked extraction across 21,354 sample comparisons. Independent
  ancestry arithmetic checks crop phase and sample step; measured sine frequency
  checks baked pitch.
- Stereo with an explicit octave pitch change, formant preservation disabled and
  baked speed 1.5 preserves both distinct channel frequencies and float headroom.
- Cropped adopted takes at both original-source edges render successfully within
  raw read bounds. A genuinely short full original domain still refuses before
  any candidate output.
- Incomplete source, failed writer, late cancellation, preparation cancellation
  and insufficient resource budgets refuse; all explicit resource credits return.
- Seven PCM files are byte-identical across Release and Debug. Both new C++ units
  pass MinGW syntax checks. This is not native Windows execution evidence.

The initial edge fixture requested a nominal envelope, which includes the full
artifact; its assertion incorrectly expected a 3,000-frame cropped take. The
retained failure led to a fixture correction: first construct the actual adopted
crop, then require 3,000 output frames at each source edge. No production duration
check was weakened.

CI includes the new core suite in Linux inventory and the native comp bank, with
retained evidence paths and a 180-second suite deadline. The expected inventory
becomes 219 Linux tests and 41 tests in the first native comp bank. Existing jobs,
other selections and deadlines are unchanged. Fresh qualification of this source
has not yet run.

## Remaining delivery work

The worker request needs a new protocol binding the full raw domain, both grids,
exact candidate phase, settings, baked history and fixed block schedule. Its
reader must rederive these fields from the verified project/recipe history,
verify owned source media, and publish completion only after the exact candidate
has drained. Add refusal tests for altered geometry/history and cancellation.
Then integrate an explicitly versioned pair policy and the actual desktop repeated
short-edit workflow. Do not reinterpret old saved requests or retry a failed
legacy job using a different producer.

A whole original domain that is itself too short remains unsupported by this
strategy, even if additional raw media exists outside it. A future extension must
declare that extra analysis context explicitly. Long original domains may require
more processing time; fixed memory does not establish a performance guarantee.
Broader material/layout/pitch quality, native Windows execution, worker/UI
integration, installed previews and full-suite parity remain open.
