# ADR 199: Separate clocked protected worker identity and exact bindings

Status: accepted for owned span workers; paired application integration pending.
Date: 2026-10-15.

## Decision

Add an explicit clocked-protected payload and encoder with a distinct protocol and
processor identity. Rebuild its original geometry from the digest-bound recipe.
Bind exact geometry and sampling clock separately; repeat these identities in
readiness and completion. Derive the requirement to review history from both
recipe sides. Reuse existing bounded parsing, media/history admission, process
ceilings, cancellation and publication guards.

## Consequences

Old request identities keep their algorithms. Exact geometry and output rate are
never projected onto a legacy integer region. A completed file still requires
fresh history verification before use. Pair selection, adoption and editor paths
need explicit integration and acceptance evidence. See [checkpoint 312](../312-comp-clocked-protected-worker.md).
