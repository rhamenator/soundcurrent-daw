# Individual processing calls in retained native callbacks

Date: 2026-10-06. Scope: M2d4c8 diagnostics; sustained native recording remains open.

## Matching stage evidence at the failure

The separate stage-instrumented 1800-second attempt at `5e13406` stops after about
447.32 audio seconds (462.63 supervisor seconds). Sink previous cycle 183967 is
followed by 183970, skipping two 1024-frame cycles. The owner and source longest
callbacks both have the first missed clock position, cycle 183968. No clock xrun
or discontinuity flag is reported by those callbacks.

Owner wall 25.097698 ms / CPU 25.095800 ms exceeds the 21.333333 ms period. It starts
17.261859 ms after the native timestamp and ends 42.359557 ms after it. Reported
usage is 24.019 ms user / 1.074 ms system; no faults or switches in this callback.
Source wall 8.394433 ms / CPU 8.391981 ms on the same cycle starts 4.633199 ms late;
it also reports no faults/switches. Accounting is not retired instructions or a
frequency/cache/interrupt diagnosis. The interval between source completion and
owner start has not been isolated.

The retained stage snapshot has that **same** clock, with these CPU intervals:

| Boundary | CPU interval |
| --- | --- |
| All 32 raw pushes | 5.806479 ms |
| All 33 EQ drivers | 18.350076 ms |
| Mix, including EQ drivers | 19.171763 ms |
| Wrapped bridge | 25.087768 ms |

Mix contains EQ; these numbers must not be added as disjoint costs. The snapshot
does not reveal whether an aggregate is spread across calls or concentrated in
one call. The similarly slow source also remains part of the evidence.

All raw lanes retain 21,471,232 frames, no rejected/missing raw frames. Sink keeps
21,468,160. Read-only verification checks all 687,079,424 raw and 42,936,320 common
stereo samples exactly, including the raw suffixes and overs (peak 4.28308), with
all 105 original files/canonical state/media/hashes/journals unchanged. All 33 disk
phase pairs are complete; ready queue maximum is 5/118, with no exhaustion.
Owned routes retire and defaults/prior links remain unchanged. The exact original
diagnostic executable is frozen locally by SHA before rebuilding.

The first retained-reader launch failed before executing because a copied binary
lacked its execute bit. Its exact handle terminated; correcting the owned copy's
mode allowed the read-only verifier to run successfully. No timeout-based restart,
original media edit or source change occurred during a live reader.

## Diagnostic extension

Within each stage of each retained worst bridge snapshot, keep the longest known
individual call's elapsed duration, the CPU duration from **that same call**, and
its zero-based invocation ordinal. A tie keeps the earlier invocation. Invalid
CPU-greater-than-wall or unknown intervals count as unknown and cannot replace a
known maximum; even a valid zero interval remains distinguishable from unknown.

Ordinals are callback-local diagnostic observations, not persistent track IDs.
For this admitted fixture, raw calls follow the 32 prepared arms; EQ drivers follow
the 33 prepared mix tracks including the unarmed file track. Failure/partial calls
retain their actual count and status. Never infer a missing invocation or reuse
an ordinal across another graph without its preparation mapping.

The existing start/end queries are reused: no additional clock, allocation, lock,
queue or log is introduced. Only test helper storage/comparisons/reporting change;
production DSP/backends/APIs/parameters/schema/durability and static libraries are
unchanged. Aggregate totals deliberately omit individual ordinals because they
do not select a single callback clock. Top-four snapshots still rank bridge wall,
not cycle lateness; the limitation documented in [the prior contract](46-native-processing-stage-diagnostics.md)
remains. Unknown interval coverage stays visible and denies qualification.

## Verification

Tests check paired wall-selected CPU, tie order, rejected inconsistent/unknown
intervals, valid zero intervals, snapshot preservation and omission from aggregate
totals. Existing real ABI/boundary/terminal/outside-scope tests still check exact
original/wrapped bridge output and zero callback allocation/free/blocking locks.
Release passes; three targeted Debug groups pass 1.31 s and the same
ASan/UBSan/LSan groups pass 3.40 s. Production libraries and plain fixture hashes
stay unchanged; Windows has no new runtime/profiling qualification.

A serial 20-second native check verifies all 30,720,000 raw and 1,920,000 stereo
samples exactly, origins/alignment/hash/Save-reopen/overs and complete original
wall/CPU/resource coverage. Owner maximum 3.815271 ms and maximum cycle-end context
6.379656 ms pass the original finite gates. All active blocks have 32 capture,
33 EQ-driver and one mix call; individual maxima are known, paired and within those
ordinal bounds. This short check does not resolve the sustained failure or any
earlier observation. [Evidence](../tests/results/M2/2026-10-06-individual-native-processing-calls.json) records the scoped checks.
The nineteenth unresolved observation is preserved alongside
the earlier eighteen with original source/binary/clock/phase/media receipts.

No host scheduler, affinity, governor, memory-lock, NUMA, device/rate/quantum policy
changes, equalizer writes, dependency/license additions or publication. All 24
borrowed inputs/heads and 92 frozen acceptance/quality/reference/F/Q/C/N contracts
stay unchanged. The independent [European inventory audit](47-europe-language-inventory-audit.md)
still has no qualified translations. Full product goal remains active/incomplete.

Next: run the unchanged long diagnostic workload to obtain individual-call facts
at a matching initiating cycle. Use those facts to choose a bounded processing,
placement or further measurement experiment; do not assign an instruction/host
cause from an elapsed/CPU interval alone. Original long native, physical/load/
filesystem/power-loss/unload, Windows, punch/loop/takes/comping and every remaining
professional, import, equipment and European-language requirement remain open.

## Sustained individual-call attempt: retained failure

The subsequent `cafc539` diagnostic stops after56.92audio seconds /83.78supervisor
seconds. Exact native handle11110 terminates exit1; actual binary is frozen
before changes. Exact reader35316 terminates successfully: all87,425,024raw /
5,459,968common stereo samples match exactly and all105original files unchanged.
Raw lanes retain2,732,032frames, sink2,729,984; the2048-frame raw suffix is intact.
Peak3.82195 retains float headroom.

Sink previous cycle188191 is followed by188193, skipping1024frames. Owner maximum
shares missed cycle188192:21.854538mswall /21.851922msCPU, starting1.635503ms after
the clock and ending23.490041ms after it. Usage20.229msuser /1.620mssystem, no
faults/switches in that interval. Original finite maximum gate fails. Timing,
CPU and resource coverage is complete.

Matching snapshot raw32calls sum9.408983mswall /5.228982msCPU; longest raw call
ordinal3 is4.177203mswall /4.175100msCPU. EQ33calls sum8.490520mswall /7.028372msCPU;
longest wall-selected call ordinal4 is1.494642mswall /0.784833msCPU. Inclusive
mix8.533080mswall /8.531484msCPU; bridge21.840753mswall /21.839188msCPU. Nested
intervals are not disjoint. Inner paired CPU queries omit observer work included
in outer intervals; wall-minus-CPU does not establish scheduler/instruction cost.
Raw outlier, wider EQ costs and remaining bridge/observer work stay unresolved.
Source maximum8.530514ms is at another cycle, not evidence for this missed cycle.

All33disk phase pairs complete, maximum ready79/118, no exhaustion. Some flush/
journal phases exceed1second; exact preserved media does not prove durable
cadence or disk performance. Defaults/prior links stay, owned routes retire.
No concurrent build or CPU experiment, policy/device/rate/quantum/affinity/
governor/NUMA change, or equalizer write. The twentieth unresolved observation
and original nineteen remain in
[evidence](../tests/results/M2/2026-10-06-individual-stage-sustained-failure.json).

Next: independently reproduce and fix desktop-close cancellation by an already
published controller error. Continue native performance work from this evidence;
do not repeat an unchanged long run or waive its gate. All professional, Windows,
physical, durability, import, equipment and localization requirements remain.
