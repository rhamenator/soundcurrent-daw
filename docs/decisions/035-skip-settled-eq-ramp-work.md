# ADR-035: skip settled EQ smoothing traversal

Date: 2026-10-06. Status: accepted for the prepared engine, scoped qualification.

## Context

A retained native owner outlier uses20.930266ms thread CPU, predominantly reported
user time, with no page faults/switches in that callback. The complete pipeline is
not isolated or qualified by an EQ-only benchmark. The EQ currently traverses all
bands again per sample to find ramps even while every parameter is settled.

## Decision

Maintain one bounded audio-owned active-band-ramp count. Increment on idle-to-active,
retain on retarget, decrement at completion, clear on stopped reset. Advance only
while a band or wet ramp exists, retaining all numerical order/timing/state policies.

Test overlapping/all64/same-frame/restarted/reset ramps against a closed-form gain
oracle and bit-identical block partitions. Retain existing independent math/numeric/
headroom/event/RT tests. Measure before/after with pinned old objects and serial
ABBA timings; do not infer native or general worst-case improvement from averages.
Then verify short native exactness and unchanged long sample/period gates.

## Consequences

Reduce redundant steady-state work without changing processor behavior, parameters,
latency, schema, queue limits or durability. One fixed private counter, no callback
allocation/lock/logging/query or new library/license. Record this adapted GPL code
as a later upstream candidate without touching ongoing equalizer repositories.
Historical whole-pipeline outliers, page faults and host causes remain unresolved.
See [contract](../45-steady-eq-ramp-work.md) and the evidence for exact scope.
