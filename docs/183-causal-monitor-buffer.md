# Prepared causal monitor buffer

The engine now has an original GPL-3.0-only single-owner monitor buffer around
the unchanged pinned best-sinc kernel. This implements buffering and explicit
ratio scheduling; clock/buffer control, raw-writer routing, SPSC publication and
native Windows duplex composition remain to implement. No audible UI is enabled.

## Ownership, admission and input

`PreparedMonitorBuffer` reserves its state/event storage, input/output rings,
one64-frame pending block and512KiB/channel kernel allowance before allocating
and touching memory. The allowance is conservative payload credit, not measured
allocator/RSS usage. Even equal nominal rates prepare persistent filter state:
independent endpoints can drift. The first lease member retires last after all
payload/state owners; construction/destruction and shared-ledger calls stay off RT.

One serialized processing owner calls the prepared methods. Control/capture/render
threads must hand off events/input/output through separate prepared queues; they
cannot concurrently access this object. No GUI, backend, disk, logging, blocking
lock, dynamic allocation or exception is used by prepared methods. Bounds cover
work per invocation, not a proved audio deadline at every admitted channel/rate.

Input is copied as complete finite interleaved frames with original channel order
and floating headroom. InputFull accepts nothing and leaves the caller's source
ownership intact for retry after service; actual lost input is an explicit
InputOverrun fault. Nonfinite input faults without partial packet acceptance.
Raw capture bits/stamps must be routed independently before conversion; this
object creates no capture timing origin, native clock fit or project alignment.

## Causality, events and output

Warmup requires the declared retained-source reservoir plus conservative pinned
filter context and one output block's source demand at the lowest admitted ratio.
Capacity must cover that threshold. Live processing retains the reservoir and
uses actual received source only. Persistent filter history crosses input packets
and output-ring wraps. One service call invokes the kernel at most once, consumes
at most the admitted maximum packet span, and publishes at most64 output frames.

Ratio events have run generation, ordered unique output-grid coordinates, finite
range and bounded step relative to the last scheduled ratio. The bounded FIFO
holds64 events; invalid/full refusals do not mutate it. Defaults allow±2,000ppm
around nominal ratio and a1ppm step. Tiny floating comparison tolerance covers
arithmetic rounding only. A grid starts when processing begins, even before its
first output sample: retroactive changes are refused. Explicit ratio is held for
the whole64-frame grid through partial kernel calls. The later clock controller
must provide validated estimates and control stability/quality evidence.

Output rings support arbitrary admitted capacities and partial pulls. Priming
and NoOutput emit no fake silence or stale PCM. NoOutput alone does not identify
missing source: more processing service may be needed. The native deadline owner
decides when this is OutputUnderflow. Ring pressure returns NeedOutput without
consuming source or changing filter state. Output ordinals remain integer even
above2^53; they are opaque stream coordinates, not native/project timestamps.

## Stop, failure and retirement

`finishAt` declares an exclusive output-stream boundary at or beyond the produced
prefix. It releases the retained reservoir and drains only actual supplied input.
The kernel's `end_of_input` remains zero; no future zero padding is inserted.
Real postroll must suffice, otherwise InsufficientInput faults. A final partial
grid is published exactly once, including a boundary on already-generated pending
PCM. Extra supplied postroll can remain unused by monitoring; original raw capture
must retain its own extent. This is not a variable-rate recording-alignment map.

Cancel discards monitor-only backlog/events, retains accepted/consumed/generated/
delivered counts and accounts discarded monitor frames. Repeated cancel is
idempotent. The first fault retains its code, accepted-input count and output
ordinal; subsequent faults/cancel cannot replace it. Nonfinite output, impossible
kernel counts and arithmetic overflow are terminal. Fault pulls expose no queued
stale audio. Native loss/deadline faults are explicitly signaled by the owner.
Quiesce/join all composed owners before deleting this object or returning credit.

## Frozen qualification

Source `cc72594764a3835dceaf90ae154253f905b044d8` passed three Linux Release
suites in2.51s (buffer, stamped bank, clock correlation), ASan/UBSan in22.80s
with leak/halt-on-UB, and full C/C++ TSan in45.35s with halt-on-error. The buffer
is a single-owner object; this TSan execution is not a multi-owner race proof.

Each buffer run made366,972 bounded service invocations and3,845,658 checks over
13 synthetic streams. Long3-channel streams cover48→48,44.1→48,48→44.1 and96→48kHz
with1/17/257-frame input packets and17/7/1-frame pulls,1001-frame input/129-frame
output rings, events crossing FIFO wrap, reused/poisoned caller scratch and output
bases above2^53. A256-channel stream exercises admitted width. All output float
bits match a direct persistent fixed-grid kernel reference. This is a partition/
buffer correctness comparison, not independent spectral/phase/transient parity.

Admission/credit failure and retirement, warmup without invented silence, strong
input/output/event backpressure, invalid/stale/duplicate/range/step events, cancel,
nonfinite input/output, insufficient actual postroll, exact partial Stop, explicit
native loss/underflow/overrun faults and output ordinal overflow are covered.
Guarded prepared paths have zero C++ allocation/free and zero Linux interposed
libc allocation/free/pthread blocking-lock counts. Interception is test-only and
does not cover every library/syscall path. Windows execution remains independent.

The [33,942-byte packet](../tests/results/M2/2026-10-11-causal-monitor-buffer/summary.json)
retains15 members: incremental build/CTest/LastTest logs, actual frozen Release
compile/link transcript, C kernel compile commands,542 selected inputs and five
executable hashes. A fresh Release compile produced the exact previously tested
executable hash, so replaying identical bytes was unnecessary. No executable or
media is included. Standalone archive/member validation and all selected-source
byte checks passed. Inputs are not an exhaustive toolchain/system/dependency closure.

Reproduce with `cmake --build <configured-build> --parallel 2 --target
sc-monitor-buffer-tests`, then `ctest --test-dir <configured-build>
-R '^causal-monitor-buffer$' --verbose --no-tests=error`. Native MSVC CI explicitly
includes the target/suite; qualification requires its actual execution.

## Next implementation

Implement the clock/buffer drift controller from validated original capture/render
fits, uncertainty, age and occupancy with bounded events/slew. Then compose the
original bank/raw-writer route, monitor/output SPSC handoff, SDK owners and versioned
raw-to-project mappings, including finite/punch/loop/Stop postroll and partial
activation/fault retirement. Bind Windows duplex only after actual native desktop
qualification. [ADR117](decisions/117-causal-monitor-kernel.md) remains the architecture
direction; this buffer closes its prepared-storage/grid slice.

P005/M2, processing-quality failures, deadlines/load, native/installed/physical/
Windows/localization and full functional/quality/content/compatibility scope remain
open. No driver, signing certificate, new dependency or raw/project schema change.
