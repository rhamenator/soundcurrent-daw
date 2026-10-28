# Immediate comp-review invalidation on project replacement

Checkpoint 343 removes dependence on a periodic GUI refresh when a project Open
or Create command is accepted. Old comp-review intent and controls are invalidated
before the model worker publishes a new project epoch. The full DAW goal remains
incomplete; this is a repair to the existing rendered-comp preview workflow.

## Original evidence and bounded conclusion

The original hosted run `38801059137` at `2cfec5e` failed
`desktop-comp-assembly-apply` on Windows with “Old opening applied into new owner”.
That assertion combined action admission, button state and session equality, so
the original message does **not** prove which predicate failed or that an edit
reached the reopened project. The Windows desktop/import aggregate also failed
because it depends on that job; it was not another independent test failure.

All 15 original job logs and all 12 original archives were retained before any PR
branch advance. The archives total 1,985,464,103 bytes; their GitHub-reported sizes
and SHA-256 digests and their ZIP CRCs were verified. The original failed Windows
fixture and diagnostics remain available. No CI rerun replaced that evidence.

Source inspection found that the dialog's Apply button depended on a 100 ms timer,
while the action itself checked the live project epoch. A deterministic Linux
probe stopped only that dialog timer, admitted a same-root reopen, and measured
each predicate separately. Before the repair:

| Observation | Result |
| --- | --- |
| Apply button enabled immediately after Open admission | Yes |
| Apply preparation admitted immediately after Open admission | Yes |
| Apply preparation admitted after the new epoch appeared | No |
| Old Apply button still enabled after reopen | Yes |
| Reopened session equal to the original registered session | Yes |

This reproduces an admission/presentation race and a stale enabled control. It is
consistent with the original Windows failure, but it cannot reconstruct which
predicate failed in that earlier combined assertion. No model mutation was
observed in the deterministic probe. Existing canonical edit guards remain in
place and are not weakened by this repair.

## Repair

After Open/Create admission succeeds, the GUI invalidates comp-review intent,
cancels in-flight verification, retires an idle result and immediately disables
the old dialog's actions. The dialog stays invalid even if its timer is stopped
or the subsequent project open fails. Closing it and explicitly reviewing the
current project establishes new intent. An explicitly accepted new transition
render also establishes fresh intent, without reactivating an old review dialog.
Later accepted project replacement invalidates that new intent again.

The Apply entry point checks this invalidation state in addition to the existing
current owner/root/session/epoch checks. Publication still uses worker verification
and the canonical controller's structural guard. No file decoding, disk work or
blocking worker join was added to the GUI path.

The dialog invalidation callback is scoped to the live dialog and cleared on
close. An Open/Create request rejected before queue admission does not invalidate
review intent. No new
translation strings, dependencies, processing algorithms or project fields were
introduced.

## Verification

The strengthened existing UI test stops the refresh timer deterministically and
writes an independent JSON observation before asserting each condition. It checks
immediate and post-reopen action refusal, disabled controls, unchanged session,
then closes the expired review and verifies fresh Review/Apply/Undo. The old
product code fails this test with the observations above; source and binary hashes
for that baseline are retained.

The adjacent transition-creation workflow caught a regression in the first repair:
the invalidation flag also prevented a newly rendered, verified transition from
enabling Apply. That original failure is retained. The repair now resets intent
when a new render is explicitly accepted, preserving the creation workflow rather
than changing its test or deadline.

Both final Linux UI workflows pass: `desktop-comp-assembly-apply` and
`desktop-comp-transition-create`, in 37.00 seconds combined. They retain the
original six-case assembly workflow and eight-case transition-creation workflow,
including Apply/Undo/Redo/save/reopen/export, cancellation and owner changes.
Final local results and exact tested source hashes are recorded in the
[evidence manifest](../tests/results/M2/2026-10-15-comp-review-invalidation/manifest.json).
These Linux UI tests use offscreen Qt, synthetic owned projects and one low-priority
worker. No VM or physical audio is involved. Native Windows verification of the
repair remains a separate CI gate; installed previews have not been replaced.
The next feature work remains asynchronous waveform supervision and viewport
drawing, with checkpoint 342's worker protocol and explicit stale-result checks.
