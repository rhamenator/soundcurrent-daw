# ADR069: staged GUI payload leases under the project parent

Status: implemented scoped checkpoint, 2026-10-07; full X006 remains open.

Use the existing off-audio ResourceLedger parent for selected-track Session copies,
stable-ID list indices/decorations and timeline interval/query arrays. Borrow the
original first-track publication; lease non-first projections through aliasing
shared ownership. Stage all affected lists, timeline and projection before display
publication. Reuse unchanged inventories at full credit; retire old payload before
returning its lease. Preallocate timeline candidate scratch during preparation.

Retain the last admitted display on resource refusal, pause stale editing and
preparation, and expose required/available bytes plus resource-editor/explicit
retry controls. Suppress repeated allocation for an unchanged failed fingerprint.
Synchronize initial controls with admitted state while preserving the user's
requested monitoring value and accepted-prefix barrier semantics.

These are declared GUI payload scopes, not exact heap/RSS or universal allocation-
failure guarantees. Caller decoration temporaries, Qt/plan/control metadata,
allocator behavior, graph/cache/IO, commands and waveform/meter allocations remain
open. No dependency/schema change or native Windows/real-time capacity claim.
Internal prepare/commit plans require originating-component, single-use sequencing.

See [ownership, refusal workflows, evidence and next task](../84-gui-memory-resources.md).
