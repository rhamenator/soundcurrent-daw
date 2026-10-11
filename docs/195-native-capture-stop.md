# Native capture Stop coordination and original end receipts

The processing owner can now request capture Stop at its finite monitoring
horizon, on cancellation, or on a processing fault. The actual WASAPI source
has bounded polling hooks and reports its original owner-end facts. This is
original GPL-3.0-only C++20 code. The Windows duplex factory remains disabled;
these hooks have not yet been assembled or qualified as a native user workflow.

## Owners and admission

`PreparedMonitorCaptureStop` contains two fixed cells: processing publishes one
immutable request, and the SDK capture owner publishes one original end receipt.
Acquire/release publication makes the complete scalar records visible. There is
one writer for each cell, no reuse/reset, allocation, blocking lock, SDK call or
I/O in these prepared methods. An optional resource ledger charges the whole
object before construction. All owners join before settlement and retirement.
A missing owner receipt settles as OwnerLost, never as an invented successful
Stop or a fabricated HRESULT.

Admission matches the processor's run generation, source rate, channels, actual
warmup, target queue depth, maximum packet size and capture monitor-bank slots.
The input ring must hold at least:

```
max(warmupInputFrames, targetQueuedSourceFrames)
    + (monitorSlots + 2) * maximumPacketFrames
```

The bound covers the threshold plus one whole input push, every bank slot and
one native packet racing a stop check. It requires the SDK owner to check before
**each** lease, including catch-up batches. It bounds retained input at the
horizon; it does not guarantee disk/render throughput, avoid bank-full faults,
admit unlimited stalls, or impose a wall-clock Stop deadline. Bank exhaustion
remains an explicit fault with its original prefix, rather than growth or drops.

The first request preserves kind, generation, planned exclusive monitoring end,
generated monitoring end and accepted monitoring input count. Horizon requires
planned = generated. Stale generations, invalid kinds, repeats and requests
after owner completion refuse without mutation. A later fault or Cancel cannot
replace an already published Horizon request; its separate outcomes remain
observable. Monitoring ordinals are not original capture or project positions.

## Actual SDK lifecycle

`WasapiCaptureStream` prepares `PreparedWasapiCaptureLifecycle` before its SDK
thread starts. Existing three-field callback callers remain valid; optional
`shouldStop` and `finished` hooks permit the future owned context to compose the
handoff. Polls run before Start, at loop entry, after wake/timeout and before
every `GetBuffer` lease in the existing 16-lease catch-up quota. At most one
packet can race an individual check. Packet bookkeeping occurs only after the
actual release/copy path reports that the consumer callback was invoked.

The end callback runs once after actual Stop/owner exit and before thread join
and native object retirement. It preserves actual Start success, the first
stream failure/operation, actual Stop HRESULT if Stop was called, owner exception,
counter overflow, callback-delivered packet/frame counts and the last original
packet's flags/device position/QPC. Delivered means callback invoked, not raw
acceptance, drained device backlog or project end. Wide native positions and QPC
are never narrowed or extrapolated. Nonzero successful HRESULTs are retained.
Unstarted exits contain no manufactured Stop HRESULT. Duplicate end/Stop facts
do not overwrite the first original record; post-end bookkeeping is inert.

`requestStop()` signals the existing event from serialized control without
joining; `stop()`/destruction still join. This OS signal is not part of the
prepared RT contract. `endAfterJoin()` exposes the actual joined receipt, or
nullopt if the owner has not joined/reported. It must not race another control
operation. With no source event and no control signal, active polling may await
the existing two-second timeout. A prepared inactive owner needs its controller
to signal/activate/join it. These are lifecycle limits, not hidden guarantees.

## Processing and completion order

Processing publishes Horizon immediately when the kernel generates the reserved
end, before waiting for pending PCM publication/render drainage. Fault and Cancel
also request capture Stop without invoking SDK/control operations. A failed or
inconsistent original SDK end becomes CaptureEnd while preserving an earlier
processing cause. Cancellation and retained SDK failure remain separate facts.

The context must publish the original SDK end **before** capture EOF. Otherwise
EOF could allow successful processing completion to hide a failed SDK Stop.
The fixture does this ordering explicitly. The production context, native render
finite lease/drain, and durable SDK-end binding in the recording transaction are
still implementation gates; the raw journal alone is not that binding.

## Retained qualification

Frozen source `4e6cba432c871a118fe10f19ef50d83941f8f388`, tree
`6b5c2fc228dcbefefa767adaec07e3e53ec23015`, passed five suites in each profile:

| Profile | Seconds | Capture Stop checks | Control | Processing | Combined |
|---|---:|---:|---:|---:|---:|
| Release | 0.40 | 3,880 | 6,158 | 489,463 | 9,251 |
| ASan/UBSan | 1.97 | 3,880 | 6,158 | 489,463 | 9,251 |
| ThreadSanitizer | 3.53 | 3,880 | 6,158 | 489,463 | 9,251 |

Packet-copy is the fifth suite; no new per-check count is invented. Prepared C++
and interposed libc allocation/free/blocking-lock audits are zero. The Windows
SDK cross-build compiled the modified actual capture source and lifecycle.
Compilation does not qualify SDK execution or native Stop behavior.

The [61,922-byte/16-member packet](../tests/results/M2/2026-10-11-monitor-capture-stop/summary.json)
binds 577 frozen Git and checkout inputs, checkout transformations, 15 executable
hashes, four Windows object/library hashes, full build/CTest/LastTest logs,
profile configurations and the qualification script. No executables or media
are included. Earlier initial source qualification remains archived locally;
these counts/hashes refer to the final frozen getter revision above.

Four actual threads run fake-SDK capture through the original packet-copy lease
path, disk writing, processing and render-bank consumption. Three barriers impose
400 rounds with synthetic clock observations spanning four logical seconds;
this is composed ownership evidence, not uncontrolled pacing/native timing.

- Healthy ends 0, 7, 512 and 3,072 generate exactly those monitor lengths while
  retaining respectively 64, 512, 960 and 3,520 original raw frames.
- Successful PCM matches an independent direct buffer bit for bit; original
  raw samples, metadata, hashes and quiet-NaN payloads remain unchanged.
- Source-ring bound 1,664 is admitted and 1,663 refused for the tested geometry;
  generation, geometry, budget and joined resource retirement are checked.
- Cancel, buffer failure, failed SDK Stop, combined earlier buffer fault/later
  SDK Stop failure, before-start exit, failed Start, owner loss, duplicate end
  and inconsistent receipt retain separate original results and first causes.

The stdout label “actual postroll” reports fixture counts against a nominal
one-to-one boundary. It is not an admitted raw/project alignment or a hardware
latency measurement. Failed runs' delivered monitor prefixes are diagnostic,
not playable project transactions.

## Core-only build correction

The first stacked132 cohort38133989863 exposed a real configuration failure:
cross job114453966338 could not link `-lsc-media` because the new composed disk
fixture was defined even with `SC_BUILD_MEDIA=OFF`. Its original full job/API,
head138f11d and tested mergece9bd056 remain retained. This cohort cannot qualify
the PR. The SDK-source-only cross-build above did not cover this all-target link.

Revision `679a644a493e4f2ab40ce3715315459c45e741ef` creates that fixture only when
its `sc-media` dependency exists. Media-enabled Linux and native Windows jobs
retain the same complete fixture; the portable processing/control code and
tests are unchanged. The complete core-only Windows cross-build passed, and
all five media-enabled Release suites passed again in0.37s with the same3,880
capture Stop checks. The [27,042-byte/16-member correction packet](../tests/results/M2/2026-10-11-monitor-capture-stop-media-gate/summary.json)
binds this frozen CMake revision, full local build/CTest/configuration records,
executable hashes and the original byte-exact failed job/API/review. The earlier
sanitizer evidence remains evidence for the unchanged production C++/fixture;
no new sanitizer execution or SDK runtime is claimed for the CMake-only fix.
A fresh PR cohort remains required before admission.

## Next gate

Assemble the actual prepared capture/fanout/clock/stop context after SDK format
admission. Publish SDK end before EOF, specify the separate native render
lease/padding/drain boundary, and persist the original SDK end in the recording
transaction. Qualify short/long Stop, Cancel, failure/loss, joins and retirement
on actual native adapters and installed desktop workflows before activation.
Raw-to-project mapping, playable finite-prefix admission, repeated punch/loop,
EQ/mix execution, processing quality, localization and the full M2–M11 scope
remain open. See [ADR127](decisions/127-native-capture-stop.md).
