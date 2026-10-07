# ADR068: shared controller parent and operation credit bank

Status: implemented scoped checkpoint, 2026-10-07; full X006 remains open.

Extend the existing framework-independent ledger with sibling domains under one
parent and atomically checked parent/child policies. Charge canonical state and
history persistently; charge immutable blocks through the snapshot child; reserve
declared edit workspace before mutation and reconcile persistent growth/shrink
through allocation-free matching-scope credit transfers. Preserve full-budget
Cancel and shared Save/barrier ownership.

Expose trusted parent/snapshot policies in the real scrollable Project resources
dialog with byte-exact unedited values, correlated results and application
preference persistence. Project files cannot configure machine resource budgets.
Use 1 GiB parent and 256 MiB snapshot defaults; retain independent state/Undo/IO
policies as additional constraints. No new library or project-schema revision.

The mutex and payload allocation/destruction remain off audio. Declared charges
are not exact allocations/RSS; expanding trials/command payloads, GUI indices and
projections, graph/caches/IO and allocator overhead remain required integrations.
This checkpoint must not be described as whole-process memory admission.

See [workflows, accounting boundaries, tests and next task](../83-controller-memory-resources.md).
