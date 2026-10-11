# Independent native clock correlation core

The Windows duplex implementation now has a prepared, framework-independent
clock-correlation primitive. This is a timing prerequisite; it changes no audible
desktop workflow. Native Windows simultaneous recording/playback remains open.

## Implemented contract

`PreparedClockCorrelation` keeps capture-frame, native render-tick, 100ns QPC,
and signed fractional project-frame domains distinct. Endpoint/stream/generation
and run-generation tokens must agree. Original first observations and first
faults are preserved. A caller supplies the project/QPC correspondence explicitly;
that anchor is not proof of an audible start or aligned recording.

Two original endpoint observations define a bounded affine fit. Default admission
requires at least 10ms and at most 1s between fit endpoints, limits fit age to
100ms and extrapolation to 10ms, and declares 100us observation uncertainty and
1,000ppm nominal drift allowance. These are configurable admission limits,
not measurements of Windows hardware. Both adjacent readings and the whole fit
must pass the drift check. A long observation gap requires a fresh fit.

Missing/delayed readings and render-startup zero do not seed or freshen a fit.
Capture frame zero with a valid timestamp is admitted. Initial capture
discontinuity is retained; a later discontinuity, backwards position/QPC,
unit/identity/frequency change, excessive drift or overflow produces a sticky
first fault. Query refusal does not mutate an otherwise valid owner.

Conversions subtract original integer coordinates and use checked standard C++
wide arithmetic with bounded iterations. QPC rounding chooses the nearest 100ns
tick, with ties toward the earlier absolute timestamp. Project coordinates retain
an exact signed floor and fraction with denominator 10,000,000; out-of-range
values are refused. Published error bounds cover declared observation uncertainty
and rounding only, excluding actual clock-model residuals or physical latency.

One owner uses each instance. Preparation/exception admission stays off RT;
observe/project/snapshot/fault access performs no allocations, blocking locks,
I/O, SDK calls or logging. Snapshot delivery between owners still needs a bounded
queue. No shared native clock origin or `CaptureTimingOrigin` is synthesized.
See [ADR115](decisions/115-bounded-clock-correlation.md) for decisions and API sources.

## Frozen local evidence

Source `6d45a86ad87c7d91fb7b56ea723a069a5289e316` passed four Linux Release
suites in 0.05s: clock correlation, native render timing, WASAPI packet admission
and packet copy. The new suite passed AddressSanitizer/UndefinedBehaviorSanitizer
with leak detection and halt-on-UB in 0.12s. The suite has 4,728 Linux checks,
including 768 committed golden vectors calculated independently with Python
unbounded integers and Fraction. It covers 44.1/48/96kHz, byte-like/native
frequencies up to UINT64_MAX, unrelated origins above 2^53, large QPC/project
differences, signed fractions/ties, ±200ppm drift, accumulated-jitter refusal,
startup plateaus, resets, faults, age/extrapolation limits and arithmetic overflow.

20,000 rolling observations/projections and sticky-fault paths have zero C++
allocations/frees and zero interposed libc allocation/free/pthread blocking-lock
calls. This test-only interposition does not cover every library or syscall path.
Windows builds report C++ interception separately; libc/pthread interception is
not silently claimed there. The extra Linux interception assertion accounts for
the platform's check-count difference.

The [29,497-byte packet](../tests/results/M2/2026-10-11-clock-correlation/summary.json)
retains seven members: selected input/executable hashes, actual new-core
compile/link logs, Release CTest/LastTest logs, and sanitizer build/CTest/LastTest
logs. All 535 selected source/test/UI/catalog/CI inputs matched the frozen commit.
They are not an exhaustive compiler/system/third-party dependency closure.
Standalone validation and optional source-byte comparison passed without Git
history or audio replay. No executable, installer or sample media is included.

Reproduce the core gate using `cmake --build <configured-build> --parallel 2
--target sc-clock-correlation-tests`, followed by `ctest --test-dir
<configured-build> -R '^clock-correlation$' --verbose --no-tests=error`.
The explicit MSVC native-unit CI target/allowlist now includes this suite;
independent Windows execution is recorded only after its actual job succeeds.

## Remaining scope and next implementation

This original GPL-3.0-only core adds no dependency, driver or certificate.
It does not qualify native duplex, drift-resampling quality, durable mapping,
physical latency, load deadlines, installed workflows or language coverage.
P005/M2 and full F/Q/C/N remain open; earlier processing-quality failures remain.
The existing Linux native loop/delayed-postroll evidence retains its original
frozen source and scope.

Next: add the charged, preallocated stamped capture bank and bounded SPSC
handoff. Keep original packet identity, frame position, QPC and flags; test
overflow/underflow, wrap, cancellation and retirement without blocking owners.
Then add causal monitor delay, prepared drift correction and versioned durable
raw-to-project mappings before composing native SDK owners and enabling the
Windows desktop duplex factory.
