# ADR 019: grouped stable-ID session edits and scoped structural history

Date: 2026-10-06. Status: accepted for M2a foundations.

## Decision

Extend the framework-independent session model with transactional typed track/clip
operations. Preserve raw assets and the existing schema. Reuse canonical control
ownership, accepted-prefix barriers and immutable save snapshots. Retain scoped
before/after track/asset values and changed orders for one grouped undo unit,
preflight conflicts, and bound retained payload as well as unit count.

Verified recording admission uses the same history after independent disk checks.
Undo/Redo changes metadata and never deletes source recordings. Structural edits
publish no device/graph action; preparation remains immutable until graph-level
M2 integration is implemented and qualified.

## Alternatives and consequences

A whole-project undo snapshot would duplicate unrelated state and could overwrite
independent edits. Per-operation inverses require substantially more conflict and
ordering rules for mixed groups and asynchronous admission. Scoped value patches
provide an auditable first model; track-value copies can still be large, so a byte
budget retires whole old entries. Future finer clip/name patches can reduce this
cost without changing stable IDs or the public transaction contract.

An event journal for durable undo/recovery is still required separately. This
in-memory bounded history does not claim autosave or power-loss protection. CLI
editing provides a concrete workflow while desktop selection/timeline follows.
No new dependency/license or native audio callback work is introduced.
See [contract](../29-multitrack-edits.md).
