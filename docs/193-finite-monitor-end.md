# Reserved finite monitor output boundary

`PreparedMonitorBuffer::planEnd` and `PreparedMonitorProcessing::planEnd` reserve
an explicit exclusive monitor ordinal before it has already been generated.
This is original GPL-3.0-only portable C++20 processing code, with no dependency,
driver, raw journal or project schema change. It extends the admitted owner
workspace; reservation itself uses bounded POD work with no allocation or lock.

## End reservation and open input

Only the serialized processing owner calls these methods. A GUI/native control
thread must deliver its request through an owned bounded handoff; concurrent
direct calls are not allowed. An accepted end is immutable. Repeating that same
end is idempotent before closure; a different end, an end behind already-generated
PCM or a terminal/closing owner is refused without changing progress. Already
submitted hardware audio cannot be retracted. The end is an opaque monitoring
ordinal, never an inferred raw position or project time.

The buffer caps its current/future64-frame grids at that end, including a grid
that has already generated only part of its output. Input remains open. At the
horizon, `WaitingEndInput` performs no kernel progress or invented output. Actual
postroll can still be copied into the admitted source ring. Matching final input
closure publishes/drains the genuine last partial grid; a conflicting end cannot
replace the reservation. Insufficient real input fails rather than padding zeros.
The pinned converter remains causal with end_of_input zero.

The processing owner continues bounded whole-packet acquisition at the horizon,
even above its normal occupancy target. Each step copies one leased packet or
performs its existing one service/output handoff. A full source ring at the
horizon cannot justify more DSP beyond the end: it records PostrollCapacity plus
an InputOverrun buffer receipt, releases the actual held monitor lease and
preserves the accepted original raw prefix. It does not drop/trim postroll or
allocate a larger ring. Native admission must bound the intended postroll and
pressure against this finite capacity. The component does not choose that native
postroll policy or stop the source device automatically.

After original capture EOF and the drained monitor route, planned runs close
automatically at the reserved end. A positive end still establishes the original
clock fit/ratio event before kernel progress, including a short final-only grid.
A zero-output end needs no fabricated fit or generated audio. Existing age/loss
checks remain active while waiting. Clock receipts can refresh during output
pressure; scheduled ratio/input/DSP progression stays blocked by pending PCM.
Cancellation and late source/clock failure retain their existing terminal receipts.
Unplanned runs keep the earlier explicit finishAt/NeedEnd contract.

## Acceptance evidence

Frozen source `726886b703d7d8992523d78fcb097b28709cdf33`,
tree `594e7dfee8dc798e73b224b188ed07f65ff50a76`, passed three suites per
profile: Release2.67s, ASan/UBSan23.86s and TSan35.14s. Each profile records
3,845,710 buffer checks,489,463 processing checks and9,251 combined recording
checks. Prepared C++/interposed libc allocation/free/blocking-lock audits are zero.
Existing compiler warnings remain in the logs.

The [66,710-byte/17-member packet](../tests/results/M2/2026-10-11-monitor-finite-end/summary.json)
binds569 selected inputs, nine executable hashes, complete compilation/CTest/
LastTest/profile records and the first failed fixture source/log. Standalone
gzip/member and frozen Git/checkout verification passed. Executables and test
media are excluded.

- Direct buffer: reserve inside an already-generated partial grid, refuse a late
  or conflicting end without mutation, cap its16-frame result, wait with input
  open without kernel progress/output writes, then close the matching end.
- Processor: zero, seven and512-frame ends, idempotence, conflicting/late/terminal
  refusals, mid-run reservation while capture is still open, automatic EOF,
  insufficient input, capacity loss, cancellation while waiting and stale clocks.
  Successful short/normal output is bit-exact with an independent direct buffer.
- Combined disk/clock fixture: four actual owners under its explicitly phased
  logical schedule,4,004 original frames/256 packets and exactly3,072 monitor
  frames. It consumes518 original observations,406 pressure calls and actual
 932-frame postroll. Original sample/metadata verification and nonfinite/clock
  failure prefix checks remain. No end overshoot or fabricated input is accepted.

The first new dynamic-reservation test incorrectly supplied EOF before waiting
for its reservation point, reaching the original NeedEnd contract. That failure
and source are retained. The corrected fixture keeps capture open until its
mid-run reservation, then supplies real EOF; no production deadline/assertion was
suppressed. The prior checkpoint192 late3,072 end remains valid refusal evidence;
the new early reservation now reaches that end without overshoot.

## Limits and next task

This is one finite processing run, not repeated punch/loop end scheduling or a
native/GUI Stop workflow. No actual SDK activation, control-to-processing request
handoff, hardware latency/alignment, uncontrolled pacing/disk stalls, installed
preview is qualified. Earlier
[Windows combined recording](192-combined-monitor-recording.md) tested the prior
explicit-end revision. EQ/mix integration, recording mappings, portable finite
playable admission, DSP quality and European translation/UI gates remain open.
See [ADR125](decisions/125-finite-monitor-end.md).

Actual MSVC job114440502333/run38129436958 subsequently passed 63 synthetic
suites in147.65s, including combined recording1.20s and processing0.15s. The
[43,913-byte/three-member packet](../tests/results/M2/2026-10-11-monitor-finite-end-windows/summary.json)
retains full job/API evidence and18 reviewed Git blobs, with exact tested
merge/tree/parents and matching frozen finite-end source. This tests the new
reservation, separately from the prior explicit-end Windows run. It is stacked
qualification; current-MAIN admission, SDK/hardware and installed workflows
remain separate. Per-check counts and executable hashes were not exposed or
claimed by this Windows job. No executables or test media are included.

[Checkpoint194](194-monitor-stop-handoff.md) now supplies portable generation-bound
Stop intent delivery to the processing owner. Next compose it with the native
capture/render lifecycle. Admit real postroll/capacity before activation,
reject stale/late requests with original receipts, and qualify actual endpoint
startup, short/long Stop, cancellation, loss, joins and retirement on Windows.
Keep the native duplex factory disabled until SDK and desktop gates pass.
