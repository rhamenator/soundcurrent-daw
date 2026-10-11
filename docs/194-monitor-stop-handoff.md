# Owned monitor Stop request delivery

`PreparedMonitorControl` delivers Stop and Cancel intent from one serialized
control owner to one serialized processing owner. The processing owner returns
an original-request receipt. This is original GPL-3.0-only C++20 code; it adds
no dependency, driver, raw journal, project schema or GUI workflow.

## Ownership and admission

Prepare both queues before processing starts. Their configured generation must
match the processing run. A started, finished, failed or mismatched handoff is
refused during processing admission. The fixed storage includes two 64-entry
SPSC queues, one retained in-flight result and 65 joined diagnostic dispositions.
Each lane admits 1–64 entries; queue counters may wrap independently of request
IDs. The complete object is charged before construction against an optional
resource ledger. This separate handoff charge is additional to the processor's
existing charge. Construction, settlement and retirement happen off RT.

Only the control owner submits requests and reads replies. Only the processing
owner calls `pumpControl`/`step`, `planEnd` or `cancel`. These owners may be
separate threads; neither API permits a second producer or consumer. External
handoff, clock, capture and output storage outlive the processor. Join all owners
before settlement or destruction. Never reset/reuse this handoff for another run.

Each original intent contains a run generation, a nonzero request ID, kind and
explicit exclusive monitoring ordinal. Within the matching generation, IDs
strictly increase without wrap. A refused ordered request consumes its ID;
retry with a new ID. A stale generation does not advance the current sequence.
`UINT64_MAX` is supported as the final ID of a run. Invalid kinds, duplicate/zero
IDs, stale generations, conflicting/late ends and terminal owners get explicit
receipts. No capture/native/project position is inferred from an ordinal.

## Bounded action, pressure and failure

Submit Accepted means the intent entered the queue. It does not mean Stop was
accepted or that a device stopped. Submission pressure is a nonmutating refusal;
the caller retains the request and can retry. The processor reserves reply
space before popping/applying one intent. Receipt pressure leaves that original
intent queued and returns `ControlFull`; no further input copy, kernel service
or PCM publication occurs on that step. Clock observation pumping can continue.
The processor's existing route, QPC monotonicity and clock-age checks run before
this pause, so pressure cannot hide source loss or stale clocks.

One handled request consumes the step's action budget. Stop delegates to the
existing immutable finite-end reservation. Cancel delegates to the existing
monitor cancellation, preserving accepted raw capture. A receipt retains the
original request plus the generated end/published-frame snapshot from before the
action. Planned is a monitoring boundary reservation, not a native Stop/drain,
hardware alignment or successful completion receipt. Already generated/submitted
audio cannot be recalled. Native postroll/capacity policy is still required.

Producer finish closes submission while accepted requests drain. Terminal
processing owners still reply with refusals; a native/controller lifecycle must
service or explicitly retire that prefix before leaving its owner loop. Owner
loss is a terminal control fault. An action already in flight may finish; loss
does not roll it back. Unread replies remain readable after loss/settlement.
After joins, settlement preserves every accepted request without a queued reply
in the bounded diagnostic prefix. Unhandled requests are marked Abandoned; the
defensive failed-reply path preserves its actual result and before-action
snapshot. No unobserved failure is represented as successful Stop. Joined
accounting requires accepted = replied + retained dispositions. Settlement is
idempotent. Concurrent counter snapshots are observations, not an atomic balance.

## Qualification

Frozen source `220bb1e064d0e29383ae08b4afcc8c411d2e15a5`, tree
`f1b9fc78d8af39bb618ccb953567baf7c5d38a3f`, passed three suites in each profile:
Release 0.31s, ASan/UBSan 1.73s and ThreadSanitizer 3.32s. Each records 6,158
control, 489,463 processing and 9,251 combined recording checks. Guarded C++
and interposed libc allocation/free/blocking-lock counts are zero. Existing
dependency/compiler warnings remain visible in the packet.

The 59,080-byte/13-member [retained packet](../tests/results/M2/2026-10-11-monitor-stop-handoff/summary.json)
binds 572 frozen Git inputs, checkout transformations, nine executable hashes,
complete build/CTest/LastTest logs and profile configurations. Executables and
test media are excluded.

- Admission: generation, finished owner, lane geometry, memory budgets,
  unchanged resource usage on refusal and joined credit retirement.
- Original receipts: stale/duplicate/zero IDs, invalid kinds, same-end retries,
  conflicting ends, maximum ID, terminal refusals and before-action snapshots.
- Pressure/loss: one-slot request and receipt lanes, repeated nonmutating PCM
  pauses, cancel after receipt credit, both owner losses, stale clocks and capture
  loss during pressure; all accepted requests retain joined dispositions.
- Finite output: zero, seven and 512 frames, including mid-run late-end refusal
  after actual generation then a future reservation. Successful PCM is bit-exact
  with an independent direct buffer; all 1,024 raw frames remain unchanged.
- Concurrency: two actual threads exchange 20,000 generation-refused requests
  followed by accepted Stop and Cancel. Original order/fields/results and queue
  wrap are checked; prepared calls pass the RT audit. Yield/deadline orchestration
  is outside those calls. This is not uncontrolled native scheduling evidence.

## Remaining work

The native duplex factory remains disabled. This handoff does not stop an SDK
capture owner, deliver a render drain command or admit a source postroll budget.
No Windows execution of this new handoff or installed/native UI workflow is
claimed. Earlier Windows qualification of the
[finite boundary](193-finite-monitor-end.md) is separate.

Next implement generation-bound capture-stop and render-drain coordination with
an explicit admitted postroll/pressure policy. Preserve actual SDK receipts,
short/long Stop, cancellation, loss, joining and retirement evidence before
enabling the native factory. Original-to-project timing, playable finite-prefix
admission, repeated punch/loop, EQ/mix execution, processing quality and reviewed
European translations remain independent gates. The full DAW goal is incomplete.
See [ADR126](decisions/126-monitor-stop-handoff.md).
