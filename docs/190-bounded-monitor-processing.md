# Bounded monitor processing composition

`PreparedMonitorProcessing` connects the original capture fanout's monitor route,
the causal best-sinc reservoir, clock/occupancy controller and typed output bank.
It is an original GPL-3.0-only C++20 single-owner component. The disk owner still
consumes the independent original raw bank. This component adds neither a device
adapter nor an installed recording workflow.

## Admission and ownership

Prepare external fanout/output banks and the processing owner before any route
starts. Admit matching channels, rates, capture identity, generation, output base,
packet/grid sizes, reservoir capacity, drift and slew policy. Reserve the entire
processing group before converter/pending storage: reservoir/kernel allowance,
controller, owner workspace and a touched 64-frame pending slab. Inner ledgers are
refused; use the outer group's ledger to avoid double charging. External banks
have their own prior admission. Joined retirement returns the processing credit.
Logical workspace allowances do not measure actual library/allocator overhead.

The processing owner alone calls `observe`, `step`, `finishAt` and `cancel`.
Native capture/render owners must deliver original observations through bounded
handoffs; they must not call this object concurrently. The
[original clock handoff](191-original-clock-handoff.md) now supplies bounded
processing-owner delivery; native SDK publication and activation remain open.
External banks outlive the processing object, and every owner joins before retirement or control settlement.

## Bounded service and pressure

One `step` performs an input acquisition/copy, one reservoir kernel call or one
output handoff. There is no unbounded drain loop. Input is copied as a whole
leased packet; release follows acceptance. A temporarily full reservoir retains
the actual leased packet while servicing existing input. The raw bank is never
read, modified or trimmed by processing.

Output is pulled into the charged pending slab and stamped with its actual opaque
monitor ordinal. A full output bank retains exactly these bytes, frames and
ordinal; no more DSP/input/scheduled ratio-grid progress occurs until publication
succeeds.
Original clock observations may still refresh while that pending PCM is retained.
There is no replay, invented silence, resampler reset or drop to hide pressure.
The native owner remains responsible for activation, deadlines and genuine loss.

`step` receives the caller's actual current QPC time. Zero, backward/future control
times or independently stale capture/render observations fault before processing.
At each new live grid, propose from actual ring occupancy, enqueue the exact ratio
event, then commit before service. Startup waits for original fits and sufficient
confidence rather than processing nominally without them. Native startup must
establish clock observations before activating monitoring: the default two-second
fit cannot be collected by buffering unlimited audio in a short reservoir.
Tests use declared synthetic clock observations and fixed logical time, not proof
of live clock freshness, latency, scheduling or drift stability.

## End, cancellation and faults

`finishAt` is nonmutating on refusal. It requires original capture EOF, a drained
monitor queue, no held input and an established live grid. The caller supplies an
explicit exclusive monitor end with real captured postroll. It is not inferred
from raw duration or project time. Final drain keeps the last scheduled ratio and
uses only actual remaining source; insufficient postroll faults. Raw recording
alignment remains separate. Short recordings that cannot establish this monitor
startup need a separate native policy; this component does not fabricate it.

Cancellation releases a held monitor lease, counts/discards pending monitor PCM,
cancels reservoir/controller/output and stops future capture publication. Already
accepted raw data remains available to the disk owner. Rendering/clock/reservoir/
capture-route faults retain the first processing receipt and original held stamp
when present, mark monitor-consumer loss and fault the output producer. The raw
accepted prefix is preserved. A faulted pending slab stays diagnostic evidence;
it is not published as healthy output. Joined settlement faults an abandoned
processing owner without inventing a packet and closes output production.

## Acceptance and limits

Frozen source `8d36d4ce0d8be26303afb6734ccf461409442f3f` passed435,830 checks
in each profile: Release0.23s, ASan/UBSan1.14s and TSan2.34s. The
[57,822-byte/15-member packet](../tests/results/M2/2026-10-11-monitor-processing/summary.json)
binds565 source/checkout inputs, three executable hashes and actual compilation/
CTest/LastTest/profile logs. Standalone member/source checks passed. No executable
or test media is included; incremental builds and compiler warnings remain visible.

- Three and 256 channels: 1,024 original frames, 512 output frames, forced
  single-slab output pressure, stable retries and bit-exact direct-buffer reference
  comparison under a different partition. Headroom survives without clamping.
- Four actual concurrent owners: capture, raw reader, processing and rendering;
  256 variable packets, 4,004 raw frames and 3,072 rendered frames, original raw-bit
  checks and bit-exact output reference comparison. Source credit waits/yields are
  outside prepared methods. The raw reader in this fixture is not the disk writer.
- Configuration/credit/partial-construction refusal, clock startup/age/reset,
  backward control time, nonfinite monitor input, explicit render underflow,
  abandonment, cancellation of pending PCM and insufficient postroll were tested.
- Guarded C++ and interposed libc allocation/free/pthread-lock counts are zero.
  This does not cover every syscall, SDK callback or real-time deadline.

Separate [raw writer evidence](189-original-raw-recording.md) includes actual disk
ownership and interrupted processes. A combined disk/processor/native workflow
still needs independent qualification. Actual Windows processing execution is
recorded below; installed/physical/load tests and processing-quality gates remain
open. This component composes resampling; EQ/mix graph execution and parameter timing need
their own processing integration. Earlier DSP quality failures are unchanged.
No complete DAW parity, European language coverage or release readiness follows.
See [ADR122](decisions/122-bounded-monitor-processing.md).

## Next task

The bounded original clock handoff is recorded in checkpoint191. Next compose
the disk writer, original clock producers and processing owners in a synthetic run.
Drive actual advancing control time and startup observations, establish output
priming before native activation, and qualify stop/postroll/punch/loop and joined
retirement. Keep the native Windows duplex factory disabled until SDK and desktop
acceptance exists. Portable finite-prefix recovery and original-to-project timing
mapping remain separate recording tasks.

## Independent Windows observation

MSVC job114429387569/run38125660721 passed61 synthetic suites in166.54s,
including monitor processing0.09s. Tested merge `d59c8475` binds heada7f8d898,
base9097b7a7 and tree792e2b5e. The
[60,636-byte/three-member packet](../tests/results/M2/2026-10-11-monitor-processing-windows/summary.json)
retains the complete native job/API observation and14 exact reviewed blobs.
Per-check counts and executable hashes were not exposed and are not inferred.
This earlier processing revision does not include checkpoint191's new clock
handoff; it is neither current-MAIN admission nor Windows native duplex,
installed preview, deadline or full product qualification.
