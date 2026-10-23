# Bounded waveform cache and source geometry

Checkpoint 345 adds a viewport cache around the isolated waveform controller and
Qt-independent projection helpers. The timeline has not yet been connected to
these components; installed previews are unchanged.

## Cache ownership

The cache admits a complete deduplicated visible source set before changing its
current project context. Default policy allows 32 entries and 96 MiB of retained
models, with separately charged metadata and controller/child storage. This is a
cache policy, not a track-count limit. Exceeding either budget refuses the whole
new view and retains the previous admitted view. A GUI caller must suppress that
old view when admission of a new context fails.

Keys include the expected source digest, frame count, sample rate, channel count,
relative media name and base resolution. Project root and opening epoch scope
reuse. Spare capacity retains recently used offscreen results; visible entries
are never evicted to load another visible entry. Immutable external borrowers
keep their own resource credit after eviction or shutdown.

Admission and lookup perform no media I/O or child launch. Control-thread `poll`
schedules one child at a time and accepts only a result matching the complete
current selection. Project replacement and explicit source invalidation cancel
old work and reject late completions. Faults require an explicit retry or changed
selection. File watching is not implemented: cached peaks describe previously
verified bytes, not a continuous guarantee about the file on disk. Normal GUI
shutdown must request shutdown and observe `closed` before destruction.

## Source geometry

Projection uses the existing rational source mapping, including fractional
offsets, project/source sample rates and clip playback rate. Returned sample
coverage is half-open and clamped to actual source frames. Only bins wholly
contained in that coverage are exposed as exact extrema. Cropped left/right bins
are flagged as needing finer data; no adjacent samples or silent padding are
silently included. The last short bin retains its real source extent.

The inverse mapping returns continuous clip-local positions for presentation.
It does not change project timing or audio processing. Peaks represent source PCM,
not clip gain, fades, EQ or the output signal. For a rendered stretch asset, the
current asset already contains the rendered PCM; its raw provenance anchor must
not be applied a second time.

## Verification and outstanding gates

Release and ASan/UBSan both pass 112,881 cache/projection checks and the existing
106 controller checks in 1.55 and 4.24 seconds respectively. The projection oracle
independently checks sample membership and full-bin coverage at five project
rates and four playback ratios, fractional offsets, all small-fixture intervals,
multiple levels and a large absolute clip position. Cache tests use actual child
workers and cover reuse, whole-view refusal, explicit retry, source mutation,
late old-project completion and borrower/resource retirement.

The preceding CI run `38806111667`, at `0fe6729`, failed. Its localization failures
identify an outdated UI source inventory; running real Qt `lupdate`/`lrelease`
refreshes that inventory without adding translated terms or promoting language
coverage. A separate Windows paired-crossfade timeout remains unexplained. The
native comp workflow passed. Those results do not qualify the new cache or
controller on Windows. Original CI evidence is retained before PR advancement.

See the [acceptance manifest](../tests/results/M2/2026-10-15-waveform-cache/manifest.json).
Next connect explicit project-root/epoch propagation, timer-driven scheduling,
channel-aware timeline drawing, async close gates and worker packaging. Fine
trim-edge/large-file resolution and native Windows rendering still need evidence.
Full DAW parity remains incomplete.
