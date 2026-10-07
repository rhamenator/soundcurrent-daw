# ADR071: coordinate desktop execution policy and declared recording ownership

Status: accepted for the bounded X006 checkpoint, 2026-10-07.

## Decision

Sample the controller's trusted parent limit for new desktop graph/capture/export
preparations. Measure occupancy using immutable prepared payload declarations,
not the owner's configured allowance. Extend optional ResourceLedger leases to
capture pools, dynamic desktop acknowledgement lane arrays, recording bridge bindings,
manual take banks, writer/hash/journal
workspace and monitoring-off scratch. Aggregate manual banks own their nested
pipes once. Release only after existing callback/consumer quiescence off RT.

## Reasons and consequences

Independent hard local allowances could refuse large desktop preparations after
a user raised project policy, and recording owners overcounted unused graph
allowance as occupied memory. Shared leases admit overlapping declared payloads
without imposing a track-count ceiling. No dependency, persisted schema, routing,
quantum or DSP algorithm changes. Standalone explicit policies remain supported.

This remains partial payload accounting: copied Sessions, allocator/RSS, IO/Qt
internals, CPU/deadline capacity and recording input descriptors need separate
work. The full acceptance/limitations contract is [86](../86-execution-memory-policy.md).
