# Clocked protected-gap rendering

2026-10-15. Implementation `0d57e517fe7677a9585ae6354346fe6a7699d40e`.
Full DAW and independent processing-quality admission remain incomplete.

## Owned exact partition

`PreparedClockedProtectedWarp` prepares the full original semantic geometry before
selecting the requested output samples. Nonunity gaps own expanded halo intervals;
the complement is verified to have unity slope at every original map knot.
Touching protected spans and unity gaps therefore leave no holes or overlaps.
Derived coordinates and memory credit are owned; no original planner or mutable
project state is borrowed. Original anchors outside a crop still define its audio.

Each stretched gap has a fixed intrinsic grid independent of the crop and output
clock. Exact linear source extension supplies halo analysis context. Integer
processing counts choose sample grids whose exclusive endpoints retain the exact
rational spans; they do not quantize semantic markers. The existing pinned R3
backend stretches between those grids, compensating pitch for their exact step
ratio. The final positioned sampler maps requested destination samples into that
intrinsic rendered grid. A crop renders the same full contributing gap and skips
unrequested samples, instead of changing its analysis window or phase.

A bounded streaming buffer retains only the intrinsic samples required by the
next output block's FIR context. Slabs are admitted before allocation, reads and
writes are at most 512 frames, and work is cancellable. Complete finite reads are
required. The caller binds immutable original media and discards partial output on
failure. Existing worker process/CPU limits must also cover vendor allocations;
explicit slab accounting is not a claim to account for the backend's entire heap.

## Halo blending and identity

Halo weights use exact geometry distances converted to floating amplitude only
at the blend. At the outer halo edge the output is original unity-slope raw audio;
at the gap edge it is the rendered gap. These distances remain attached to full
semantic boundaries when a crop begins inside the halo. Raw core samples agree
with the previously qualified prepared-core sampler. Unity partitions avoid R3
and preserve sample bits, signed zero and floating-point headroom.

This is a **new processing identity**,
`soundcurrent.protected-linear-gap-clocked-v1`. It uses linear gap extension and
continuous semantic halo ramps. It does not replace or relabel legacy integer
keyframe/halo output. Existing worker admission and persisted project formats
remain unchanged until versioned integration and independent quality verification.

## Wide processing grids

The original probe exposed a valid fractional gap whose composed step exceeded
the existing 32-bit processing-grid representation. `ExactProcessingGrid` adds a
separate 64-bit processing-only ratio, using the existing checked exact arithmetic
for coordinates and overflow refusal. `SourceFrameMap` and project formats retain
their existing limits. The positioned kernel has an overload for this grid; its
original coefficients, taps, phase quantization and 32-bit timing path remain.

The common constructor initially conflicted with the public sample-rate constructor;
a private tag resolves overload selection. That build failure and subsequent stale
probe output are retained and are not qualification. The independent wide-grid
sample fixture also initially reduced one intended wide ratio below 32 bits; its
fraction denominator was increased so the same assertion genuinely exercises a
wide ratio. Original logs and the pre-correction fixture remain retained.

## Qualification

Qualified engine source `8178da52839a45fe075a63a08a862727ebb568f4` (checked product fast path added
by `8178da5`; exact wide fallback retained):

- Release: eight suites pass in 30.21 seconds.
- Debug: eight suites pass in 124.77 seconds.
- Optimized Debug (`-O1`) with ASan/UBSan and leak checking: five suites pass
  in 156.00 seconds, under unchanged individual deadlines.
- Every configuration checks 1,142,952 conditions, 29,694 crop samples and
  22,624 direct protected samples across seven cases, including compression,
  fractional anchors, all halo crop edges and 44.1/48/96 kHz clocks. Maximum
  retained intrinsic buffer is 1,381 frames; output peak 2.54976272583 preserves
  floating-point headroom. A 211 Hz tone has maximum measured pitch error
  0.01822930418 Hz under the existing 1% gate. This single-tone check is not a
  general sound-quality qualification.
- Wide-grid sampling checks 28,673 analytic samples with maximum error
  3.2195473487206527e-7 under the 2e-6 gate. The independent Python Fraction
  oracle passes 37 cases and 257 assertions, including a product that requires
  the portable wide fallback. All three exact response corpora match.
- Existing kernel comparison retains 323,574 samples with maximum difference
  zero and 750,959 checks. Prepared-core sampling/oracle regressions pass.
- All five changed/new C++ translation units pass MinGW syntax checks. This
  does not establish new native Windows runtime behavior.

Two **unoptimized sanitizer attempts timed out** at the original 180-second
renderer deadline, once before and once after the checked product fast path.
The four other selected suites passed in each attempt. No sanitizer error was
reported before timeout; neither run is a sanitizer pass. Their complete original
logs/results remain retained. The optimized sanitizer build is separately recorded
with its actual compiler flags, source/binary hashes and the same full assertions
and deadlines. No timeout increase, bank reduction or runtime-fix claim is made.

[Local evidence index](../tests/results/M2/2026-10-15-clocked-protected-warp/index.json)
contains the successful and failed original qualification, initial probes, fixture
correction, source/binary hashes, commands, compiler configuration and oracle data.
No application strings changed; no additional localization qualification is claimed.

## Preceding original CI and evidence retention

[Run 38693427671](https://github.com/rhamenator/soundcurrent-daw/actions/runs/38693427671)
ran at `6b4cd5134623fa62ca99a3e8260d7295a8b2e39d` and passes all ten jobs. It tests
the preceding prepared-core sampler, not this new gap renderer. All ten full job
logs/APIs, nine original ZIPs and 214 PNGs were retained and verified before the
next PR advance. The [CI evidence index](../tests/results/M2/2026-10-15-clocked-protected-warp/original-ci-index.json)
contains the portable audits, original logs/images and scoped native corpora.

- Linux: all 203 tests and subsequent archive gates pass across disjoint 68/67/68
  shards. CTest times are 413.21 / 602.36 / 482.95 seconds.
- Native Windows: 27 comp and 93 other suites pass; desktop banks 22/6/1 and the
  required aggregate pass. The one-frame startup regression passes.
- Native prepared-core oracle: all 240 responses and 2,232 exact assertions pass;
  responses match Linux Release/Debug/sanitized and script bytes match the tested
  head. The C++ test PASS is retained in the original job log. Its emitted numeric
  counters were **not retained**: a second CTest invocation overwrote LastTest.log.
  The failed derived audit assumption is retained, and the corrected audit reports
  this gap without inventing counters. Future CI copies LastCompTest.log before
  starting the second bank and preserves the first exit status.
- Repeated protected desktop: nine adoptions, three mixed pairs, 15 reaped workers,
  five failure/close cases, 32,000 protected raw, 1,640,832 unchanged and 1,810,560
  live/export samples pass; actual credits retire. All nine WAVs match current
  and preceding Windows core. Fourteen original timing observations pass, maximum
  4,408 ms. Representative Review/applied images were visually inspected.
- Source/deployment audit verifies 15 deployment files and 1,747 corresponding
  source files using actual Windows checkout filters. Native core job time is
  27m16s; desktop main/comp/protected jobs take 15m07s / 13m59s / 10m14s.
  No installed or physical-audio result follows from these synthetic CI results.

Linux scheduling now uses all 203 original observations and explicit provisional
Debug-derived estimates for the three new suites. All 206 tests are assigned once
across three shards (69/69/68 tests). Every test,
archive gate and job deadline remains unchanged. Fresh CI must establish actual
native execution and scheduling margin for this implementation.


## Remaining integration and quality work

The comp adapter still needs exact outer handle extents: the existing original
geometry uses integer processing endpoints, which must not be substituted for
fractional expanded handle bounds. The isolated worker then needs a versioned
request binding original media, full semantic
geometry, output clock/crop, processor/kernel identity and history. Verification,
publication/adoption, undo/save/reopen, live/export and real desktop paths must be
qualified on both platforms. Baked pitch changes need explicit treatment. The
current implementation refuses insufficient linear raw context, unrepresentable
arithmetic and short unsupported nonunity gaps; it never silently rounds, patches
output duration, or falls back to treating assembled PCM as raw.

The current evidence is synthetic implementation acceptance. Transient fidelity,
complex programme material, spectral/phase quality, extreme ratios/channel counts,
perceptual comparison, installed previews and physical audio remain required.
The preceding 44.1 kHz native numerical observation remains independently open.
