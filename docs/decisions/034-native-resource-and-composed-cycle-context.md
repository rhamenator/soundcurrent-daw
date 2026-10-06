# ADR-034: retain native callback resource and composed-cycle context

Date: 2026-10-06. Status: accepted for fixture diagnosis.

## Context

A later long run skips a sink cycle despite passing individual callback budgets.
The owner maximum is 16.718543ms thread CPU inside 16.722020ms wall, finishing
21.661598ms after native cycle nsec versus a 21.333333ms period. Disk queues stay
small. CPU charged to the thread alone cannot distinguish processing from kernel
work/page faults, and maximum callback wall alone misses late cycle starts.

## Decision

Add opt-in Linux calling-thread getrusage snapshots to test-only DurationTiming,
with fixed coverage/totals and resource facts associated with maximum wall/cycle
intervals. Retain an independent maximum from native cycle timestamp to callback
end, explicit unknown/jittered/overflow context and count beyond native period.
Do not replace or loosen existing sample, clock, wall/period or duration gates.

Validate deterministic deltas/associations/unknowns and live resource reads, then
short actual native coverage and the unchanged long workload. Preserve original
unknown component facts and all historical failures. Choose any later production
change from measured evidence; no presumed memory/NUMA/governor fix here.

## Consequences

Two resource-query system calls per observed callback add overhead. User/system
accounting has timeval resolution and differs from the thread CPU clock; native
cycle timestamps have jitter. These are diagnostic observations, not pure DSP,
physical-deadline or root-cause proof. Fixed storage grows only by scalars/snapshots;
existing wall sample capacity is unchanged. No production processor/RT/syscall,
route/schema/durability/dependency/license or Windows change.
See [contract](../44-native-thread-resource-diagnostics.md) for limits and evidence.
