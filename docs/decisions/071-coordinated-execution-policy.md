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


## Review correction

The local duplex total must agree with the shared ownership declarations: bridge
bindings count once, each external raw pool counts once. Early standalone
preparation must include the same graph, reader/cache, bridge and pool declarations
before constructing pools or hashing media. Share off-RT declaration helpers
between preflight and construction. Trial metadata and copied Session state remain
outside this declared payload contract. Preserve actual prepared-lane validation
before a reader hashes media, even if its later caller Session has changed.
