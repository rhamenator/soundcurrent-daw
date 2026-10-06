# ADR 020: stable selected identity and single-track adapter projection

Date: 2026-10-06. Status: accepted for M2b desktop foundations.

## Decision

Keep canonical project order separate from transient desktop track/clip selection.
Bind inspector edits to stable IDs and capture preparation through an immutable
accepted-prefix barrier. Adapt existing single-track owners with an off-RT
selected-track projection; normalize follow snapshots using the owner's captured
ID. Never serialize the projection or switch an active target on selection.
Report a missing ID and require explicit reprepare after incompatible changes.

Use the existing Qt Widgets scene/list and typed structural commands for the
first desktop timeline. Retain exact integer edit fields independently of the
approximate double visual axis. Disconnect child signal callbacks before derived
teardown and defer reconciliation that might delete emitting list/scene items
until their input event finishes.

## Alternatives and consequences

Reordering the canonical project to select a track would dirty/save UI intent and
make Undo and asynchronous take admission ambiguous. Generalizing every native
adapter to a full multitrack graph in this change would combine UI identity work
with a separate clock/graph/RT qualification effort. The explicit projection
keeps current workflows usable and lets M2c replace it with graph-level targets.
It copies immutable session state outside callbacks and is cached for the
inspector; copy/scene cost still requires broad UI load qualification.

This adds no dependency or project-format change, and confers no full M2,
Windows, localization or frozen-reference parity claim.
See [contract](../30-desktop-timeline.md).
