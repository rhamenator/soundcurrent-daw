# ADR 149: atomic paired fade edits on existing clip state

Status: implemented for overlapping arrangement clips; broader comp workflow open.

Use one stable-ID `SetClipCrossfade` command to set two fades on the canonical
session owner. A shared project-frame window is converted to each clip's local
anchors. Validate both before publication; existing history snapshots retain one
atomic inverse. Do not materialize new audio or change source timing.

Existing immutable prepared curves serve live and offline rendering, with shape
1 for complementary-gain or constant-power pairs. This avoids introducing a
second rendering algorithm, callback ownership path or state migration.

A modal editor borrows the admitted virtual clip list and returns one command.
Selection/epoch/editability checks prevent retargeting across projects or busy
transport. Independent later edits remain per-clip; a persistent link model is
separate required work. Adjacent comp fragments require explicit source-handle
preparation before overlap and are not silently extended by this command.

Acceptance: independent project-frame curve oracle, invalid/group rollback,
existing state codec/history, actual Qt dialog interactions, original recorded
PCM/live/export/save-reopen equality and raw-file preservation. Native Windows
and installed workflow acceptance remain separate gates. Full parity is open.
