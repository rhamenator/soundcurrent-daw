# ADR134: publish one immutable full-FIFO startup receipt

2026-10-11. Accepted portable handoff; native consumption remains pending.

Mutable processing statistics cannot safely authorize a concurrent native owner.
Prepare an independently charged, generation-bound handoff with exact geometry
and stream identities. Claim one processing publisher after storage preparation.
Publish exactly once after full actual-FIFO population, using release/acquire
ordering around fixed immutable original statistics. Reject incomplete, stale or
malformed receipts without changing publication state.

Keep control cancellation independent of publication. A receipt can survive a
terminal state for diagnosis; it is never permission to bypass output, clock,
cancel or fault checks. Mark missing publication OwnerLost only after producer
join. Join every reader and owner before retiring storage. No reset/reuse.

Retain converter context separately from audible/project alignment and native
descriptor authority. Add native warmup and explicit auxiliary schema migration
as dependent implementation work. Existing callers need no handoff and retain
their behavior. No native factory activation or recording-format change.

Concurrent immutable-reader, full-FIFO, raw-bit, identity, cancellation, owner-
loss and resource-credit fixtures qualify this bounded primitive; compilation
does not qualify native device execution. [Checkpoint203](../203-monitor-startup-ready.md)
records exact source layers, original warnings and remaining gates.
