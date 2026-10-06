# M2d4c3: distinguish callback wall time from thread CPU time

The first 1800-second native attempt after reserve admission fails after about
196.3 audio seconds. The failure is retained and does not establish native duration
or quality qualification. The preceding120-second pass remains scoped to its own
finite run; all earlier observations remain open.

## Original failure facts

Commit `be93d8f` and the exact binary/source hashes were frozen before launch.
The sink's previous cycle starts at6727368437 with1024frames; its next received
position6727370485 skips1024frames. Mapping remains stereo/non-null with1024capacity.
The owner has no retained callback fault; control stops it after sink failure.
Every32raw writer joins at9,422,848frames, zero rejection/nonfinite/missing file
frames. Sink retains9,420,800frames; raw accepted suffixes are not trimmed.

Owner timing has complete9202samples: p99.9=4.542799ms(21.294%period), maximum
20.768787ms(97.354%period), failing the unchanged80%maximum budget. Maximum callback
clockposition6727369461 is exactly the skipped sink cycle. Callback start is
10.071982ms after that native cycle's monotonic timestamp; observed completion is
30.840769ms after it, versus a21.333333ms period. The installed SPA clock contract
(`/usr/include/spa-0.2/spa/node/io.h`, `spa_io_clock.nsec`) identifies CLOCK_MONOTONIC
and warns of jitter. This is temporal/position evidence, not proof of the physical
output deadline, scheduling root cause or exact syscall/processor component.

Thread CPU time and the failed sink's complete flags/rate/ID were not reported in
the original run. Those facts cannot be reconstructed or labeled absent. Raw
capture continuity does not establish sink continuity. The source/sink themselves
meet their finite callback budgets. Every33disk observer has complete phase pairs;
maximumready1, writegap92.346822ms, flush68.712531ms and journal70.482890ms. This run
shows no pool exhaustion; increasing reserve is not supported by its observations.

Read-only verification checks all301,531,136full raw and18,841,600common stereo
samples exactly, including the extra raw suffix. Peak4.4291 is preserved. All
original file contents/extents, canonical state, raw identities/origins/checksums
and finalized inactive journals remain unchanged. No Save/attachment/trim/copy
recovery alters this original. Defaults/prior links remain and owned routes retire.

## Supplemental fixture instrumentation

`DurationTiming` can explicitly request CLOCK_THREAD_CPUTIME_ID; its default stays
disabled. Only the opt-in native duration fixture enables it. Two additional clock
reads bracket the callback inside the existing monotonic wall interval. The real
production graph, processor, routing, buffering, project state and callback hooks
are unchanged. The fixture still acquires no audited allocation/free/blocking lock.
Clock reads have overhead and may include kernel CPU charged to the thread; this
is thread CPU time, not an isolated pure-DSP measurement.

Fixed scalars retain CPU sample/failure counts, sum/mean, maximum CPU, largest
wall-minus-CPU remainder and CPU time associated with the maximum wall callback.
Missing/backwards CPU intervals and CPU greater than enclosing wall time remain
invalid/visible; CPU coverage is separate from wall/sample/period coverage. A wall
remainder includes scheduling/interruptions/measurement edges and cannot be called
one identified scheduler cause. No per-callback CPU vector or new unbounded trace
is added; the existing one-million16-byte elapsed/period samples remain fixed.
Overflow still denies base deadline qualification and the maximum clock/start/
CPU context remains retained independently of that vector.

Maximum native-clock JSON now retains XRUN/discontinuity flags; joined sink
failure diagnostics print received/previous ID, cycle, nsec, rate and flags. These
are output-only additions to clocks already retained by the fixture, after joins.
They add no production/audio state or logging. The original logs remain immutable.

## Scoped acceptance and next action

Release/Debug/ASan+UBSan+LSan deterministic timing tests verify CPU totals/maxima,
associated worst callback, missing/impossible intervals, overflow/context retention,
unknown default and native clock flags. Existing nearest-rank/current-period and
60%p99.9/80%maximum gates remain unchanged; CPU values never replace wall budgets.
The targeted builds/tests are sufficient for these test-only source changes;
production's prior27Debug/27sanitized groups remain scoped to `be93d8f`.

A20-second native CPU probe verifies30,720,000raw/1,920,000output samples exactly,
Save/reopen, overs and all wall/CPU coverage/finite budgets. Owner meanCPU2.114234ms;
maximumCPU5.884950ms accompanies maximumwall5.888962ms; largest wall remainder
18.748us. Source/sink CPU coverage is also complete. That short probe does not
explain the original long outlier. Its source pins precede the output-only flag
extension; the final diagnostic sources and tests are pinned separately.

Next run the unchanged1800-second workload with supplemental CPU/clock facts.
Choose any processing/scheduling change from measured outlier evidence, preserving
sample/RT/period/full-duration gates. Do not waive the twelve retained unresolved
observations or promote any frozen F/Q/C/N contract. Linux physical/controlled-load,
Windows native/install/Qt, filesystem/power-loss/unload, full professional workflows,
X004/X005 and all-Europe qualification remain required.
