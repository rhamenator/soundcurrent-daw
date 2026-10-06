# ADR-028: separate synthetic duration from native deadlines

Date: 2026-10-06. Status: accepted for M2d4a qualification.

## Context

Finite owned-native capture proves sample/ownership behavior at a short range.
P001/M2 separately require a ten-minute 32-track synthetic workflow and a
30-minute declared-device native run. Generating a contiguous clock in a tight
loop or recovering a canceled short take cannot establish either duration or
driver deadlines.

## Decision

- Add an opt-in framework-free fixture using the production duplex engine and
  disk workers; pace its private source against an absolute wall clock.
- Keep source generation, comparisons, pacing, supervision and all media I/O
  outside the audited callback. Never wait for writers or change the source clock
  to hide gaps. Report producer lateness independently of capture correctness.
- Verify every raw sample/coordinate/hash, timing identity and alignment, then
  explicit Save/reopen. Use Unicode paths and independent input/matrix oracles.
- Qualify bounded cancel/write-fault/pool-overflow/process-kill recovery separately
  from the duration run. Preserve original checkpoint/media files and diagnostic
  artifacts. Kill only the supervisor's newly spawned child, retaining its ready
  receipt and signal exit status separately from recovery results.
- Keep process kill, disk full, power loss, physical alignment, Windows runtime
  and native deadline/load qualification distinct. Do not promote frozen parity
  axes or waive historical unexplained failures after one successful run.

## Consequences

The default normal run takes ten real minutes and about 3.69 GB of raw media;
it is not an ordinary CTest. Existing core/dependencies are unchanged. Short
Debug and sanitizer cases qualify correctness only; sanitizer lateness remains
visible. Native 30-minute timing needs sufficient bounded storage for all
callbacks, 99.9th-percentile/max measurements and a declared actual workload,
not the earlier finite fixture's 8192-sample prefix. Full M1/M2 remains open.
