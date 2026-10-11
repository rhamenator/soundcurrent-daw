# Prepared monitor startup and bounded preroll

2026-10-11. M2 progress. The full frozen-reference functional, processing-quality,
content, compatibility, installation and localization goal remains incomplete.

## Problem and scope

The current drift controller requires two seconds of trustworthy original moving
clock observations. Keeping all PCM from that interval in the ordinary monitor
ring can exhaust it. The native renderer also needs enough output to prime its
actual FIFO; admitting one small callback does not admit the whole FIFO.

This checkpoint adds an **opt-in processing-owner startup path**, with explicit
native capacity/fill geometry. It does not enable the native factory. The existing
path without this option remains covered by the regression suites.

## Prepared ownership and admission

`MonitorStartupConfig` carries the actual native capacity, maximum native fill,
and bounded wait in original QPC units (100 ns). Prepare it before either route
starts. The external capture fanout, clock/control/Stop cells and output bank
outlive processing and all owners join before retirement.

Admission checks native capacity/fill, the output bank's **actual 64-frame
publication grid** times its slots, maximum pull, source ring, wait and resource
credit. The bank must fit a full native FIFO plus one grid. The recent source tail
fits worst-case demand at the lowest admitted output/source ratio, causal kernel
context, target occupancy, one native FIFO plus one grid, and one actual capture
packet. It must fit the ordinary source ring. Admission reserves sample storage
and enough original packet descriptors even for one-frame packets. These credits
are conservative payload accounting, not measured RSS or allocator overhead.

All vectors and resource leases are prepared off RT. Each observation-phase step
copies at most one actual monitor packet. Trimming examines at most one maximum
packet's worth of older descriptors. Transfer pushes at most one original packet
fragment and one contiguous ring span. Cancel/fault disposes of retained startup
state in constant work. No disk I/O, logging, allocation/free or blocking lock is
added to the prepared calls.

## Original observations and disposition

The raw route retains its entire original input. Only the monitor preroll drops
its oldest frames while waiting; this is counted explicitly. Full incoming PCM is
checked for finiteness before anything is discarded. Native identity, positions,
timestamps, flags and packet ordering still enter through the original capture
bank and original clock handoff. No clock history is seeded, cloned, reset or
invented, and no confidence requirement is relaxed.

Once a complete admitted tail and a real ready clock proposal exist, the owner
enqueues and commits that proposal before transferring actual retained source to
the existing converter. It retains the first transferred original packet and its
partial-packet offset, the last accepted original stamp, counts for accepted,
trimmed, transferred, retained and abandoned frames, and startup phase. Counts
satisfy accepted = trimmed + transferred + retained + abandoned. The first
transferred source includes causal kernel context: it is **not a first-audible
sample, project alignment or acoustic latency map**.

Content readiness is recorded only after publication of enough actual output for
the whole native FIFO. Premature output consumption fails explicitly. Missing
input, stale/foreign clocks, confidence rejection, source fault, cancellation and
the bounded wait retain their failure/disposition evidence. Timeout covers both
clock measurement and output prefill. No live underflow is replaced with silence.

Statistics belong to the processing owner (or control after join). The readiness
boolean is **not** a cross-thread handoff or authorization for SDK callbacks.

## Qualification

Frozen C++ commit `1dbcea2b56d796f8c3df20141e2edcc05292e0be`, tree
`c4447ad99958c51e601d0c8a1c2a5baf3d5cff9e`, passed eleven suites per profile:

| Profile | Elapsed | New startup checks | Guarded allocation/free/lock |
| --- | ---: | ---: | ---: |
| Release | 1.69 s | 19,652 | 0 |
| ASan/UBSan | 14.14 s | 19,652 | 0 |
| TSan | 20.00 s | 19,652 | 0 |

The six independent-converter cases cover 1/2/8/256 channels, 44.1/48/96 kHz
source rates, an actual declared 4,097-frame FIFO, ring wrap, original wide unsigned
positions and partial-packet source offsets. Clocks start without previous fit
history and move over successive synthetic ticks. Readiness occurs after 200
10-ms ticks with the unchanged two-second minimum span. Maximum error against an
independent converter fed the retained actual tail is zero. Raw samples and
original stamps are checked throughout rather than retaining a second huge raw
copy. One-frame packet descriptors are exercised through rolling trim and transfer.

Failure workflows cover no clock timeout, early EOF, nonfinite PCM, cancel,
foreign clock identity, premature pull, prefill timeout, strict confidence
rejection, insufficient ring/bank/pull geometry and failed resource admission.
Existing native outcome, raw recording, render/capture binding, original SDK end,
Stop/control, combined recording and packet-copy suites remain in the cohort.

[Evidence](../tests/results/M2/2026-10-11-monitor-startup-preroll/summary.json)
binds 593 selected source/checkout inputs, 33 executable hashes, 15 Windows
object/library hashes and full logs in a 68,378-byte, 15-member packet. Native
Windows wrapper and full core-only targets cross-compile; the new processing and
test objects compile without warnings. This does **not** execute new startup tests
on Windows or qualify SDK/hardware deadlines. No executable or media is included.

## Preceding independent Windows evidence and protected history

[Checkpoint199 Windows proof](../tests/results/M2/2026-10-11-native-outcome-recording-windows/summary.json)
retains the original actual MSVC job114485524060/run38144707175, all-four terminal
API and tested checkout. That original tree ran 69 suites in164.08s, including
original native outcomes3.22s. Its 51,584-byte/three-member packet binds18 reviewed
files and original merge`bd0eacf61cb22a581d46c8bd5338b84c29d1809f`. It executes the
preceding durable original-outcome code, **not this new startup path**. Later
parent refreshes need their own qualification.

[Protected PR134 history](../tests/results/M2/2026-10-11-render-end-current-main/summary.json)
records the ordinary protected squash to MAIN`387df5847b8726f9071380bcf541584ac1efc49e`,
matching the whole tested tree/base and17 reviewed blobs. Its147,898-byte/eight-member
packet retains all four byte-exact original jobs, binding, admission and merge
receipts from38143651032. No policy change or bypass occurred. The original
Windows caller807 take-group wait and interrupted-parent recovery discrepancy
remain unresolved by these fresh greens.

## Refreshed PR135 failure remains a merge gate

The ordinary parent merge to `1f4f6ed6923d61ce9a4da5e3674f5fe1ae56ab03`
inherits MAIN134 and resolves five conflicts with their original stages retained.
Fresh run38145862264 passed Linux, Windows desktop and Windows cross-build, but
native114488944113 failed manual-recording-control (21.23s) among68 tests151.47s.
The concurrent group wait timed out at caller808, take12/identity13, occupied1,
transport status1, callback_failed0. The reported file sizes do not identify which
owner/condition prevented group delivery. Neither a causal fix nor scheduler-only
failure is established. Original assertions/deadlines are unchanged; do not retry
or increase them to count this cohort as qualified.

[Original failed cohort](../tests/results/M2/2026-10-11-render-context-current-main-failure/summary.json)
retains all four full byte-exact jobs, original terminal API,19 reviewed blobs and
tested tree/parents binding. PR135 remains draft and unmerged. First inspect a
bounded, identity-bound control-owner diagnostic for take phase, pending-start
credit, consumer attempt/join state, worker completion/written extent and producer
EOF. Use original writer phase observations to distinguish unfinished capture,
worker completion, disk finalization and control retirement. Keep original
failure receipts before changing production or fixture behavior.

## Next implementation task and remaining gates

Implement an explicitly bounded **native-only warmup lease** in the SDK renderer:
actual Start, actual moving clock observations, original successful silent-release
receipts and separate warmup counts. It must not invoke a normal content callback
with fictitious PCM or confuse startup/finite end-guard silence with warmup. Add a
generation-bound atomic content-ready handoff after whole-FIFO publication and
make post-admission underflow abort. Preserve actual unsuccessful Stop/drain/loss
and owner-retirement evidence. Extend native transaction state through an explicit
versioned migration for these receipts and startup disposition.

The existing SDK primes normal content before Start, so this prepared path alone
does **not** solve actual native activation. Short takes/Stop during clock warmup,
bounded owner pacing, actual native FIFO sizing, installed Linux/Windows checks,
professional low-latency behavior, complete EQ/mix execution, recording alignment,
punch/loop workflows and processing-quality tests remain gates. The default
occupancy and kernel context are not accepted as professional latency parity.
All remaining M2–M11, F/Q/C/N, X004/X005 and all-Europe work stays in scope.

See [ADR132](decisions/132-monitor-startup-preroll.md).
