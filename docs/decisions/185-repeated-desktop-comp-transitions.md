# ADR 185: repeated rendered transitions through the desktop owner

Status: implementation under qualification; not an installed preview update.
Date: 2026-10-14.

## Decision

Use the existing timeline Save and render, background preparation/registration,
durable save, supervised child workers, assembly, Review and explicit Apply path
for clips backed by earlier comp assemblies. Remove the temporary rejection in
`prepareOwnedCompTransition` once this path has concrete repeated-workflow
acceptance. Original histories remain immutable and are fully reviewed using the
same publication/verification/adoption contracts as core repeated production.

The change does not introduce a direct unverified edit, bypass saved references,
change worker policies, or implicitly apply audio. Canonical Apply retains its
owner/epoch guard, joined dependency closure and fresh file review. Undo restores
the registered state; Redo restores adoption. Original/derived media remain
available across these operations and project reopening.

## Required acceptance

Drive the actual modal timeline dialog for two successive repeated edits and a
third mixed edit that retains an assembled take on one side. Use real original
rendering jobs at 44.1, 48 and 96 kHz source rates. Exercise separate Review and
Apply, no audio change before Apply, duplicate request/Apply refusal, Undo/Redo,
save/reopen, unchanged outer playback and exact reopened live/export samples.
Record original child logs, retirement, screenshots and joined-resource release.

Use the same main window to reject canceled and stale preparation, corrupted
original history during rendering and history changed after Review. Closing the
review during a held Apply must cancel without changing canonical audio; final
window destruction must retire the controllers and release assembly credits.
Qualification hooks control test timing only and do not fabricate successful
adoption tokens or bypass the production controllers.

All comparison/decoding and file work stays outside audio callbacks. The tests'
additional unchanged-playback comparison excludes the transition plus a
128-frame edge guard; it is not an independent sonic quality assertion. The full
original-file validators still run. Native Windows, installed delivery and
broader layout/quality/localization acceptance remain separate gates.
