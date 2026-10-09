# Prepared resampling and exact source/project coordinates

Date: 2026-10-09. Frozen baseline remains **SC-DAW-BASELINE-2026-10-05**:
Reference A 6.1.3 and Reference B 15.0.30. F/Q/C/N parity remains incomplete.

Previous goal turn was **progress**: PR73 merged exact qualified1849bde into
2882f11 after all four unchanged protected contexts passed. Linux92, native
Windows core28 and Qt8; the actual focused Undo repair passed. Three archives,
20 source hashes and87 committed evidence hashes were verified; tested and merged
trees agree. Its final receipt is carried into this checkpoint. Old b1a4c30 local
preview lacks the focused refresh repair; no new installed preview is inferred.

## Concrete implemented component

`SourceFrameMap` expresses source and project frames separately using a reduced
integer ratio and exact source fractional positions. Quotient/remainder arithmetic
avoids overflowing frame products and floating-point frame accumulation. Advancing
an origin preserves its fraction across mathematical splits. Signed/rate/extent
arithmetic is checked, including offsets beyond2^53 and near INT64_MAX.

`PreparedResampler` is an original framework-independent C++20 adapter with the
stable machine algorithm ID `soundcurrent.src-best-aligned-v1`. The exact pinned
libsamplerate best-sinc kernel performs fixed sample-rate conversion. Equal rates
copy samples exactly, without creating a sinc state. Interleaved input remains
unchanged and float headroom is retained. Counters separate accepted/consumed owned
input and emitted output. Failed processing latches failure; retire rather than
retry a partly advanced converter. Invalid input/admission refuses before mutation.

Input/output blocks are1..65536 frames and layouts1..256 channels, matching the
current DAW admission range. Prepare reserves its optional shared ResourceLedger
lease before allocating. Best-sinc has a conservative512KiB/channel private-buffer
allowance plus8192 bytes and the admitted input buffer; equal-rate copies omit that
allowance. These are payload grants, not allocator/RSS measurements. Static shared
coefficient code/data and runtime libraries are not charged per converter.

### Timing, drain and ownership

Output is the explicit interval **[0,ceil(N * projectRate/sourceRate))** for N owned
source frames. Upstream0.2.2 mono and multichannel terminal conditions differ; the
first actual test retained mono940/stereo941 for1024 source frames at48→44.1kHz.
The adapter supplies bounded virtual zero context and stops at the exact rational
end. It emits the actual filtered sample at that final coordinate, rather than
loosening the channel assertion or padding the returned output with arbitrary
samples. Virtual source zeros never count as owned input or extend the output domain.

The pinned filter's conservative source look-behind/look-ahead is declared by
`resamplingSourceContextFrames`, with caller batching separate. The observed first
output is checked against that bound plus the configured input block. Logical
output starts at source coordinate0; it has no unexplained timestamp shift. This
is not a zero-latency claim for live input. Streaming availability requires source
look-ahead; a live-input wrapper must account for it in monitoring/record alignment
and PDC. The current closed interval excludes residual samples outside its domain;
processor tail policy for general graph use is still an integration decision.

One serialized worker owns mutable converter state. Preparation/retirement and
resource lease operations stay off RT. No file, GUI, backend or Session access is
inside this component. Happy-path push/pull C++ allocation/free guards pass; Linux
also wraps C allocation/free and blocking mutex calls. This evidence does not
make the exception-reporting worker API suitable for direct real-time callbacks.
Windows C/lock instrumentation is a separate gate.

## Dependency decision and exact provenance

[Official release0.2.2](https://github.com/libsndfile/libsamplerate/releases/tag/0.2.2),
[BSD-2-Clause license](https://libsndfile.github.io/libsamplerate/license.html),
[full streaming API](https://libsndfile.github.io/libsamplerate/api_full.html).
The official tag resolves to c96f5e3de9c4488f4e6c97f59f5245f22fda22f7
(2021-09-05). GitHub reports a verified annotated tag; local PGP verification is
not claimed. Archive SHA256:
`16e881487f184250deb4fcb60432d7556ab12cb58caea71ef23960aec6c0405a`.
All96 files/10,352,083 bytes are unmodified and hash-inventoried with original
copyright/license in `third_party/libsamplerate` and the manifest.

BSD-2-Clause describes the compiled kernel/public API. The complete source
includes unused GPL-3.0-or-later and GPL-2.0-or-later Autoconf macros with their
original configure-output exceptions, FSFAP macros, a historical permissive
`clip_mode.m4` notice and Wenzel Jakob's BSD-2-Clause FFTW discovery helper.
The manifest's `additional_source_notices` and THIRD-PARTY-NOTICES record exact
paths and retained notices. These helpers are not used in the selected build;
FFTW is not linked. No blanket BSD license is inferred for those source files.

Scoped CMake builds static best-sinc, disables fast/medium variants and upstream
examples/tests/install rules. CMP0077 NEW ensures normal option values are honored;
the original ignored-option build is retained. Upstream's supported CONFIG_CHAN_NR
is256 because its128 default failed the existing256-channel gate; original source
is unchanged. Root MinGW now selects its C compiler explicitly; native MSVC CI
pins both C and C++ compilers. The upstream C sources are actually ASan/UBSan
instrumented in the sanitizer cohort. There are no codec/FFT dependencies beyond
platform C runtime/math. Final binary notices/source delivery remain release gates;
the desktop does not yet link this component.

The selection is scoped to this prepared component. Checkpoint129 evaluates
Rubber Band4.0.0 and soxr0.1.3 against functionality, licensing, maintenance and
integration cost. libsamplerate has a stable2021 release and inspected maintenance
activity in2026, but that is not a support guarantee. It does not provide independent
pitch/stretch. Neither alternative is silently excluded from future qualification.

## Acceptance and remaining gaps

Owned actual C++ tests cover121 rate pairs across8..384kHz representative rates,
whole/37/211-frame input partitions,64/256/511-frame output partitions and9 layouts
(1/2/3/4/6/8/32/128/256). Mono/channel samples agree, all rate-pair durations match
the exact rational ceiling, neutral copies are exact, headroom exceeds1, grants
retire, refusals preserve ownership, and non-finite processing latches failure.
The independent Python arbitrary-precision oracle checks324 actual C++ mapping
cases, including overflow refusals beyond floating integer precision.

At48→44.1kHz, one1kHz interior analytic comparison has max error3.48573e-7;
one23kHz→21.1kHz alias projection is6.52547e-9. These points are not full Q-RESAMPLE,
full-band/listening/deadline/latency or frozen-reference quality parity. Local
Release and ASan/UBSan pass their two selected tests; final scoped receipts record
exact counts/times. Leak detection is disabled. New native Windows/source-head
qualification is recorded separately, never inferred from cross compilation.

**Not yet implemented by this increment:** Session source/project timing state,
arbitrary imported fractional origins, decoder context/seek checkpoints, tempo/rate
changes, independent pitch/stretch, UI controls, Save/reopen of these settings,
import conversion/adoption or live/offline project resampling. Existing schema1.9
and matching-rate reader behavior stay intact. No VM, physical audio route, system
install, equalizer checkout or public product release changed. No language support
or F/Q/C/N axis is promoted by a component name.

## Next implementation task and exit criteria

1. Version clip source/project timing and processor identity; migrate neutral1.9
   state explicitly. Define source extent, project duration, source fractional
   origin and fade-anchor domains without relabeling imported properties converted.
2. Integrate the same resampling owner into shared read-ahead/offline rendering,
   with admitted cache/filter contexts, source coordinates and safe retirement.
3. Preserve actual samples across split/trim/seek using qualified phase/history
   restoration; do not approximate fractional starts with integer decoder seeks.
4. Qualify constant rate and independent pitch/stretch with explicit dependency,
   latency/tail/quality decisions. Keep those frozen workflows in scope.
5. Actual editable desktop workflow, one Undo/Redo, Save/reopen, typed conversion/
   loss preview and aligned originating-suite render comparisons. Raw hashes remain
   unchanged. Then refresh paired installed Linux/Windows workflow previews.

Detailed integration evidence is required before P009 or X004 claims conversion.
