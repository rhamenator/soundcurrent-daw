# Exact sampling for protected audio cores

2026-10-15. Implementation source `6bf1b4e`. The full DAW goal remains incomplete.

## Component and ownership

`PreparedProtectedCoreSampling` is a framework-independent, off-RT component for
the direct portions of a transient-protected render. It uses the existing pinned
positioned resampler without changing that kernel or the stretch worker. Its
output clock maps destination sample indices into the original geometry's exact
output coordinates. The clock can express sample-rate conversion and a fractional
crop origin. Semantic anchors and core boundaries remain exact rational values.

For a core with half-open output interval `[b,e)` and clock `g(i)`, it selects
integer sample indices for which `b <= g(i) < e`, intersected with the requested
output window. A bounded binary search finds these indices without floating-point
coordinate arithmetic or a common-denominator product. This discrete sample
selection does not round or move `b` or `e`. The protected region has unity slope
in the geometry's acoustic coordinate system, so its original raw coordinate is
`rawBegin + g(i) - b`. Cropping through a core therefore preserves the same phase.

The plan owns its marker IDs, rational boundaries, raw sampling maps and memory
lease. It survives destruction of the original planner. It copies no audio and
borrows no mutable project state. Source reads use at most 512 output frames per
call and the existing kernel's bounded FIR context, clipped to the admitted raw
asset with its explicit zero-extension policy. Integer neutral sampling preserves
sample bits, including signed zero and headroom. Preparation is cancellable and
resource-limited; disposal returns credits after owned storage is retired.

Callers own and admit source/output buffers and verify the immutable original raw
asset. This geometry-only component does no file I/O, project mutation, logging,
GUI work or worker publication. It is not a real-time callback API.

## Qualification

Qualified at clean implementation `6bf1b4e811fb223bbd20c4d7d80b79ce7f0b4219`:

- Release: five selected suites pass in 4.44 seconds; Debug: five in 13.26 seconds.
- ASan/UBSan: both new suites pass in 3.58 seconds, including leak checking.
- Each configuration checks 137,142 conditions, 119,394 analytic samples and
  14,080 integer bit-exact samples. Maximum analytic error is
  `3.769772867201482e-7` under the existing `2e-6` phase gate; peak 2.49999976
  demonstrates preserved headroom. Original planner destruction, block partition,
  seeks, crop phase, edge context, 256 channels, overflow and cancellation are covered.
- Independent Python Fraction oracle: 240 cases and 2,232 exact assertions per
  configuration. All three response corpora match, including origins beyond
  floating-point exact integer range and near the signed 64-bit limit.
- Both new C++ translation units pass MinGW syntax checks without diagnostics.
  Native Windows execution of this component remains pending.

[Evidence index](../tests/results/M2/2026-10-15-protected-core-sampling/index.json)
retains source/binary hashes, original commands, logs, oracle inputs/responses and
explicit limits. No UI strings changed; no new localization qualification is claimed.

## Integration still required

The complete renderer still needs exact-clock nonunity gaps and halo blending,
including sample ownership where a crop intersects a blend. The isolated worker
must bind the original asset, geometry, clock, kernel identity and history in a
versioned request, reject changed inputs before publishing, and pass repeated
adoption/undo/live/export and actual desktop workflows on both platforms. Original
semantic anchors outside a cropped window must still inform the surviving
protected span. Baked pitch changes need their own validated treatment.

Existing integer-only admission remains in the current worker; this component
alone does not advertise fractional protected edits in the app. No existing
refusal, deadline, quality gate or persisted format was changed. Independent
processing-quality and perceptual comparisons remain required. The preceding
44.1 kHz native nominal-render numerical observation is separate and remains open.

## Original CI retained before the next source advance

[Run 38689906853](https://github.com/rhamenator/soundcurrent-daw/actions/runs/38689906853)
ran at the preceding source `347ef70`; it does **not** test the new sampler.
All nine full job logs, terminal APIs, eight original artifact ZIPs and 214 PNGs
are retained and verified. [Original CI index](../tests/results/M2/2026-10-15-protected-core-sampling/original-ci-index.json)
provides the portable audit and detailed native corpora.

- All 201 Linux tests pass across disjoint shards, with every subsequent archive
  gate passing. Actual CTest times: 147.65 / 774.69 / 899.93 seconds.
- Native core: 25 comp and 93 other suites pass, including the original one-frame
  startup regression. Cross-build passes separately.
- Native repeated protected desktop: one bank passes in 83.62 seconds; job time
  is 9m04s. Nine adoptions, three retained mixed pairs, 15 successful reaped workers,
  five failure/close cases, 32,000 protected raw samples, 1,640,832 unchanged samples
  and 1,810,560 live/export samples are checked. Actual project credits retire.
  All 14 render observations meet the original 30-second bound (maximum 4,684 ms).
  All nine retained WAVs match current and preceding Windows core outputs.
  The native generation-1 Review image shows the corrected current status.
- Original desktop: all 28 tests pass in 253.98 seconds, but the job exceeds its
  **20-minute execution limit** and is marked cancelled. GitHub's original failure
  annotation confirms the cause. The required desktop aggregate fails; the whole
  run is cancelled, not successful. All original uploads completed and are retained.
- Deployment inputs: 15 files and 1,741 corresponding-source files verify against
  the original native checkout filters; both media builds match the reviewed pin.
  This is no installed app or physical-audio qualification.

The workflow now separates the original six comp tests/four build targets into
another desktop job. The three suites select 22 / 6 / 1 tests exactly once, retain
all original build targets/evidence paths, have independent artifacts, and feed
the unchanged required aggregate. All retain the 20-minute deadline and disabled
fail-fast. A static workflow/coverage check passes; fresh native execution is pending.

Linux scheduling uses all 201 actual observations plus explicitly provisional
Debug-derived estimates for the two new sampler tests. The 203 tests distribute
68 / 67 / 68 with approximately 608 seconds per shard; the former new desktop
bank estimate is replaced by its actual 147.62-second measurement. Existing
30-minute job limits, test deadlines and archive checks are unchanged. This is a
scheduling estimate, not proof of future runtime. The scheduler's unknown-future-test
case remains covered.
