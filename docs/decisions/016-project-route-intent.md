# ADR-016: typed channel intent with exact matching and explicit activation

Date: 2026-10-06. Status: accepted for S8a.

## Decision

Use schema 1.1 per-channel backend descriptors for capture, playback and monitoring.
Migrate schema 1.0 opaque intent without interpreting or discarding it. Match every
configured channel uniquely by the complete adapter descriptor; show named missing,
ambiguous, legacy or unavailable-backend placeholders. Restore current IDs only after
matching. Do not activate audio when loading or resolving a project.

Merge GUI channel patches on the canonical controller instead of submitting whole
routes copied from stale GUI snapshots. Join route changes to existing bounded undo;
validate before touching an active scalar gesture. Keep active routing unchanged until
Stop/reprepare. Reset live UI choices on each successful Open/Create, including the
same project UUID. Save and attachment keep their existing revision/content semantics.

## Reasons and consequences

PipeWire numeric IDs and serials are transient. Names are useful portable intent but
not immutable/authenticated device identity. Arbitrary first-match/default/fuzzy
selection can route audio to the wrong endpoint; ambiguous matches need explicit user
choice. A currently selected duplicate can be used explicitly, but cannot be uniquely
restored from the same saved descriptor on the next opening.

1.1 is a deliberate format change; old readers reject it. New readers preserve 1.0
identities, media and settings and emit current snapshots only after explicit saving.
Unknown fields/versions still fail. Metadata has explicit field, channel and aggregate
budgets. No native device activation, disk I/O or routing allocation enters the RT
callback. No new dependency is introduced.

Alternatives rejected: persist graph IDs (unstable), match display labels (localized),
automatically choose first/default output (silent misrouting), copy stale whole routes
(loses burst channel edits), reinterpret opaque legacy strings (unproven semantics),
reconnect on Undo (changes active processing topology without a safe transition).

Windows endpoint GUID adapters, richer physical identity, monitor-mode persistence,
multitrack route commands and safe live graph replacement remain staged requirements.
See [contract and acceptance](../26-project-routing.md).
