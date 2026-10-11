# ADR112: one continuous raw source per loop-recorded input

Status: accepted for the core foundation; desktop/native workflow pending.

## Decision

Reuse the duplex recording owner with looped backing reads and continuous capture.
Retain pass windows as non-destructive views of each full raw asset. Keep original
capture origins and stream/native clocks monotonic. Publish bounded original loop
intent alongside all raw jobs before activating audio. Retain it independently
of subsequent project settings. Stage inactive take views in explicit charged
pages, with partial-pass retention opt-in.

## Alternatives and consequences

Restarting capture jobs per loop would require replenishing prepared workers and
command credits, especially when loops are shorter than callback periods or input
latency. A fixed prepared-pass inventory would add an arbitrary musical limit.
Fabricating trimmed raw receipts would undermine immutable media, recovery and
clock-origin provenance. Continuous files avoid these costs using existing DSP,
writer, delay/postroll and callback ownership contracts.

Until-Stop duration is still bounded by signed frame clocks, disk space and
prepared resource budgets. Retained view counts consume explicit model/history
budgets. Sidecar publication is durable per job, not a multi-directory atomic
transaction. Failure refuses activation and preserves recovery material. Native
activation must honor this control-owned barrier. Recovered-copy reconciliation,
portable context copying, desktop/native workflow and broader quality gates are
separate follow-up work. No new dependency/license decision is introduced.

See [checkpoint175](../175-continuous-loop-recording.md).
