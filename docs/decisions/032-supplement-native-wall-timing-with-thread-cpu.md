# ADR-032: supplement native callback wall timing with thread CPU time

Date: 2026-10-06. Status: accepted for scoped fixture diagnosis.

## Context

The post-reserve native attempt skips one sink cycle around196seconds. A coincident
owner callback uses97.354%of its period and starts10ms after the native driver
cycle timestamp. Disk queues remain at mostone slab. Original CPU time and full
sink flags were absent, so another reserve increase or presumed scheduler fix
would lack evidence.

## Decision

- Add optional CLOCK_THREAD_CPUTIME_ID measurement to the native fixture's bounded
  timing helper; default off, no production processing changes.
- Retain fixed CPU coverage/totals/maxima and CPU associated with maximum wall
  interval. Keep wall-minus-CPU interpretation scoped to a remainder, not an
  identified scheduling/IRQ cause or pure DSP timing.
- Preserve all one-millionelapsed/period samples and original wall/period gates.
  Missing/invalid CPU intervals stay explicit; CPU metrics do not replace deadlines.
- Print full already-retained received/previous sink clock and maximum flags only
  after joins. Preserve original incomplete evidence without invented flags.
- Verify deterministic helper contracts, actual short native CPU coverage/samples,
  and then the unchanged1800-second workload. Retain all originals/historical faults.

## Consequences

The fixture adds two CPU clock calls per observed callback, with visible overhead;
CPU time may include system work charged to that thread. No new library/license,
production callback counter/clock/allocation/lock/disk/log/GUI, project schema or
budget change occurs. A passing short probe does not diagnose a previous long
outlier or qualify long native/physical/Windows/full-product behavior.
