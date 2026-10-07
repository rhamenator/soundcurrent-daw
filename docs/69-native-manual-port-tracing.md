# Native manual channel and processing-stage diagnostics

Checkpoint, 2026-10-07 UTC. Test diagnostics and finite evidence; full DAW goal
remains incomplete. See [receipt](../tests/results/M2/2026-10-07-native-manual-port-tracing.json)
and [ADR 055](decisions/055-native-manual-port-and-stage-tracing.md).

## What is measured

`sc-pipewire-manual-trace-fixture` links unchanged static production libraries with
GNU wrappers around `pw_filter_get_dsp_buffer`, `ManualPunchBridge::process`, raw
capture, EQ drivers and prepared mixing. The original fault fixture stays available
without these diagnostics. No profiling hooks or clock queries enter production
libraries; no new dependency or project schema is introduced. This opt-in target
requires Linux, a 64-bit GNU/Clang ABI and PipeWire.

Two prepared traces retain up to 8192 callbacks each, up to 32 channels and eight
buffer identities per channel. No pointer addresses are serialized. Each row has
its actual driver clock, buffer extents, presence, stable numeric identity and exact
IEEE bits for the first/last four samples. The source samples after filling its
outputs; the recorder samples inputs before processing. Recorder output buffers
are counted but never sampled as inputs. Short/absent/unsupported buffers and
unknown clocks are explicit. Dropped rows and identity exhaustion deny complete
trace qualification. All serialization occurs after callback/control/disk joins.

The recorder observer also counts actual manual-bridge calls. Stage totals must
agree with that independent count. The four worst complete bridge callbacks retain
their own clock and inclusive raw/EQ/mix costs; per-stage maximum CPU/ordinal comes
from the same invocation as its selected wall maximum. Mixing includes EQ, so these
costs must not be summed as independent work. Instrumentation itself consumes CPU;
diagnostic timing is not a measurement of an uninstrumented build.

The independent Python verifier reconstructs the source waveform and correlates
source/received markers at the same driver position/cycle/ID/quantum. A diagnostic
one-quantum shift is reported without correcting samples or timestamps. Markers
alone do not prove every interior sample: the original strict raw/recovered and
continuous nonflat output oracles remain required alongside the trace checks.

## Original observation 41 and startup classification

The first native cancellation fixture passed all existing gates. Its original trace
analysis refused 64 missing markers in two source callbacks: all 32 output buffers
were absent, at positions 5444517888 and 5444518400, cycles 7934901/7934902. The first
advancing recorder callback occurred later, position 5444519936/cycle7934905.
Original traces, raw/recovered media, journals, logs, receipt and verifier version
were frozen before diagnosis. No sample mismatch was observed in that original
analysis. This is a verifier classification error, not a demonstrated channel
delay or recording failure.

Source generation may run before/after its links provide output buffers. The
reviewed verifier retains and counts those rows, checks every available generated
channel, and separately records absent/partial source rows. Every advancing
recorder callback still requires all 32 observed/present/sampled inputs, one actual
bridge call, and a matching source row with all 32 sampled outputs. Consumption of
an absent or partial source row is refused. No waveform, continuity, deadline or
recording acceptance gate was reduced.

A mutation harness initially selected the first absent startup source as if it
contained measured samples. That assertion failure is retained separately; the
corrected harness mutates a source row actually consumed by the recorder.

## Qualified finite evidence

- Debug, Release and ASan/UBSan: three stage/marker unit tests pass; leak detection
  explicitly enabled for sanitizer tests. Manual wrapped/unwrapped output, raw
  samples, timing origins, receipts and postroll agree; callback allocation/free/
  blocking-lock counts are zero. Separate-buffer-provider linking verifies actual
  wrapping, and executable disassembly confirms external calls reach all wrappers.
- Debug and sanitizer: cancellation with initially unserviced disk ownership and
  retired hash failure pass the existing fixture and new marker/stage checks.
- Native Release: 141 advancing cancellation callbacks and 149 advancing hash-failure
  callbacks have complete 32-channel source/received marker correspondence. Both
  retain two pre-recording all-absent source-generation rows, zero dropped/unknown
  marker coverage and zero RT allocation/free/locks. Hash injection occurs after
  complete delayed postroll; healthy recordings recover with the initiating error
  retained. Defaults/prior links remain unchanged and owned routes clean up.
- Native owner callback maxima are 1,262,018ns and 1,238,325ns, respectively, at
  512 frames/48 kHz; existing finite and current-cycle gates pass. Per-callback raw/
  EQ/mix costs and clocks are in the receipt. This does not qualify sustained load.
- Fourteen altered traces are refused against both a synthetic and an actual
  native reference, including marker corruption, a channel 19 quantum delay,
  missing ports/clocks, dropped coverage, exhausted identities, count/cost/ordinal
  errors and consumption of absent/partial source buffers.

The [earlier originals1–40](68-native-manual-fault-recovery.md) remain unchanged.
Original 37's channel 19 extra 512-frame prefix and original 38's predominantly-CPU
13.88ms owner overrun are not explained by these later passing runs. No common
cause is established. Original 36's native early-Stop failure remains unqualified.
No F/Q/C/N axis or frozen reference contract is promoted. Native Windows, sustained/
physical qualification, all 92 contracts, X004/X005/X006 and Europe remain open.

## Reproduction and next task

Build the diagnostic target plus `sc-port-marker-tests` and
`sc-manual-processing-stage-tests`. Run the original
`tests/verify_pipewire_manual_fault.py` with the diagnostic executable, then
`tests/verify_pipewire_manual_trace.py --receipt RECEIPT --output ANALYSIS`.
`tests/manual_trace_verifier_tests.py` accepts a retained qualified receipt. Native
runs must be sequential and start after all owned builds/tests/producers terminate.
Freeze any original failure before diagnosis/rebuild; never replace it with replay.

Next implement a serialized off-GUI manual control worker with bounded messages,
reliable replies/results and priority Stop/Cancel. Separate command acceptance from
disk binding so early control requests are not held behind startup work. Qualify
native early Stop, actual Qt manual controls, shutdown/recovery and rapid monitoring
edit/Undo against widget state. Use these traces if alignment or CPU faults recur;
keep full original terms and unchanged timing gates.
