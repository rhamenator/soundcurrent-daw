# M2d4c1: bounded recording-writer diagnostics

This checkpoint adds optional worker-side observations to investigate the native
recording backlog retained by M2d4b. It preserves the 32-track duration contract,
fixed capture pools, memory budgets, one-second checkpoints and durable publication.
It does not prove the cause of the original uninstrumented failure or complete
M1/M2, physical/Windows qualification or frozen-reference parity.

## Ownership and observation contract

`CapturePipe::consumerBacklog()` is disk-owner-only. It reads the existing SPSC
publication counters, plus its own acquired packet. Ready slabs exclude the
acquired slab and the producer's unfinished slab. Ready frames are an upper bound
because the final ready slab may be short. Queued upper-bound frames include the
acquired packet's actual extent. The configured pool capacity is explicit. This
adds no audio counters, shared atomic read/modify/write operation, clock query,
allocation, blocking, disk, GUI or logging to `push()`/`finish()`.

`RecordingOptions::instrumentation` is optional and defaults to no observer.
A noexcept callback receives writer phase, written/durable cursors and the
consumer backlog. The context must outlive construction and joined writer owners;
callbacks must remain bounded. Construction checkpoints mark the pipe/backlog
absent. They do not invent a producer or ready queue. The writer observes paired
write/hash, audio flush and journal publication phases. The write end precedes
slab return; audio flush follows slab return. Journal end follows publication and
updates the durable cursor. Final publication is observed separately after media
publication. Existing cancellation/fault boundaries and receipt semantics stay
unchanged. Observations also pair the worker's idle waits, including scheduling
latency. No observer runs on the audio owner.

The test-only `WriterTiming` helper owns four fixed phase slots, sample counts,
maxima and the corresponding start/end monotonic time, cursors and queue facts.
It retains sampled queue high-water marks and the largest write-to-write gap.
That gap includes checkpoint/idle/scheduling time; it is not labeled pure disk or
OS service time. Phase wall time includes observer overhead/preemption and any
explicit fixture stall. Missing/repeated/backwards/unfinished pairs remain visible
and deny complete phase-pair coverage. No unbounded trace or per-event allocation
is introduced. Inspection/output happens after disk joins.

The native callback timer also retains its maximum callback's start monotonic
time and native clock position/duration/ID/cycle/nsec/rate in one fixed slot,
even when its full-sample storage overflows. This permits scoped comparison with
worker maxima. Overlap alone cannot prove that a disk operation caused callback
preemption. Existing nearest-rank timing limits and full-coverage gates remain
unchanged; normal production audio acquires no diagnostic clocks.

## Controlled journal stall

The native `writer-stall` mode uses the same 32 mono arms, three non-flat EQ bands,
one file, stereo matrix and independent disk sink as the normal qualifier. It
holds lane 17's first journal-publication observation at/after 48,000 written frames
for 4 seconds, outside audio. This deliberately exceeds the 4096-frame/32-slab
pool horizon of 2.730667 seconds. It is fixture behavior, not a production fsync
failure or a general-storage measurement.

Acceptance requires the initiating fault to name lane 17 and retain CaptureFailed,
QueueFull and rejected frames without a native XRUN/discontinuity. The phase
report must retain at least 4 seconds and 32 ready slabs at journal end. All writers
must join, finalized inactive journals retain their independent full extents and
origin, every raw source sample/hash is verified and the stereo common prefix
matches private offline EQ plus independent matrix accumulation exactly. No raw
prefix is trimmed to a common length and the canonical project stays unchanged.
Callback allocation/free/blocking-lock audits remain mandatory. Owned nodes and
links must retire; defaults and prior links remain preserved.

## Qualification and next action

Targeted tests cover partial/full/acquired queue accounting, final short slab
upper bounds, actual phase ordering/cursors/slab return, construction exclusion,
observer exclusion from audio, exact RF64/source/checkpoint preservation,
phase pairing/maxima/context and missing observations. Full ordinary suites are
required because production recording and worker code changed. Native fixtures
run serially after all builds/tests are terminal; cross compilation alone cannot
qualify Windows native audio or Qt behavior.

All 27 ordinary Debug groups passed in 23.95 seconds and all 27 ASan/UBSan/LSan
groups passed in 66.10 seconds. Four targeted Release groups also passed. Windows
headless compilation/linking passed; Windows execution/native/Qt remains open.
Native code uses unchanged owned software routes on Dummy-Driver at 1024/48000,
with observed native data loops at SCHED_RR priority 20. CPU/native experiments
were serial; competing load was not controlled. No physical interface is qualified.

The ten-second normal probe verifies 15,360,000 raw and 960,000 output samples
exactly, Save/reopen and peak 3.75449, with complete timing coverage and finite
thresholds met. The controlled stall verifies 5,798,912 full raw and 360,448 common
output samples exactly, preserving raw extents 180,224..181,248. Lane 17's journal
interval is 4.003536049 seconds: ready slabs grow 0→32 while its durable cursor
moves 0→49,152 only after publication. The largest write gap is 4.018562400 seconds.
Every phase pair completes. Zero host-owned callback allocation/free/lock calls;
canonical state, defaults and prior links remain unchanged and owned nodes retire.

The unmodified 120-second diagnostic then FAILED after 2,785,280 audio frames
(58.026667 seconds), initiating lane 0. All 32 raw lanes and the independent sink
exhaust their fixed pools, every lane retains 1,024 rejected frames and the device
clock is contiguous, ID 30, 1/48000, with no XRUN/discontinuity. All writers join.
Owner complete 2721-callback p99.9=6.887259 ms, max10.147618 ms (47.567% of period)
passes the finite callback thresholds for this interrupted prefix. It does not
qualify the requested duration.

The phase data establish a checkpoint backlog in this instrumented run. Lane 26
at written 2,654,208/durable 2,605,056 spends 2.469220532 seconds in audio flush
(ready 0→28), immediately followed by 1.803449198 seconds in journal publication
(ready 28→32). Both have released the acquired slab. Its observed write gap is
4.272674676 seconds; other raw writers and the sink also reach 32 ready slabs.
This exceeds the 2.730667-second pool horizon while the source continues. The
largest journal interval across all workers is 2.287432765 seconds during final
drain/teardown; do not mislabel that maximum as the initiating checkpoint.
The largest write/hash interval is 141.791776 ms, idle wait 6.699143 ms. Phase
elapsed time does not isolate kernel filesystem service, descriptor flush versus
header updates, or scheduling/preemption. The original uninstrumented long run's
precise cause remains unproven; these new facts are scoped to this new run.

Read-only verification checks all 89,128,960 full raw and 5,570,560 common stereo
samples exactly, hashes, inactive journals, identities and origin; peak 4.16414.
Each raw prefix is 2,785,280 frames. Aggregate32,768 rejected frames remain recorded.
All 105 original file content/extent snapshots match before/after. The original
canonical one-asset project is unchanged; nothing is trimmed, reattached or saved.
Verification takes6.0500 seconds outside native callbacks. The first supervisor
attempt rejected the new mode at argument parsing before launching any native
child; its choices were corrected, its log retained and the tested source snapshot
was refreshed before the successful probes.

[Evidence](../tests/results/M2/2026-10-06-writer-backlog-diagnostics.json) retains
exact source hashes before native launch, complete diagnostics/phase contexts,
callback worst-clock/start facts, scheduler/load/routing data, tests and original
file receipts. All 24 reviewed equalizer inputs/heads still match read-only.
No dependency/license change, equalizer writes, publication or parity promotion.
M2d4b's original long-run pool exhaustion and subsequent maximum-budget miss
remain open until stronger evidence resolves them. The diagnostics enable the
next storage-burst/worker-policy decision; they do not make a shorter native run
satisfy the unchanged 30-minute sample/deadline gate.


Next: **M2d4c2 durable checkpoint burst policy**. The observed checkpoint gap
exceeds the current pool; evaluate phase staggering and an explicit, aggregate-
budgeted storage-burst reserve. Preserve the one-second durable checkpoint
contract, raw timestamps, live monitoring latency, fixed RT bounds and independent
failure/recovery receipts. Test absorption of the declared 4-second stall and
visible exhaustion beyond the admitted reserve, then retry the unchanged30-minute
native sample and deadline gates. Larger buffers alone do not establish sustained
storage throughput; long-duration evidence remains required.
