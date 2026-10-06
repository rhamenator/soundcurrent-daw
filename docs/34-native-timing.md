# M2c4: retained callback faults and bounded timing evidence

The playback bridge now retains the first callback validation failure independently
of the lossy meter queue. The plain record contains received and previous native
clock values, whether a previous block was admitted, generation, unchanged engine
position, detected reason, expected rate/maximum quantum/layout, buffer capacity
and supplied channel count. An invalid callback still silences certified buffers
and refuses to advance the prepared run. The first winning terminal status remains
authoritative; a concurrent control fault can win while the retained record
describes the callback's independently detected failure.

One audio owner writes fixed storage once and publishes it with release semantics.
The control owner acquires the publication flag and copies an immutable value.
Later terminal callbacks cannot replace it. Stop/device removal initiated by
control does not invent a callback clock. The PipeWire owner forwards the value;
the desktop worker copies it before endpoint retirement. Successful preparation
clears the new generation's snapshot without changing old published snapshots.
Unsupported native quanta now retain the complete received clock while passing
empty spans and zero capacity; no uncertified native buffers are mapped.

There are no callback allocations, blocking locks, formatting or logging added
by this production record. It is a developer diagnostic; a user-facing diagnostic
export/control panel and equivalent native Windows evidence remain required.

## Test instrumentation

The opt-in Qt/PipeWire fixture measures callback wall elapsed time with
`CLOCK_MONOTONIC` into 8,192 fixed duration slots per audited node (64 KiB plus
counters). Excess samples are counted rather than allocated. Sorting and
percentiles occur only after native stop/join. These measurements include
preemption and instrumentation overhead: they are neither DSP CPU time nor a
whole-graph deadline measurement. Production processing does not acquire these
timestamps.

The harness reads scheduler policy, priority and nice value for its owned process
threads through `/proc`/scheduler APIs. It preserves stdout/stderr and graph
cleanup/default observations on failure while returning a nonzero exit code.
It does not alter device defaults, rate, quantum, scheduler settings or hardware
routes. Its owned sink records first expected/observed clock positions, duration
and unavailable-input counts. Exception diagnostics read audio-owned timing
storage only after acknowledged player quiescence and sink join; otherwise they
explicitly report that timing is unavailable.

`mix-gap` deliberately skips one downstream consume after the first live edit.
The following clock must be rejected. The fixture now wakes the Undo checkpoint
on a gap/fault/completion and asserts the required prefix, rather than waiting
20 seconds after a known failure. This is a fixture correction, not a product
clock-recovery fix. No gap, xrun or mismatch is hidden to obtain a passing result.

## Qualification and limits

[The checkpoint evidence](../tests/results/M2/2026-10-06-native-timing.json) pins
source/log hashes, serial and concurrent test outcomes, optimized and debug owned
native sample oracles, scheduler samples, controlled failure and sanitizer
observations. `tests/verify_timing_reports.py` checks the finite passing corpus
and deliberate failure offline; it does not choose an audio device or qualify a
general performance bound.

Final measured cases at 48 kHz / 1,024 frames (21.333 ms block period):

| Build / case | Exact captured frames | Player p50 / p99 / maximum (ms) |
| --- | ---: | --- |
| Debug / 32-track mix | 480,000 | 6.7701 / 11.8036 / 16.2984 |
| Release / 32-track mix | 480,000 | 2.2602 / 3.1192 / 5.3885 |
| Release / disconnect | 25,600 | 1.6135 / 3.2217 / 4.5682 |
| Release / selected-track export during playback | 480,000 | 0.1001 / 0.1656 / 4.2068 |

The observed owned data-loop threads used SCHED_RR, priority 20 and nice 0.
No scheduler setting was changed. Debug and ASan/UBSan/LSan CTest suites passed
all 23 groups (21.09 and 60.59 seconds); the optimized EQ/mix/native-clock subset
passed all three groups. Headless Windows objects/tests compiled but were not run.

The final sanitized native attempt stopped at engine frame 6,144 before deliberate
sink injection. Received clock position 5,861,647,165 followed previous position
5,861,645,117 with previous duration 1,024: exactly one block was skipped. IDs and
48 kHz rate matched; xrun/discontinuity flags were false. Its seven measured player
callbacks had p50 25.0148 ms and maximum 26.4838 ms, above the block period. These
facts are consistent with deadline pressure but do not establish the cause of the
historical failures. No sanitizer error was reported; this failed native run does
not qualify native memory/performance behavior or the intended controlled injection.
Owned graph cleanup and default preservation were verified after it.

The 32-track corpus covers a ten-second mono saved matrix with independent EQ,
lane-17 live edit/Undo, exact replay and zero observed missing frames. Disconnect
checks an exact shorter prefix; concurrent selected-track export retains its
sample oracle. Audit wrappers observe no callback allocations/frees/blocking
locks in passing cases within their instrumentation scope. This is not proof
about every opaque library operation. Debug, Release and instrumented builds
have different measured costs and must be reported separately. Host load was
not controlled. Sanitized callbacks can exceed the observed block period;
memory-safety CTest success does not qualify native deadlines.

The earlier M2c2 completion timeout and M2c3 sink gap remain unexplained. Their
original logs lack these new facts. Later exact runs, measured build differences
and deliberate loss cannot retrospectively establish a cause or fix. A concurrent
debug CTest run also timed out awaiting synthetic recording capture at
`tests/ui_tests.cpp:368`; the serial debug and sanitizer suites passed. The first
failure is retained and its cause remains unresolved. Qualification under declared
CPU/disk load, long duration, quantum/device changes, physical devices, native
Windows and normal module-unload memory behavior remains open.

## Next implementation task

Prepare one native playback/capture owner for simultaneous armed tracks and
overdub. Use one project origin and admitted device clock, preallocated bounded
per-track raw capture pipes, explicit channel maps, and workers outside callbacks.
Verify timestamp/alignment, counted losses, stop ordering, take finalization and
interrupted-recording recovery against independently known sources. Retain these
clock/timing diagnostics during that work, and investigate unexplained failures
under a declared optimized load rather than treating reruns as resolution.

Full M2, general buses/sends/sidechains/PDC, frozen-reference F/Q/C/N acceptance,
X004 imports, X005 monitoring/print/portable profiles, Windows and all-Europe
localization remain required. No new dependency, license choice, publication or
equalizer checkout change is part of this checkpoint.
