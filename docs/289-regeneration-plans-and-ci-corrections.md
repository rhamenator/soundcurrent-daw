# Repeated-edit planning and desktop corrections

Checkpoint 289, 2026-10-14. The full frozen professional DAW goal remains active.

## Repeated-edit planning

Handle planning now uses the original-source resolver for adopted assemblies.
It extends the current artifact's exact raw map within retained-source bounds,
preserves original nominal processing separately, and retains the full dependency
closure for stale checks and resource accounting.

New v2 recipes serialize the paired history, assets, selection references and
historical retained takes needed to reconstruct these plans. They bind the
original nominal identity and accumulated pitch/clock ratios. First-generation
v1 recipes retain their canonical bytes. Six exact requests saved before these
changes are included as small, provenance-linked compatibility fixtures in the
corresponding source tree. [ADR 174](decisions/174-regeneration-plans-and-recipes.md)
records the design.

The producer remains explicitly gated for repeated rendering. This applies to
mixed pairs too: an unchanged assembled side cannot bypass historical-file
verification merely because only the other side needs rendering. Actual repeated
production, historical job verification, adoption and export remain required.

## Qualification

The [engine qualification](../tests/results/M2/2026-10-14-comp-regeneration-plans/index.json)
binds source `db1f8be7e80630b1cd84374a1b477e50697222b1`. Five suites passed in
53.96 seconds, including actual first-generation transition creation. The source
resolver/recipe corpus reports 124,147 assertions, twelve layered recipe cases,
six retained legacy recipes and mixed-pair refusal at three sample rates.
Exact timing survives construction of fresh playback-source declarations and
the existing independent canonical schema validation. Stale dependency changes,
canonical ordering, memory retirement and byte-limit refusal are covered.

The additional generations are metadata cases, not newly rendered recordings.
Existing first-generation workers, adoption, Undo/Redo, save/reopen and export
continue to pass. The uniform span-render regression also passed during development.

## Original CI findings and corrections

[Original run 38642154345](https://github.com/rhamenator/soundcurrent-daw/actions/runs/38642154345)
finished with both core jobs passing and two desktop failures. All four original
artifact ZIPs, all 24 PNGs, full logs and terminal APIs were retained before any
PR update. The [audit](../tests/results/M2/2026-10-14-comp-regeneration-plans/original-ci/audit.json)
records the individual workflow evidence and limitations.

- Linux passed the new transition workflow but failed the older narrow crossfade
  dialog test: a 350-pixel dialog had an 11-pixel horizontal scroll range. Option
  captions now use wrapping form labels with explicit checkbox accessible names.
- Windows failed the new test's missing-helper case because `path.string()` tried
  to convert a Unicode project path through the legacy code page. Test paths now
  use explicit UTF-8 conversion, including screenshot destinations.
- The Windows failure collector subsequently rejected an unrelated, non-UTF-8
  diagnostic in the combined CTest log. It now identifies its ASCII marker prefix
  before strictly decoding a path. Invalid marker paths still fail. The original
  mixed-encoding log was processed successfully without changing its bytes.

Windows completed the 44.1 kHz transition report, but not the complete 48/96 kHz
corpus. Its deployment bundle was skipped after the failure. The corrected
native run and fifteen-file deployment-bundle audit remain due. Existing Apply
and controller workflows passed on both platforms; that does not qualify the
incomplete new Windows transition workflow.

## Corrected layout evidence

The [layout qualification](../tests/results/M2/2026-10-14-comp-regeneration-plans/layout-index.json)
binds source `8cb51fd752f151b887fe661040337c825414cb72` and verifies that the
qualified engine sources and binaries remain unchanged. Both the crossfade and
transition-creation suites pass; the latter exercises eight cases and 180
assertions. A separate DejaVu Sans 12-point run passes the 350-by-240 dialog check.
Default and narrow screenshots were inspected. Captions wrap, help remains
readable with vertical scrolling, and no horizontal scrollbar is needed.
The three reopened WAVs exactly match the engine qualification.

Actual Qt localization update and check passed after the production UI edit:
1,090 source keys, 34 partial catalogs and 3,168 draft translations. Native review
and full localized UI qualification remain open. These are offscreen Linux tests,
not new installed-application, native Windows or physical-audio qualification.

## Next work

Integrate explicit sample-clock and pitch binding into the versioned span
producer, verify original nominal media and historical owned assemblies, and
carry those dependencies through queued adoption. Qualify multiple real
generations with Apply/Undo/Redo/save/reopen/export and unchanged PCM outside the
edit before enabling the repeated-rendering workflow. Also complete planning for
retained takes that themselves reference assembled artifacts.

Nonuniform/protected production, independent sound-quality gates, previously
observed cross-platform sample differences, native installation, European
localization and the remaining full-suite milestones remain open.
