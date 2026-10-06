# Native callback resource and cycle context

Date: 2026-10-06. Scope: M2d4c5 diagnostics; native duration remains unqualified.

## Retained attempt

The unchanged 1800-second workload from `c60b5d8` stops around 145.9 audio seconds.
Sink positions skip exactly 1,024 frames. All 32 raw takes retain 7,001,088 frames,
with no rejection or missing file samples; the sink retains 6,999,040. Read-only
verification checks all 224,034,816 raw and 13,998,080 common stereo samples exactly,
including overs (peak 4.07214), preserving all 105 original files/journals/hashes
and canonical state. No trimming, attachment or re-save changes the original.

All individual callback wall/CPU coverage and finite budgets pass. The maximum
owner callback occurs at the skipped sink cycle. It takes 16.722020ms wall and
16.718543ms thread CPU, starts 4.939578ms after that cycle's monotonic timestamp,
and ends 21.661598ms after it versus a 21.333333ms period. This corroborates a late
composed cycle while leaving 3.477us of wall-minus-CPU. Original kernel/user time,
page faults and context switches were not reported and cannot be reconstructed.
Native cycle timestamp jitter prevents treating it as an exact physical deadline.
Disk maximum queue is 1 slab, flush 76.732782ms and journal 91.409512ms: no reserve
exhaustion is observed. Historical storage failures are not thereby resolved.

## Supplemental fixture measurements

Optional Linux `getrusage(RUSAGE_THREAD)` snapshots bracket the callback inside the
existing CPU and wall interval. The [Linux man-pages contract](https://man7.org/linux/man-pages/man2/getrusage.2.html)
defines calling-thread user/system time, minor/major page faults and voluntary/
involuntary context-switch counters. Validate signed values, microseconds and
conversion bounds; unknown calls or any backwards counter produce unknown deltas.

The native fixture opts in; helper default remains disabled. Fixed scalars retain
coverage/failure counts, totals, maximum system time/minor/major faults and deltas
associated with the longest wall callback. No per-call resource vector is added.
Resource CPU accounting has timeval resolution and may update differently from
the thread CPU clock. It does not prove pure DSP time, explain a particular page
fault, identify interrupt/NUMA/CPU frequency cause, or replace exact thread CPU
and wall measurements. Small accounting differences are retained rather than
misclassified as clock failure. Two extra system calls add visible overhead.

Retain independent composed-cycle context when a native monotonic timestamp and
period are known: elapsed from cycle timestamp to callback end, count ending beyond
that interval, and the maximum's clock/start/wall/CPU/resource facts. Future/jittered
or missing timestamps and arithmetic overflow remain unknown. This maximum can
come from a different callback than the longest wall interval and survives fixed
sample-store overflow. It is contextual evidence, not a physical deadline gate.

The existing million 16-byte wall/period sample records, per-period nearest-rank
quantiles, overflow/unknown coverage and 60% p99.9 / 80% maximum gates remain.
No gate is relaxed or replaced by a CPU/resource metric. Raw/output exactness and
whole-graph continuity remain separate, required observations even if individual
callback gates pass. No production timing/syscall/logging/allocation/lock, route,
parameter/buffer/durability/schema/dependency changes are introduced.

## Qualification and next action

Targeted Release/Debug/sanitizer helper tests cover all backwards counters, unknown
snapshots, live calling-thread reads, resource totals/associated maxima, distinct
cycle and wall maxima, overflow retention and exact cycle-boundary arithmetic.
The production recording change retains its prior Debug and scoped sanitizer
results at c60b5d8, including the unresolved desktop close timeout; only four Linux
fixture/helper/test sources change here. No Windows behavior or dependency changes.

The 20-second native probe verifies all 30,720,000 raw and 1,920,000 output samples
exactly, Save/reopen, floating overs (peak 3.98858), complete wall/CPU/resource
coverage and unchanged finite gates. Owner maximum wall is 5.684524ms and maximum
end-after-cycle interval 8.436455ms; zero measured callback page faults/switches or
ends beyond native interval. Its worst callback resource user time is 5.436ms and
system time 0, with the accounting-resolution caveat above. This short probe does
not explain the original outlier or qualify the unchanged 1800-second run. Inspect measured user/kernel/fault/cycle evidence
before choosing a production processing, memory-residency or scheduling change.
Preserve all sixteen unresolved observations, originals and reference contracts.
Full Linux hardware/load/filesystem/power-loss/unload, Windows native/Qt/install,
professional functionality, imports, equipment profiles and all-Europe localization
remain required; full goal is active and incomplete.
