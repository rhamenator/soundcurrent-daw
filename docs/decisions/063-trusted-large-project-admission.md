# ADR063: trusted large-project resource admission

Status: accepted first implementation, 2026-10-07; full X006 pending.

Replace fixed total-track ceilings with configurable caller-owned byte admission
for canonical state, parsing/encoding and prepared DSP/playback. Project files
cannot override trusted budgets. Preserve checked arithmetic, explicit refusal,
transactional editing/publication, stable IDs, strict shapes and distinct backend
channel/processor contracts. Charge and prepare track-sized callback masks off RT.
Use validated immutable session indices to avoid per-lane whole-session validation
and scans. Retain full musical state in schema 1.7, with strict older-reader refusal
and 1.0–1.6 migrations.

Admission charges are not allocator/RSS or universal real-time capacity claims.
Do not qualify X006 from a large synthetic count alone. Finish media/cache,
virtualization, history/graph aggregate envelopes, recording, freeze, scheduling,
platform and sustained workloads. Retain failed original experiments and missing
terms. See [implementation and scoped evidence](../78-resource-admitted-projects.md).
