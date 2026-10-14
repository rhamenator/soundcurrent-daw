# Original assembly history verification

Checkpoint 290, 2026-10-14. Full DAW goal remains active.

## Implemented

Historical review now loads the original source selection using the project's
recorded selection digest. It reconstructs an isolated verification session,
loads the independently recorded assembly request, checks original completion
and source media, recomputes complete output PCM, and compares reconstructed
playback provenance with the adopted history. Today's edited timeline is never
replaced by the old selection.

The move-only result retains its source owner and memory lease. It captures
original assets, paired histories, retained takes, selection references and track
layouts for subsequent metadata checks. Unique assemblies are visited through a
bounded iterative queue, including dependencies found in the original selection.
See [ADR 175](decisions/175-original-assembly-history-review.md).

Existing live-selection loaders retain their freshness checks. Snapshot loaders
are explicitly separate and grant no adoption authority. Version-one recipe
bytes remain unchanged. Repeated rendering is still gated.

## Qualification

The [source-bound evidence](../tests/results/M2/2026-10-14-comp-regeneration-history/index.json)
binds implementation `745c158594e5f212ec5465205d2cc8603b5ccc39`.
Three suites pass in 67.05 seconds:

- Six history reviews of actually adopted sources at 44.1, 48 and 96 kHz.
- 405,432 complete PCM sample comparisons across those six principal reviews;
  additional successful rechecks are exercised without inflating this count.
- 393 history assertions and 132 recorded refusals, covering altered/missing
  recipes, requests, completion records, raw/nominal/output files, exclusive
  writer ownership, stale dependencies, cancellation and resource bounds.
- Twelve layered planning cases and six retained legacy recipes remain valid.
  The additional layers are metadata fixtures, not newly rendered generations.
- Canonical project bytes remain unchanged, resource reservations are released,
  and all observed child processes retire. Existing fixture adoption still
  exercises Undo/Redo, save/reopen and exact live-core/offline-export agreement.

The acceptance archive contains 1,248 entries, 34,438,453 bytes, SHA-256
`9362a5d31a2cedf424227c45bd31946e409d37013d1ba37135fb7e83c1d01f30`.
Source and binary inputs are unchanged across qualification. No physical audio,
installed application, UI modification or VM operation was involved.

The first development run caught a fixture error: a shared reader lease was used
to simulate an active writer. The fixture now holds an exclusive existing lease;
the production verifier correctly refuses it. The original failure log and
fixture stderr are retained locally, separately from the fresh qualification.

## Windows correction

Original CI run 38646658520 at `0ead497` exposed MSVC's ambiguous overload when
comparing JSON directly with a string-view format constant. Both native Windows
builds stopped at that comparison. The implementation and affected assertions
now explicitly extract the string before comparing. New native confirmation is
required; Linux success does not establish Windows qualification.

## Next implementation

Integrate this original-file review with explicit sample-clock and accumulated
pitch handling in a backward-compatible span protocol. Carry all dependencies
through queued canonical adoption, then qualify repeated actual rendering,
mixed unchanged/generated pairs, retained takes backed by assembled artifacts,
Undo/Redo, reopening and unchanged PCM outside the edit.

This review provides integrity evidence at review time, not permanent file
authority or sound-quality admission. Total hard wall-clock isolation, independent
processing-quality gates, earlier cross-platform sample differences, native
installation, full European localization and the remaining suite milestones are
still open.
