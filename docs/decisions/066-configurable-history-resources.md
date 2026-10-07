# ADR066: trusted configurable history policy and declared operation accounting

Status: accepted checkpoint, 2026-10-07; full X006 remains incomplete.

Replace hard-coded Undo retention with positive trusted byte/count policies.
Keep defaults compatible. Use deque entries with cached checked payload charges,
observable oldest-command retirement and transactional Undo/Redo candidate state.
Preflight before committing unrelated active gestures. Reject reductions below
existing usage rather than discarding it. Expose usage and async correlated policy
results through the existing controller; persist preferences outside project files.

This uses the existing C++20/Qt6 infrastructure, retains GPL-3.0-only licensing
and introduces no dependency or schema change. A full shared memory-lease ledger
is deferred until GUI, retained snapshots and old/new graph owners have independently
measured accounting. Declared work checks must not be presented as exact allocator
or RSS bounds, or as pre-admission of every candidate-building allocation.

See [workflow, admission terms and acceptance scope](../81-history-resource-admission.md).

Review correction: initialize the first immutable controller snapshot from trusted
options before starting the worker. Return read-only preflight charges and record
only successfully accepted operations so gesture-commit eviction cannot erase
previously admitted peak work. Both behaviors have deterministic regression gates.
