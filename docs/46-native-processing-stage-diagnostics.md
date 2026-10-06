# Native processing stage observations

Date: 2026-10-06. Scope: M2d4c7 diagnostics. The 30-minute native gate remains open.

## Retained failure

The unchanged 1800-second attempt at `49f28e7` stops at approximately 326.25 audio
seconds (341.80 supervisor seconds). The sink skips 1024 frames. Its missing
clock position matches the owner's longest callback: 25.807899 ms wall,
25.806142 ms thread CPU, 120.975% of the 21.333333 ms period. The callback ends
26.668947 ms after the native cycle timestamp. Reported thread usage is
25.337 ms user / 0.373 ms system, with no page faults or context switches during
that callback. This is accounting evidence, not an instruction, cache, interrupt
or frequency diagnosis. Minor faults and switches elsewhere remain visible.

All 32 raw lanes retain 15,660,032 frames; no rejected or missing raw frames.
Sink length is 15,657,984. The original binary verifies all 501,121,024 raw and
31,315,968 common stereo samples exactly, including raw suffixes and overs
(peak 4.18949), without modifying the original project. All 105 files, canonical
state, media, hashes and journals remain unchanged. A reconstructed frozen
executable matches the failed launch's SHA-256 exactly and is retained locally.
All 33 disk observers have complete phase pairs; maximum ready queue is 6 of
118 slabs, with no exhaustion. Defaults/prior links remain intact and owned
routes retire. This is the eighteenth retained unresolved observation.

## Separate diagnostic executable

`sc-pipewire-stage-fixture` links the same static production libraries with four
test-only GNU ABI wrappers. It intercepts unresolved cross-object calls to:

| Boundary | Measurement scope |
| --- | --- |
| `DuplexBridge::process` | Shared raw capture, playback, processing, routing and observations |
| `CapturePipe::push` | Aggregate raw publication for all armed lanes within that bridge call |
| `EqLiveDriver::process` | Aggregate parameter ingress and EQ processing within that bridge call |
| `PreparedMixGraph::process` | EQ drivers **and** signed matrix accumulation/output checks |

Mix includes the EQ intervals. Adding mix and EQ would double count them.
Subtraction only approximates other work: inner queries, wrapper dispatch and
accounting boundaries have costs. Costs are intervals, not retired instructions.

The [GNU linker documentation](https://sourceware.org/binutils/docs/ld/Options.html)
specifies that `--wrap` applies to unresolved references; calls within one
translation unit are not intercepted. `PreparedEq::process` is called inside
`eq.cpp`, so the externally called `EqLiveDriver` boundary is used instead.
Signatures preserve the GNU 64-bit ABI, including the receiver and return type.
This experiment is Linux-only and is neither installed nor a Windows profiler.
LTO/inlining/toolchain changes require requalification of actual wrapper counts.

One callback owner writes fixed totals and four complete worst bridge snapshots,
each with its own native clock, status and nested intervals. Preparation pushes,
source/sink pushes and offline checks are excluded by a thread-local scope.
These four snapshots rank bridge elapsed time, not cycle lateness. A late-start
miss can fall outside this set; the original whole-callback maximum-cycle context
still exists separately. If their clocks do not match a new initiating miss, add
a bounded cycle-selected stage snapshot before attributing that miss to a stage.
Callback operations allocate nothing, log nothing, take no blocking lock and
perform no disk work. Wall/thread-CPU queries are deliberately present in this
diagnostic executable; query failure/backwards or inconsistent intervals remain
explicit unknown counts. Reporting happens only after callback owners join.
The original fixture and production libraries get no stage hooks or new clocks.

Observer overhead is included in the original whole-callback wall/CPU/resource
measurements. Diagnostic timing cannot establish the unwrapped product's
worst-case performance or physical round-trip latency. The unchanged full-range
sample, current-period, percentile/maximum, coverage and RT audit gates still run.

## Verification and observed scope

The new acceptance test validates fixed top-four ranking/ties, unknown intervals,
actual nested call coverage, terminal calls, outside-scope exclusion and
bit-identical wrapped/unwrapped duplex output. Wrapped callbacks allocate/free
nothing and acquire no blocking locks. Release passes; Debug and
ASan/UBSan/LSan pass alongside unchanged EQ and duration-timing tests. An initial
test setup used an inadmissible 64-frame slab; the existing admission check
rejected it, and the test was corrected to a 256-frame slab without changing
production limits. Previously recorded full 27-group and Windows build evidence
remains scoped to its checkpoint. Current Windows headless configuration/build
passes with these Linux-only targets excluded; native Windows remains unqualified.
The direct bridge baseline calls the original function; nested wrappers simply
forward while the diagnostic scope is inactive. This checks output/ABI fidelity,
not an uninstrumented-versus-instrumented performance comparison.

A serial 120-second owned-source diagnostic run verifies all 184,320,000 raw and
11,520,000 output samples exactly, origins, alignment, hashes, Save/reopen and
floating-point overs (peak 4.2659). All 5625 active bridge blocks contain exactly
32 capture calls, 33 EQ-driver calls and one mix call; every interval is known.
Complete whole-callback wall/CPU/resource coverage meets the existing finite
gates. Owner maximum is 11.235597 ms; 99.9th percentile is 6.067810 ms.

Average measured CPU per active bridge block is approximately 2.200 ms inside
the wrapped bridge: raw capture 0.227 ms, EQ drivers 1.683 ms, inclusive mix
1.795 ms. These are measurements under this host/load/observer configuration,
not a maximum or a cross-platform benchmark.

The four slowest bridge calls do not share one dominant interval:

| Same-callback wall total | Raw capture CPU | EQ-driver CPU | Inclusive mix CPU |
| --- | --- | --- | --- |
| 11.228280 ms | 2.981827 ms | 6.026957 ms | 6.845883 ms |
| 10.550068 ms | 0.130808 ms | 10.224570 ms | 10.297494 ms |
| 9.479832 ms | 2.221654 ms | 5.072624 ms | 5.135156 ms |
| 8.832166 ms | 7.770101 ms | 0.869900 ms | 0.933217 ms |

The largest observed callback also reports 32 minor faults. That fact does not
explain the original 25.8 ms callback, which reported none. This shorter run does
not resolve the original failure or qualify 30-minute sustained native recording.
No affinity, scheduler, memory-lock, governor, NUMA, rate, quantum or device policy
was changed; user playback was preserved. All 24 borrowed inputs and EQ heads
still match; neither equalizer was written. No dependency/license/push/publication
change. All 92 frozen acceptance/quality/reference/F/Q/C/N contracts stay unpromoted.

Next: use the separate diagnostic binary on the unchanged long workload and
retain a matching stage snapshot if the missed-cycle outlier recurs. If needed,
retain the longest individual capture/EQ call and prepared ordinal in that same
snapshot before choosing a processing change. Avoid inferring a single cause
from averages or combining maxima from different callbacks. Full physical/load/
filesystem/power-loss/unload, Windows, M2 editing, X004/X005, remaining professional
workflows and all-Europe localization remain required; the full goal is active.
