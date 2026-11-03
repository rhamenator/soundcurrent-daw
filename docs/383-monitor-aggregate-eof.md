# Aggregate capture completion before monitor finalization

A capture fanout finishes its raw bank, then its monitor bank, then publishes
aggregate producer completion. A monitor consumer can observe a drained monitor
bank between the last two publications. That observation alone does not satisfy
the capture-owner completion precondition of `PreparedMonitorProcessing::finishAt`.

Previously `step` could return `NeedEnd` in that interval, leaving its caller with
a finalization request that `finishAt` would refuse. The processing owner now
returns `NeedInput` until aggregate producer completion is visible. The same guard
precedes planned-end and empty-input EOF handling. It does not change the terminal
publication order, drop input, advance the converter or weaken the finalization
precondition. The extra atomic read occurs only on a drained monitor bank.

`monitor-aggregate-eof` deterministically holds the publication interval using a
compile-time seam in a dedicated test executable. The production fanout library
contains no hook. Established-clock/input and empty-input cases must both wait;
after aggregate completion the established-clock case can finalize normally.
The unchanged regression fails on the previous processing implementation and
passes after the guard is added. It avoids a timing-dependent reproduction loop.

The existing `bounded-monitor-processing` corpus retains its input/output lengths,
byte comparisons, packet acceptance, deadlines and allocation/free/lock audits.
Its post-join first-failure diagnostic identifies a future owner failure without
logging on the processing owners. A historical aggregate CI assertion did not
identify its first failing branch: this repair proves the specific EOF race, not
that every possible concurrent-owner failure shares that cause.

Linux and Windows CI run the deterministic regression. Focused local tests cover
the original processing corpus and repeated forced EOF interleavings; native and
full-suite qualification still require the current exact-head CI results. No
installed preview or frozen Windows producer is changed by this source repair.


The concurrent finite-output fixture also declares its existing 3072-frame horizon
before owners start. Asking for that fixed end only after capture EOF allowed
some schedules to render beyond it; a later finalization correctly refused to
truncate already rendered output. No frame, sample, deadline or audit assertion
is removed. An additional deterministic case deliberately advances unplanned
output past a requested final end and requires that late request to remain refused.
