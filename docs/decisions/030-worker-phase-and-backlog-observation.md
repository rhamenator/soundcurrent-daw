# ADR-030: observe recording backlog on its disk owner

Date: 2026-10-06. Status: accepted for the scoped diagnostic checkpoint.

## Context

A native 32-track attempt exhausted a 2.730667-second capture pool after about
55 seconds. The original run had no writer-phase observations. A later120-second
run was exact but missed one callback maximum budget; its coarse worker intervals
did not establish the original failure cause. Raising buffers or reducing durable
checkpoint frequency without evidence would hide rather than explain the gap.

## Decision

- Keep the configured capture pools, budgets, checkpoint cadence and durability.
- Observe ready/acquired backlog on the single disk owner using existing SPSC
  publication counters. Document final-partial upper bounds and omitted producer
  partial state. Add no counters or clock work to the audio producer.
- Add an optional bounded noexcept writer observer distinct from existing fault/
  cancellation boundaries. Pair writes/hash, audio flush, journal publication
  and idle waits; report written/durable cursors and actual consumer backlog.
- Retain fixed worker maxima/context in fixtures, and the callback maximum's
  native clock/start time in existing test-only timing. No unbounded event trace.
- Inject a declared 4-second lane 17 journal stall in the owned native fixture;
  require initiating-lane/queue evidence and full raw/common-output prefix oracles.
- Report phase/preemption/observer/stall wall time honestly. An overlap or an
  injected reproducible failure does not prove the original uninstrumented cause.

## Consequences

Observers add worker-side overhead only when configured; they must outlive all
construction and joined writers. Production defaults have no observer or clock
acquisition. Existing cancellation/fault callbacks are unchanged. No external
library/license choice changes. Linux/Windows functional parity and physical/
load/30-minute/release/full professional workflows still require independent
qualification. Choose any later storage-burst/checkpoint policy from measured
phase/queue evidence and exact recovery tests under the unchanged acceptance.

## Observed outcome

All 27 Debug and sanitized ordinary groups pass. Owned ten-second normal and
controlled4-second journal-stall probes pass their scoped sample/failure oracles.
The subsequent unmodified 120-second diagnostic fails around 58 seconds. Lane 26
flush2.469220532s grows ready 0→28, then journal publication1.803449198s grows
28→32 at unchanged written cursor2,654,208. Its4.272674676s write gap exceeds
the admitted2.730667s pool. This establishes checkpoint backlog in this run,
while underlying syscall service/scheduling and the original uninstrumented
failure remain unproven. All retained audio verifies exactly without changing
originals. Next evaluate phase staggering and an explicit admitted burst reserve,
qualify stall absorption/exhaustion and preserved durability, then require the
unchanged30-minute sample/deadline gate before any duration/parity claim.
