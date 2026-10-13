# ADR148: Copy the reviewed checkpoint through the existing I/O owner

Status: accepted, 2026-10-13. Qualification: [Checkpoint249](../249-recovery-copy-desktop.md).

## Decision

Expose prefix recovery as an explicit desktop operation with a new destination,
finite/committed preview and original outcome. Preserve full-original export as a
separate workflow. Bind the worker to the exact retained sequence and reviewed
checkpoint; reject changed verified data before output creation.

Use the existing serialized worker, atomic cancellation/close intent and shared
resource ledger. Preallocate/admit the immutable copy receipt before I/O; keep its
credit alive through its final consumer. The I/O-only progress observer supports
responsive cancellation qualification. No automatic timeline attachment or source
mutation is introduced, and native completion status remains independent of copying.

## Consequences

The visible pending/unfinalized workflow can recover playable audio without
mislabeling a failed/interrupted recording as complete. A new container has exact
sample bits, not byte-identical container metadata. Empty or unsupported recovery
continues to refuse rather than fabricate audio. Catalog extraction runs for all
UI source edits; native translation review, Windows UI/installer and physical audio
qualification remain separate gates. No dependencies or stored journal schemas change.
