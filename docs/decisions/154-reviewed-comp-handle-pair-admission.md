# ADR 154: reviewed comp buffers and atomic paired source admission

Status: accepted for model admission and first unity producer; full generation open.

## Decision

Use a distinct paired source edit with expected clip and current/raw/nominal asset
snapshots. Preserve nominal stretch state and original buffer coverage, adjust the
extended playback offset, and apply both fades inside the existing structural
transaction. A second-fragment refusal must leave canonical clips/assets unchanged.

Use the existing isolated v4 worker for exact unity copies. Verify its owned
completion, then independently compare streamed current/nominal inner PCM before
creating a private verified pair owner. Keep review, resource leases, filesystem
operations and decoding off callbacks. Do not waive ordinary stretch adoption guards.

## Consequences

Real software workflows can test generation, dual review, atomic Undo/reopen and
live/offline equality without physical hardware. Nonunity/pitch and broader warp/
context producers remain required. Copied base samples do not alone prove SRC edge
quality. Controller/UI integration and native installed qualification are separate
work. No new dependency or claim of full-reference parity is introduced.
