# Exact uniform comp span rendering

Checkpoint 273 implements an off-audio rendering library for uniform comp maps,
including nonunity duration, independent pitch, formant option and fractional raw
origin/endpoints. It produces physical candidate PCM through bounded callbacks.
Owned process/request/completion integration, nonuniform rendering, curve review,
post-SRC state/execution, adoption and full sound-quality qualification remain open.
The project schema, old unity verifier and installed previews are unchanged.

## Exact geometry and processing

`prepareCompSpan` accepts an immutable handle envelope. It checks every map
interval against the exact whole-span slope; nonuniform maps are explicitly
refused rather than flattened. These maps remain a required producer path.
The processing grid covers the exact raw span with `ceil(span)` input frames.
Canonical rational comparisons preserve both endpoints, including fractions, and
actual-source sample bounds are checked. Nominal anchors and clip playback speed
are preserved; playback speed is not reinterpreted as the independent pitch.

`renderCompSpan` uses the existing positioned kernel for raw-grid preparation and
R3 offline study/process/drain for duration and pitch. Its backend pitch factor is
`2^(pitchMilliCents / 1200000) / processingGridStep`, correcting the grid's effect
on raw frequency. Exact unity geometry with zero pitch uses positioned copying.
Mono/stereo channels use together processing; larger layouts use the existing
apart policy. Qualified rendering coverage here is stereo, not broad channel proof.

Explicit slabs and retained metadata use ledger credit. Raw reads are poisoned
before callbacks so incomplete/nonfinite input is refused. Cancellation/deadline
polling remains active between reads, study, processing, drain and output. Exact
output duration is required: no padding, truncation or repair. Short inputs below
the prepared backend window fail before writes. Writer failure yields no completed
report; an owned worker must retain/clean partial output through its job protocol.
R3's own allocations require a containing process ceiling in that future worker;
the ledger here does not claim exact third-party heap/RSS admission.

The renderer allocates, calls I/O and invokes R3 off audio/GUI. It is not a live
callback processor. The existing pinned R3 dependency is reused; no new runtime
library or proprietary algorithm/assets are introduced.

## Source-bound actual rendering

[Frozen receipt](../tests/results/M2/2026-10-14-comp-span-render/index.json) binds
source `9df4f4d8ba7d9f3464276d3ab4fa84b6dc394d58`, source/library/executable hashes,
CTest PID 3817908, actual renderer-test child PID 3818003 and independent analyzer
PID 3818036. Three selected Linux gates pass in 8.98 seconds: actual uniform spans,
exact processing grids and the independent rational timing oracle. The span test
records 2,948 checks and 36 fresh R3 renders. Older qualified banks remain intact.

The stereo corpus crosses raw rates 44.1/48/96 kHz, duration ratios 1/2 and 3/2,
pitches -1200/0/+700.007 cents, and both formant modes. Fractional raw origin/end
and independent clip speed 3/2 are retained. Duration/finiteness, exact endpoints,
headroom, copy path, short-span/nonuniform refusal, incomplete reads, cancellation,
writer failure, tiny admission and ownership retirement are exercised. No actual
filesystem source-media admission, worker completion or model edit is claimed by
this callback-based test.

Independent NumPy analysis checks the exact grid using rational arithmetic and
recomputes PCM lengths, peaks, pitch compensation and spectral observations. The
maximum measured fundamental error is 0.00505348 (0.506%), within the retained 1%
experimental fundamental guard. This is not the full processing-quality target.

An earlier zero-crossing diagnostic failed on formant-preserved waveforms that
contained additional spectral components. An independent FFT inspection of those
actual failed-run PCM files identified the requested fundamental and strong
additional peaks. The diagnostic was corrected to measure the fundamental
spectrally, with its amplitude checked against the largest peak. The original
failed scopes and diagnostic are retained. Independent final analysis records
three channel/case observations with a dominant peak outside the fundamental
guard. Formant, phase and overall quality flags stay false. This change does not
waive those artifact observations or establish waveform parity from pitch alone.

All raw/output PCM, report, independent analysis, logs and process/hash receipts
are archived: 6,386,281 bytes, 50 entries, SHA-256
`e2822d4e7483ed7b79c46eaaae0c991fabb2a5210ae5feccfab8ea303d2b0fa8`.
The dedicated Release build uses one low-priority worker. Final new code builds
without warnings. No GUI source changes; localization check remains 1,038 keys,
34 partial catalogs, 3,168 drafts and zero reviewed/fully UI-qualified languages.

## Corrected native Windows predecessor

[Original run 38598128078](https://github.com/rhamenator/soundcurrent-daw/actions/runs/38598128078)
at `92e5d1b` passes all four jobs: Linux 175, Windows core cross-build, native
Windows 93 plus seven explicit core/recipe/media gates, and Windows desktop 22.
This confirms the recipe compiler correction and executes the actual-file review
on Windows. The previous MSVC failure remains retained.

[Retained original evidence](../tests/results/M2/2026-10-14-comp-span-render/pr194-media-ci/index.json)
contains terminal APIs/full logs/five original artifact digests. Original ZIPs are
retained locally before head update. Independent build-input audit verifies 14
payload files, 1,574 corresponding source files with actual Windows checkout
filters, all collection hashes and both pinned media DLL builds. Main-window,
physical/native audio and installer flags remain false. The new span renderer's
native execution still needs its own original CI run. Future CI now retains span
and media-review evidence directories explicitly, alongside the existing logs.

## Next implementation

Connect this exact-grid renderer to owned span requests/intents/readiness/completion
and executable/recipe binding, with raw/current/retained admission and independent
alignment/curve review. Implement nonuniform/protected comp maps without flattening
or rounding their timing. Add the post-SRC overlay or explicit representation
shared by live playback and offline rendering; preserve originals/nominal anchors,
paired Undo, root/epoch staleness and cancellation/close/reopen/recovery.

Broader channel/rate/container/group/native coverage, controller/project/UI work,
formant/protection/phase/transient/listening quality and the mixed earlier join
artifacts remain open. All hard frozen M2–M11/F/Q/C/N/X004/X005, installed Windows
and all-Europe requirements remain active and incomplete.
