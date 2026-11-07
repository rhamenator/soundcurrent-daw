# Exact semantic outer warp extents

2026-10-15. The full DAW goal remains incomplete.

## Domain and API

`ExactClipWarpRegion` represents input and output extents as canonical rational
positions. They describe semantic boundaries, not a chosen number of samples.
An explicit `exactWarpDomain` constructor tag selects this path for `ClipWarpMap`
and `ProtectedWarpPlan`. Both reuse the existing piecewise interpolation and
protected-window construction; the implicit final knot and final gap now retain
the exact extents. Original user anchors and their protection windows remain owned.

The old `ClipWarpRegion` constructor retains its sample-grid admission and legacy
region view. The exact-domain path refuses that legacy view and integer vendor
adapter, even when a particular pair of endpoints is integral. The new clocked
renderer and protected-core sampler use the exact region accessor. No rounding,
synthetic integer endpoint, project schema change, or legacy renderer substitution
is introduced. An initially ambiguous overload is retained as a build failure;
the explicit tag corrects it without changing existing call sites.

A semantic exclusive endpoint is not an actual source sample. The exact planner
checks its arithmetic, physical origin and interior marker positions. It does not
clamp an exclusive boundary to the asset length. The prepared sampling adapter
checks every selected actual grid against the admitted asset. A boundary slightly
past the final asset sample can therefore remain exact while valid final samples
are rendered. A regression compares the legacy fractional-origin asset tail with
the equivalent exact-domain tail. Source reads still use the existing FIR context
and edge-extension policy; no extra physical sample is invented.

The first audio fixture was shorter than the backend analysis window. That failure
and fixture are retained. Longer gaps exercise successful fractional-boundary
rendering, while the original short case remains a required refusal with resource
cleanup checked. Backend minimum-window admission is unchanged.

## Evidence

Qualified at clean implementation `1ef0ac89635b0200fdc21bdb0e7ca487380874ae`:

- Release: seven suites pass in 34.81 seconds.
- Debug: seven suites pass in 138.23 seconds.
- Optimized Debug with ASan/UBSan and leak checking: five suites pass in
  165.59 seconds. Existing individual deadlines are unchanged.
- The new renderer/domain bank checks 31,686 conditions, 1,412 cropped samples,
  2,688 protected-core samples, unchanged integer-domain output, exclusive asset
  tails, invalid extents, legacy adapter refusal and credit retirement.
- The independent Fraction oracle checks 48 cases and 1,368 exact assertions.
  Responses match across all three builds, including origins above floating-point
  integer precision and near the signed 64-bit limit.
- The preceding clocked-renderer and protected-core reports are unchanged in all
  three configurations. All 18 legacy protected-render output hashes match their
  original qualified corpus in both Release and Debug; stretch-state tests pass.
- Five C++ translation units pass MinGW syntax. New native runtime is pending.

[Portable evidence](../tests/results/M2/2026-10-15-exact-warp-extents/index.json)
retains successful qualification, the initial short-window refusal and overload
build failure, original fixtures, commands, source/binary hashes and oracle data.



## Preceding native renderer and full CI qualification

[Original CI 38700333701](https://github.com/rhamenator/soundcurrent-daw/actions/runs/38700333701)
passes all ten jobs at `46ff86bdbf43b5a120bd32ae81dfa87f8a62719c`. It qualifies the
preceding clocked renderer, not the new exact outer-extent path. All terminal APIs,
ten full logs, nine original artifact ZIPs and 214 PNGs were retained before the
next source advance. [Portable CI evidence](../tests/results/M2/2026-10-15-exact-warp-extents/original-ci-index.json)
includes the original first CTest log, exact oracle responses and scoped native
corpora as well as the audits.

- All 206 Linux suites pass across disjoint 69/69/68 shards and all subsequent
  archive gates pass. CTest times are 485.56 / 703.89 / 499.66 seconds.
- Native Windows passes 30 comp and 93 other suites, including the startup
  regression. Desktop 22/6/1 banks and the required aggregate pass.
- The first native CTest log is now retained separately. The clocked renderer
  records 1,142,952 checks, 29,694 crop samples, 22,624 protected samples,
  1,381 maximum buffered frames and maximum 211 Hz tone error 0.01822930418 Hz.
  Actual credits retire. Wide-grid and protected-core numeric reports pass their
  gates. The independent 37-case wide-grid and 240-case core-sampling responses
  match Linux Release/Debug/sanitized and their source bytes match the tested head.
- Native repeated desktop retains nine adoptions, three mixed pairs, 15 reaped
  workers, five failure/close cases and actual project credit retirement. All nine
  exported WAVs match current and preceding Windows core. Fourteen timing
  observations meet the original bound, maximum 9,017 ms. The original Review
  screenshot was inspected and shows the correct current-generation status.
- Deployment audit verifies 15 payloads and 1,756 corresponding-source files using
  actual Windows checkout filters. No installed or physical-audio claim follows.

Linux scheduling now uses all 206 original observations and explicit provisional
Debug-derived estimates for the two new suites. All 208 tests are assigned once;
existing job/test deadlines and archive gates remain unchanged. Fresh CI must
verify new native outer-extent behavior and actual scheduling margin.

## Application integration remaining

The exact geometry is now usable by the clocked renderer, but the comp adapter
still must bind it to the complete original raw/nominal semantic map and output
clock. Current handle envelopes retain whole maps; regenerated maps may normalize
to cropped artifact coordinates and omit outside anchors. Reconstruct from original
semantic history, preserving relevant outside anchors and full gap analysis windows.
Do not equate current assembled samples with raw source data or scale protection
lengths into the wrong acoustic domain.

Then add versioned worker requests and processor identity, checked source/history
bindings, verified publication/adoption, Undo/Redo/save/reopen, shared live/export
and desktop tests on both platforms. Pitch already baked into ancestors requires
explicit treatment. Legacy worker gates remain in place; this component alone
changes no installed preview or user-visible capability. Processing quality,
perceptual/material/extreme-layout qualification and the earlier 44.1 kHz native
numerical observation remain separate open work.
