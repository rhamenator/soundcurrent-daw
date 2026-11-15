# Original geometry and retained clocks for protected comp renders

2026-10-15. Component qualification; full DAW goal remains incomplete.

## Implemented boundary

`CompClockedProtectedSpanPlan` owns the original raw asset metadata, full protected
warp geometry, prepared sampling and resource credits. It survives destruction of
the input envelope and refuses access through a moved-from plan. The renderer reads
absolute original raw coordinates through bounded callbacks. It does not read a
previously assembled candidate as though it were original audio.

The adapter starts from the original nominal clip. It follows the current asset's
own ancestor chain, extends the first/last original gap slopes to cover those full
maps and the new candidate, and retains every original anchor and protection
window. A paired sibling does not contribute geometry. This matters after cropping:
an artifact can omit both original anchors while still depending on their full
analysis windows. Rebuilding only from the cropped endpoints would change the DSP.

An affine sampling clock is recovered and proved against the union of the full
geometry's knots and each retained map's knots. Matching endpoints alone is
insufficient. Noncanonical, collapsed, reversed, altered and incomplete maps are
refused. Original nominal knots are checked after extension. The candidate clock
must agree with raw/output sample rates and the history's baked varispeed factor.
The new processor identity is `soundcurrent.comp-span-protected-clocked-v1`.
Original protected pitch modification and context remain explicit refusals until a
separate producer supports them. Existing worker identities and output are intact.

All preparation is off the real-time thread. Plan, marker and ancestor storage use
the existing resource ledger; preparation polls cancellation and releases credits
on failure. The caller still owns file verification, isolated vendor memory/CPU
limits and discard-on-failure publication. This metadata proof grants no authority
to adopt media into a project.

## Evidence

Qualified at clean implementation `2a7cb853a9371c598b8f949b2ae8ff38ed846ff7`:

- Four suites pass in Release (10.13 s), Debug
  (35.56 s) and optimized Debug with ASan/UBSan and leak
  checking (66.52 s). Individual deadlines are unchanged.
- Each configuration renders nine candidates at 44.1, 48 and 96 kHz, checks 27,638
  conditions and preserves twelve original anchors outside the retained crops.
- An independent file audit verifies all 21,384 crop samples against earlier
  same-phase renders of independently constructed original geometry. Every raw,
  candidate and reference PCM file is identical across all three builds.
- Baked pitch factors 1.5 and 1 produce the expected 316.5 and 211 Hz tones. Maximum
  measured error is 0.0007461 Hz, within the original one-percent gate; internal
  floating-point headroom is preserved.
- The independent Python Fraction oracle proves 45 cases (40 accepted, five
  refused), with 165 exact assertions. All three response corpora match.
- Cancellation after allocation, insufficient credit, moved plans, envelope
  destruction and altered/omitted map knots exercise refusal and cleanup.
- Both new C++ translation units pass MinGW syntax checks. Native execution of
  this new adapter remains pending.

[Portable local evidence](../tests/results/M2/2026-10-15-comp-clocked-protected-span/index.json)
contains source/binary hashes, commands, original logs, PCM, oracle data and audits.


An initial test compared unreduced rational representations instead of their exact
values. The diagnostic showed equal fractions at 44.1/48 kHz. Canonicalizing the
assertion corrected that test; production arithmetic and numerical tolerances did
not change. The initial failures, diagnostic fixture and successful results are
retained in the evidence archive.

## Original preceding CI

[Original CI 38704202830](https://github.com/rhamenator/soundcurrent-daw/actions/runs/38704202830)
passes all ten jobs at `a042d2aa15b0c11aa3090567d06ef00ae3c93e71`. All ten full
logs, terminal APIs, nine original ZIPs and 214 PNGs are retained and audited before
advancing the draft PR. This qualifies the preceding exact-boundary component;
native execution of the adapter introduced here is a separate pending run.

- Linux: 208 suites in disjoint 69/70/69 shards, with CTest times of
  596.14 / 739.34 / 681.74 seconds. All archive and aggregate gates pass.
- Native Windows: 32 comp suites and 93 other suites pass. The exact-boundary bank
  records 31,686 checks, 1,412 crop samples and 2,688 protected samples with all
  credits released. Its independent 48-case/1,368-assertion oracle matches all
  three Linux builds. Prior wide-grid and core-sampling oracle corpora also match.
- Windows desktop: 22/6/1 workflow banks pass. Repeated protected transitions
  retain nine adoptions, three mixed cases, fifteen reaped workers and five
  failure/close cases. All fourteen stage observations meet the original bound,
  maximum 7,763 ms. Nine WAV exports match current and prior Windows core outputs.
  Actual project credits retire; the current Review screenshot was inspected.
- Source/deployment audit verifies fifteen payloads and 1,760 corresponding-source
  files using actual Windows checkout filters.

[Portable original-CI review](../tests/results/M2/2026-10-15-comp-clocked-protected-span/original-ci-index.json)
retains the scoped native corpora, original logs, oracle responses and audit tools.
Scheduling uses 208 observed CI durations plus explicit Debug-derived estimates
for the four new suites: 212 tests assigned exactly once across 71/71/70 shards.
All original jobs, required checks, test/job deadlines and evidence paths remain.


## Next application task

Add a versioned worker request and completion identity for this owned plan. Bind
original raw media, original nominal anchors, complete own ancestry, rates, baked
pitch and algorithm version to the submitted snapshot. Revalidate those bindings
in the worker before reading media and again before publication/adoption. Cover
changed media/history, cancellation, crash and partial output before integrating
paired adoption, Undo/Redo, save/reopen, live/export and both desktop platforms.
Keep the legacy protected processor available for old projects and reproducibility.

These fixtures use synthetic mono audio and metadata-only history construction.
They qualify actual candidate rendering, not a file-verified history workflow,
installed preview, perceptual quality or general material/channel-layout coverage.
The prior Windows/Linux 44.1 kHz comparison remains a separate unresolved question.
