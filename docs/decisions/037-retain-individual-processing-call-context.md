# ADR-037: keep individual call context within each selected callback

Date: 2026-10-06. Status: accepted for the separate Linux diagnostic tool.

## Context

A native miss now has a matching stage snapshot: raw pushes use 5.81 ms CPU and
EQ drivers use 18.35 ms CPU. Those are aggregates across 32/33 calls. A concurrent
source callback is also slow. Choosing a processing change requires distinguishing
a concentrated call from widespread slow intervals; aggregate maxima cannot do so.

## Decision

Reuse existing query pairs to retain each selected callback stage's longest known
individual elapsed interval, its paired CPU interval and local invocation ordinal.
Keep ties stable and reject unknown/inconsistent intervals explicitly. Omit this
ordinal from whole-run totals, which do not provide its callback clock.

Verify deterministic pairing/ties/unknown/zero cases plus actual ABI coverage,
exact audio and RT audits. Preserve all historical observations and original long
sample/current-period/resource/durability gates. Only test helper state/reporting
changes; add no production profiling hooks, new queries or dependency.

## Consequences

The snapshot remains bounded and callback-local. Its interval includes observer
overhead and does not establish retired instructions or host frequency/cache/
interrupt causes. Top-four wall selection retains its known lateness limitation.
Repeat the same long workload before selecting a processing or placement change.
See [contract](../48-individual-native-processing-calls.md).
