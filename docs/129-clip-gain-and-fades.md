# Original clip gain and sample-defined fades

Date: 2026-10-09. SC-DAW-BASELINE-2026-10-05 unchanged.

## Usable workflow

Select a clip, expand **Clip gain and fades**, set gain/mute/polarity and either
fade window, then Apply. The group creates one Undo/Redo item. Save/reopen retains
exact canonical values; fields left untouched do not round stored doubles to their
six-decimal display. Controls use the selected number locale, signed frame fields
use explicit decimal integers, and an unfocused mouse wheel cannot change a spin
box while scrolling. Controls are read-only while audio is prepared or recording.
This is a structural edit workflow; immediate live clip automation remains required.

The existing scrollable window holds the expanded controls at 1100×850. The owned
offscreen Qt workflow exercises actual StudioWindow, project history, fake endpoint
preparation and save/reopen. It does not qualify an installed package or native I/O.
The reusable accelerating spin box remains the previously reviewed GPL snapshot;
no equalizer checkout or borrowed source snapshot was changed.

## Exact destination semantics

Schema **1.9** requires each clip's `processing`: `gainDb`, `muted`,
`polarityInverted`, `fadeIn` and `fadeOut`. Each fade stores signed `startFrame`,
`endFrame`, stable curve ID and dimensionless `shape`. Schemas1.0–1.8 explicitly
migrate to neutral clip processing. Unknown/missing/duplicate keys, wrong types,
nonfinite values, unknown curves and signed-duration overflow refuse. Gain is
admitted from −120 to +60 dB; shape from0.25 to4. These are current processor
admission limits, not proof of the reference suites' full control ranges.

Fade windows are clip-relative `[start,end)`; `0,0` disables a fade. Outside an
active window the phase clamps to its endpoint. For a window of N>1 frames,
`u=(frame-start)/(N-1)`; a one-frame window uses the midpoint0.5. Fade-out uses
`1-u`. Shape first maps phase to `u^shape`, followed by:

| Stable ID | Original gain function | Default-shape invariant |
|---|---|---|
| `linear` | u | Complementary overlapping envelopes sum to1 |
| `equal-power` | sin(πu/2) | Complementary squared gains sum to1 |
| `smoothstep` | u²(3−2u) | Smooth monotonic endpoint curve |

Gain/polarity multiply both envelopes. Overlapping fades multiply; matching
curve labels alone do not establish constant level for correlated sources or
arbitrary custom shapes. Float headroom is preserved; no limiter or normalization
was added. Abrupt structural boundaries and extreme/custom edits can still click.

Trim and split shift both anchors by consumed source frames, using checked signed
arithmetic and a strong exception guarantee. Anchors can lie outside the visible
clip; this preserves the original envelope instead of restarting it at a cut.
Moves/duplicates preserve processing. Raw media remains untouched. Today's clips
still use unity playback rate and matching asset/project rates; independent source
and project timing, resampling, stretch and pitch are separate work.

## Shared engine and ownership

`PreparedClipProcessing` is original C++20, framework independent, immutable and
stateless. Control-side construction validates settings and prepares scalar gain.
Sample evaluation uses fixed fields and math only: no allocation, disk access,
logging or blocking lock. Zero processor latency/tail; no new RT queue or owner.

The existing TrackReader evaluates each clip before overlap summation on the
serialized read-ahead/I/O owner. Both live mix playback and offline export consume
that same path. Applying gain after track summation would change overlapping clip
semantics. The neutral unity path preserves the prior addition behavior exactly.
Prepared bindings are charged to the existing resource ledger before allocation;
larger canonical Clip state is covered by existing sizeof/capacity accounting.
Native callbacks continue consuming prepared slabs and the existing EQ graph.

## Acceptance and known gaps

New owned tests cover independent analytic curve samples, monotonic bounds,
linear/equal-power invariants, signed/extreme anchors, singleton windows, strict
schema migrations/refusals, split/trim sample equality and transactional history.
Actual captured float media in1/2/8/32/256 channels is saved/reopened, rendered live
with37/511-frame callback partitions, independently compared sample by sample,
exported with127-frame blocks and freshly decoded. Live/export output is equal,
split output is unchanged, peak exceeds1 without clipping and original hashes
remain unchanged. RT guards cover gain evaluation and the playback callback;
Linux adds wrapped C allocation/free/lock checks. Windows C/lock instrumentation
is not inferred from Linux.

Local Release **12 selected tests pass**: core185303 checks and desktop63 checks
in the screenshot-enabled run. The first Qt fixture stopped because it observed
worker readiness before the UI's16ms refresh enabled Stop. It now waits for actual
canonical **and visible** state after Undo/Redo and preparation; the refusal test
and product guards are intact. The original failure/compile diagnostics are kept.
ASan/UBSan and exact-revision native Windows CI remain separately recorded gates.
See [evidence](../tests/results/M2/2026-10-09-clip-processing/).

P009 gains a concrete partial workflow; F/Q/C/N parity is **not promoted**. Reference
fade variants/defaults, interactive crossfade linking/handles, comp/group/warp,
live automation, new installed Linux/Windows previews and all-Europe language
qualification remain open. Catalogs now hold34 drafts/760 source keys/3135
non-English draft translations; new controls fall back to English where unfinished.
No catalog becomes a fully supported language through key extraction.

## Next concrete task

Separate source and project timing, admit qualified resampling and independent
rate/stretch/pitch, then build explicit import conversion/loss preview, grouped
acceptance with one Undo, Save/reopen and independently aligned source-suite
renders. Refresh installed Linux/Windows preview pairs after the resulting workflow
passes native gates. Candidate evaluation below does not select a dependency.

### Bounded dependency feasibility

| Candidate | License / primary evidence | Functionality and integration cost | Decision |
|---|---|---|---|
| libsamplerate0.2.2 | [BSD-2-Clause](https://libsndfile.github.io/libsamplerate/license.html); [streaming API](https://libsndfile.github.io/libsamplerate/api_full.html) | Resampling; consumed/generated frames, flushing, seek/reset, fractional phase and output alignment need explicit ownership. A playback-rate change couples duration and pitch. Official inspected head0844c208 (2026-08-13) indicates activity beyond the2021 release. | Candidate only; pin source/build/transitives and qualify all rates/layouts, latency, quality and Windows before adoption. |
| Rubber Band4.0.0 | [GPL-2.0-or-later or commercial](https://breakfastquay.com/rubberband/license.html); [integration](https://breakfastquay.com/rubberband/integration.html) | Independent time/pitch; variable output, pad/delay/drain and scheduled ratio changes. Offline/real-time modes require explicit shared-render decisions. Documented8–192kHz scope does not cover the entire existing project-rate range. FFT/resampler licenses/configuration need audit. | Candidate only; no API name establishes Q-STRETCH/Q-PITCH. |
| soxr0.1.3 | [LGPL-2.1-or-later](https://raw.githubusercontent.com/chirlu/soxr/master/LICENCE); [official source](https://github.com/chirlu/soxr) | Constant/variable ratio resampling, filter/phase and FFT delay; no independent time/pitch. Inspected official head945b592b (2018-02-24) is a maintenance risk requiring review. | Candidate only; source/transitive/quality/platform work remains. |

An original0.70s offline probe used the already installed, hash-identified Linux
libsamplerate0.2.2 binary: mono48→44.1kHz, four one-second tones,2.5 float headroom
and three streaming partitions.4883 checks pass; at1kHz the partitions match the
whole-buffer output exactly. The measured23kHz alias line at21.1kHz is approximately
−163.7dB; this is one line, not full-band alias qualification. No Windows execution,
all-rate/layout/deadline/seek/tail qualification, production dependency adoption,
system install, VM or device audio is claimed. The owned probe and exact binary/
script hashes are retained with this checkpoint.

## Sanitizer cohort

ASan/UBSan7 selected tests pass in25.54s, with leak detection disabled; this is
not an LSan qualification. The current core185303-check processor and desktop
workflow pass alongside actual session state, history, import persistence, timeline
and offline export. No runtime sanitizer report occurred in the retained logs.
Native Windows/exact-source hosted gates and installed previews remain separate.

## First hosted cohort and configured large-project admission

Head b1a4c3025cf385956379f3a871aa94a968582b39 / run37924685019:
Windows cross-build and native core28/native Qt8 pass. All three artifact
archive digests and raw logs are retained. Linux passed91/92; the8192-track
viewport fixture failed its default64MiB conservative canonical decoding grant
after schema1.9 added more JSON objects per clip. This is retained failure
evidence, not all-green qualification. The processor185303 checks and Qt
workflow passed on both platforms in that cohort.

The fixture now explicitly declares a trusted96MiB state grant for its unchanged
8192-track workload, checks that the default64MiB still refuses before adoption,
and uses that declared grant through Open and Save/reopen. Production defaults,
quotas, track count, paint/hit/identity/history/raw-media assertions and strict
refusal remain intact. GUI configuration of additional admission envelopes stays
a separate X006 product task. Local affected Release3 tests pass in7.17s.
New native/full Linux qualification is required for the revised test source.

A focused-Undo exploratory check passed on the existing actual StudioWindow path;
no suspected focus defect was reproduced and no production refresh change was
made. The stronger focus case remains in acceptance. The retained log named
`focused-undo-original-failure` contains its actual passing result; its filename
does not change that outcome.
