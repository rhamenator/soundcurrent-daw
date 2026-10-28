# ADR 179: Immutable dependencies through queued comp Apply

Date: 2026-10-14. Status: implemented; repeated paired production remains gated.

## Decision

Capture an immutable, framework-independent metadata guard before queuing an
assembled comp edit. Include the project identity and clock, selected assets,
retained clips and track layouts, saved source histories, and independent
selection references. Follow every reached assembled asset through both paired
outputs and their ancestors. Include dependencies discovered by original-file
history review even when the corresponding sibling is unchanged by the edit.

Reserve a conservative memory charge before copying. Deduplicate assets and
retained origins, retain the reservation through shared immutable ownership, and
release the data before returning its credits. The verified adoption token and
the guard must belong to the same resource ledger. The metadata guard grants no
file-verification authority by itself.

At the serialized canonical owner, require guard coverage of immediate sources
and retained origins, then compare all captured dependencies before mutating the
session. Missing or changed dependencies reject the edit atomically. Unrelated
title, track-name and EQ changes remain valid. These checks use no file I/O and
do not run in the real-time callback.

## Evidence and limits

Checkpoint 293 tests actual first-generation history, queued shared ownership,
resource release, stale-dependency refusal, and the main-window Apply workflow.
Layered metadata fixtures select only one outgoing assembled asset to prove that
paired siblings and ancestors are reached without a pre-expanded selection.
Those extra metadata generations are not rendered or adopted production jobs.

The production pair gate remains until history-aware paired verification and
rendering, mixed pairs, retained assembled takes, and repeated end-to-end
Apply/Undo/Redo/save/reopen/export have independent acceptance evidence.
