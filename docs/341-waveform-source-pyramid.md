# Bounded source waveform preparation

Checkpoint 341 adds the framework-independent `sc-waveform` library as the first
layer of timeline waveform display. The installed previews remain unchanged.
**The timeline does not draw waveforms yet.** Worker transport, asynchronous GUI
control, cache invalidation and viewport drawing are the next implementation work.

## Source identity and representation

`buildApprovedWaveform` consumes an already approved, pinned media file and an
expected SHA-256, frame count, sample rate and channel count. It uses the existing
WAVE validation implementation; no new library, proprietary content or algorithm
is introduced. The new implementation is original GPL-3.0-only code.

The returned immutable result contains per-channel double-precision minimum and
maximum values. Base bins cover a caller-selected number of source frames, and
each subsequent level combines adjacent bins until one remains. Odd tail bins
retain only their actual frames; neither empty padding nor an implicit zero can
change unipolar extrema. The API returns each bin's exact half-open source-frame
interval. Floating-point values above unity and precision beyond float32 survive.

This data describes **source samples**. It does not claim to describe samples after
resampling, clip fades/gain, EQ or track mixing. A view trimmed inside a coarse bin
must obtain finer data before claiming exact extrema for that subrange. Clip source
offsets, fractional timing, playback rate and channel layout mapping belong in the
subsequent viewport request and drawing layer; they are not silently ignored by a
purported complete visual editor.

## Admission, validation and failure behavior

The trusted policy declares base resolution, maximum base-bin count and retained
payload bytes. Resolution is not silently reduced to make an over-budget request
pass. Checked arithmetic admits the complete pyramid before allocation, and one
move-only resource lease owns its sample storage and fixed metadata. There is no
full-source PCM allocation. Decoder and input buffers use the existing media ledger.
These are owned-payload budgets, not an allocator-overhead or RSS guarantee.

The pinned source must pass full decoding, finite-sample validation, expected
format/digest comparison and final unchanged-file verification before a result is
returned. Cancellation and exceptions discard the entire partial result and release
its credit. No partially verified peaks escape through an observer or progress
callback. Moved-from results expose no levels; invalid level/bin/channel queries
fail explicitly.

Header preflight and full WAVE validation each use the existing validation policy;
their I/O counters are separate. Cancellation is cooperative at read/reduction
boundaries. OS I/O and third-party decoding cannot promise a hard allocation or
time bound. The GUI integration therefore needs a child process with a deadline,
bounded result parsing and explicit retirement. No decoding may run in painting or
an audio callback. Epoch and media-identity checks must reject stale worker results.

## Verification

The new suite compares every bin and channel against an independent direct scan
of generated source samples, rather than reproducing the pairwise pyramid reducer.
It covers 1, 2, 8, 257 and 1,024 channels; base widths 1, 3, 7 and 64; bipolar
impulses; unipolar bins; headroom; beyond-float32 precision; incomplete tails; empty
files; movement and coordinate refusal. Failure cases cover wrong digest/format,
bin/payload/ledger limits, cancellation before/during/after streaming, callback
exceptions, non-finite final samples and resource release on every path.

On Linux, a mutation immediately before final publication is refused. That mutation
case is excluded on Windows, whose approved handle denies write sharing; native
Windows qualification of this new suite is still pending.

Release and ASan/UBSan each pass both focused suites:

- **164,507 waveform checks**.
- **20,956 existing pinned-WAVE checks**.
- Release CTest: **0.05 seconds**; ASan/UBSan CTest: **0.38 seconds**.
- Sanitizer configuration enables leak detection and stops on sanitizer errors.

The Windows native CI build target list and recording test bank include the new
suite. This schedules verification; it is not evidence that Windows execution has
already passed. No VM or physical audio route was used locally.

See the [evidence manifest](../tests/results/M2/2026-10-15-waveform-source-pyramid/manifest.json)
for exact source and binary hashes, build/test commands and logs. Full M2 visual
editing, performance, platform qualification and the full DAW goal remain open.
