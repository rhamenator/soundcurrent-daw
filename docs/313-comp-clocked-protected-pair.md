# Clocked protected comp pairs and application preparation

Date: 2026-10-15. Source qualification: `c66f184`.

## Delivered scope

Paired comp selections now have an explicit `ClockedProtected` policy and the
versioned manifest `sc-comp-pair-selection-clocked-protected-v1`. Protected children
use the clocked original-geometry worker; uniform children retain their existing
protocol, and retained assembled media requires no new child. Saving and reopening
a selection reconstructs the selected policy and verifies exact request bytes.
Legacy manifests retain their original identities and rendering paths.

The application preparation service selects the new policy from original source
rates, fractional maps, active speed and retained baked-clock metadata. It does
this before publishing request sidecars. Unsupported input fails through the
selected adapter; rendering failure never switches algorithms or drops protection.
Preparation owns its resource reservations and leaves canonical session/project
state unchanged. Existing worker, publication and adoption controls still govern
execution and application.

## Acceptance workflows

Two focused banks each execute three real paired edits against published ancestry:

1. Fractional source timing and 3/2 playback speed, rendered after sample-rate
   conversion into the project domain.
2. Another fractional crop at 2/3 speed, retaining the independently expected 1.5
   baked pitch and both original semantic anchors outside the current crop.
3. One retained assembled take and one regenerated incoming side, including refusal
   when requested handles exceed the retained take.

One bank uses protected material on both sides. The other combines protected and
uniform material. Both enter through the application preparation service and then
exercise saved pair recovery, actual workers, assembly publication, adoption,
queued stale-apply refusal, Undo/Redo and save/reopen. Candidate PCM is compared
sample by sample with original-geometry recomputation. Audio outside the changed
region is checked, and shared live rendering is compared with exported float WAV.
Synthetic gain above unity checks retained headroom without playing physical audio.

The protected bank also checks cancellation and changed historical completion
after worker readiness, plus changed history immediately before assembly completion.
Neither refused worker may publish completion. Resource ownership is checked after
scopes exit. These are application-service/core workflows; repeated post-SRC Qt interactions
and native Windows execution of the new path remain separate pending gates.

## Qualification

Five Release suites pass in 120.80 seconds and five Debug suites
pass in 334.33 seconds, including legacy protected/uniform pair
recovery and the existing clocked uniform worker. Source and executable hashes
remain unchanged throughout each run. Three changed C++ translation units pass
MinGW syntax checks; this is not native runtime qualification.

Per configuration, the new banks complete six adoptions, ten successful workers,
two required worker refusals and two retained-take mixed cases. They compare
665,448 worker PCM samples, 275,632 samples outside the edited region and
295,920 live/export samples. Independent file/data hash, frame-count and peak
checks pass. All ten new worker files and six export files are byte-identical
between Debug and Release.

[Portable evidence](../tests/results/M2/2026-10-15-comp-clocked-protected-pair/index.json)
retains commands, exact source/executable hashes, process outcomes, project state,
original audio, failure probes and independent audits. Native CI adds the two
new paired banks alongside the preceding worker banks. Existing jobs, deadlines,
log preservation and artifact paths are retained.

A focused Release desktop suite passes in 24.37 seconds at `e07cccc`.
Its twelve cases now include six adoptions and six failure/cancel/close cases;
eleven successful span children retire. Five legacy exports match the prior corpus.
One formerly rejected fractional outer-handle edit now completes review, Apply,
Undo/Redo, save/reopen and export using the new policy. The original test failure
is retained: it expected rejection after both workers and the assembly had already
completed successfully. Only the two desktop test files changed after core
qualification; engine source is identical. Twenty-three screenshots are retained;
the new fractional review and applied views were inspected. Repeated post-SRC
history through the actual desktop and native execution remain pending.

CI scheduling covers all 216 tests exactly once in three 72-test shards, using
212 original observed timings and four explicit estimates. The local build has
four experimental banks disabled; scheduling coverage uses the retained original
CI inventory rather than claiming that local configuration exercises those banks.

## Retained limitations and probe corrections

The first fixture crop already had all required handles, so both sides used
existing media and the assembly worker grant correctly refused zero children.
Moving the fixture to actual media edges exercises the intended regeneration path.

A short uniform second-generation span (4,054 processing frames, baked pitch 1.5)
hit the existing backend analysis-window refusal. The uniform fixture was extended
to cover a supported window; the refusal, original fixture and logs remain retained.
Support for that short uniform window remains an explicit gap. No backend minimum,
worker memory limit, watchdog or existing test deadline was relaxed.

Coverage is synthetic stereo at 48 kHz with the documented fractional clocks.
It does not establish arbitrary layouts, perceptual quality, all possible histories,
the full desktop history matrix, installed/native qualification or full DAW parity.

## Next implementation task

Extend actual Qt main-window coverage to repeated post-SRC comp preparation, preview,
apply/cancel, Undo/Redo, save/reopen and export. Qualify that workflow on Linux and
native Windows before updating the usable installed preview. Keep the short uniform
analysis-window gap and broader quality/material/layout coverage visible.
