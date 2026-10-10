# Bounded pages for retained render recovery

This M2 continuation adds an explicit **Next page** workflow to retained
pitch/stretch review. It does not complete M2 or the frozen reference goal.

## Workflow

Scan begins a fresh inventory against one immutable project snapshot. Each page
examines at most128 directory entries and8MiB of manifest bytes, with the existing
512KiB per-manifest bound. Malformed/nil filenames and interrupted-publication
remnants consume the entry/byte budget. A page containing no recognizable jobs
may still offer Next page. Next page replaces the visible list rather than
accumulating an unbounded list; Scan again returns to the beginning.

Ready results are still independently reverified by the existing Review action.
The user must explicitly Apply in the clip dialog. There is no implicit attach,
child restart, deletion, cleanup or project edit. Existing Active, Incomplete,
Invalid, Stale and Attached classifications retain their exclusion rules.

## Engine and ownership

`StretchRecoveryScan` is framework independent. Its immutable Session copy,
directory iterator, directory handle and resource credit live on the serialized
control/I/O caller. The Qt controller owns and destroys the cursor exclusively on
its background worker. No callback, GUI timer or GUI cancellation closes a
cursor or performs directory/media I/O.

Each page reacquires the cooperative publication read lease and releases it
before returning. Publication is allowed while the user inspects a page. An
owned-handle identity/change stamp and a newly opened pathname stamp must both
match the initial selection-directory stamp before and after the page. Added,
removed or replaced directory entries expire continuation. These stamps are
not media digests or a filesystem snapshot; the existing contained file reads,
selection binding, raw/result hashes and explicit review remain necessary.

Cancellation/failure expires a core cursor. Controller continuation additionally
requires the same root, project-opening epoch, model revision and immutable
Session pointer. Cancel/clear/render/review/shutdown revoke cursor admission;
retirement runs on the worker. Page backing storage and retained entry payloads
are charged to the ResourceLedger; externally held immutable snapshots keep
only their admitted credit alive. The normal dialog retains one page.

Traversal order is native directory order, with sorting within each page.
There is no claimed global ordering or total count. The all-at-once inventory API
retains its existing strict limits. **New selection publication still has the
existing128-entry/8MiB whole-directory admission ceiling**: this checkpoint
supports bounded review of larger existing/foreign directories, not unlimited
new render-job publication. Removing that ceiling requires a separately
qualified publication/index design, not increasing a constant.

## Acceptance and limits

Linux Debug build uses Qt6, C++20 and the actual in-process/controller/helper
implementation, with at most two compilation jobs at nice10/ionice7. Tests cover:

- 257 canonical invalid manifests plus two ignored/remnant names over three
  pages: all259 entries counted, all257 jobs seen once, original per-page bounds
  and bounded retained memory; retirement returns to the resource baseline.
- Three512KiB invalid manifests with512KiB page budget: one per page, no skips.
- Added entries, replaced directories, active publication, cancellation and
  expired cursors; publication can acquire its writer lease between pages.
- Controller continuation, wrong revision/epoch rejection, cancellation,
  restart/clear, changed-directory failure and immutable project state.
- Actual Qt dialog:129 invalid jobs become128+1 rows; Next page replaces rows,
  invalid jobs stay unreviewable, close retires the page, reopen starts at page1.
- Existing owned completed-render review/apply/undo/save/reopen/export workflows
  continue to pass; audio endpoints are not activated by these tests.

The larger corpus uses invalid synthetic manifests. This is not a quality or
throughput qualification of hundreds of completed renders. Windows native unit
execution, installed UI paging, many completed jobs, concurrent-publication
stress, interruption/power-loss durability and translated/native-speaker review
remain open. New UI strings use Qt translation/plural/locale formatting hooks;
this does not constitute additional delivered translations.

Initial local UI compilation found a missing test-only `src` include path; the
CMake target now provides it. An incorrectly named optional CTest selector found
zero tests and failed; the corrected selector runs `approved-media-root`.
Those operator/build failures are not counted as passing product acceptance.

## Next task

Qualify native Windows paging through protected CI; then design bounded new
selection publication for projects that retain more than128 jobs, including
migration, interrupted index publication, cancellation and stale-entry handling.
Keep monitoring correction/raw takes/default exports independent. Full
M2–M11/X004/X005, European translation/review/UI coverage, processing quality,
content coverage and project compatibility remain active and incomplete.
