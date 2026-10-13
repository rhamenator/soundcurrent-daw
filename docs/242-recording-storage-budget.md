# Prepared recording storage budgets

Checkpoint 242 adds explicit storage-stall admission to the owned recording backend.
The full professional-suite goal remains active. This is a backend checkpoint;
there is no new desktop budget control or installed preview in this change.

## Why this is required

Original CI recordings have measured storage pauses longer than their 8-packet,
80 ms fixture buffers. PR174 includes a 2.315 s raw checkpoint followed by
0.520 s paired publication. PR175 has one remaining failure in each Linux and
Windows native job: the recording controller. Its Linux raw checkpoint is
549.856 ms, followed by 300.972 ms paired publication; Windows measures
130.003 ms and 44.388 ms respectively. Callback lateness is retained separately.
These observations do not establish that every earlier native failure had the
same cause.

Original PR175, source `674a972418265d0c39c91a8fcf2969da7c7a11f0`, run
`38335658217`: Linux 153/154, Windows native 80/81, Windows desktop 20/20,
and cross-build successful. Both failing jobs identify the controller test.
The raw crash-selector correction passes independently on these runners.
Original terminal APIs, job logs and selected artifact metadata are retained;
original Windows archives stay local. A later pass does not replace this evidence.

## Admission and callback contract

`RecordingStorageBudget` declares rates, capture width, maximum capture packet,
minimum capture/render callback sizes, stall milliseconds, slot ceilings and a
PCM payload quota. The standalone planner permits 1–60,000 ms, 1–4,096 slots and
1–64 safety packets. The owner integration currently requires **two safety
packets**. Unsupported geometry, capacity or memory is refused before preparing
native owners or starting recording.

Required packets per lane are:

```
ceil(rate * stallMilliseconds / (minimumCallbackFrames * 1000)) + safetyPackets
```

PCM is charged for every admitted raw slot at the maximum capture packet width.
Runtime SPSC arrays are allocated and initialized during preparation, rounded to
powers of two, with a hard ceiling of 4,096 cells. Capture banks issue only the
requested slot IDs; receipt queues independently enforce the requested occupancy.
Rounding does not increase usable capacity. The resource ledger charges the
actual rounded queue arrays, PCM and route payload before constructing owners.
Payload accounting is not a measurement of resident process memory or allocator
overhead.

The optional owner budget determines raw slots and expands receipt capacity as
needed while preserving an already larger receipt declaration. In its absence,
legacy slot declarations remain available, with **no duration claim**. Clock,
control and monitor queues retain their separate bounds. Memory admission applies
to the complete route as well as the planner's narrower PCM quota.

Actual callbacks below an admitted minimum fault as `InvalidQuantum` before raw
publication or output consumption. The original clock remains in the failure
facts; render buffers are silenced. Actual rates and maximum sizes retain their
existing checks. An audio system that changes geometry needs a newly prepared
route. Capacity is finite: repeated stalls, insufficient sustained disk throughput,
late scheduling, or other queue pressure can still fail. No samples, deadlines,
quality checks or durability requirements are relaxed.

## Persistence

New owner manifests use **1.2**. Intents preserve minimum capture/render sizes,
stall duration and effective queue counts. Reopening validates that the saved
counts support the declaration. Outcome encoding remains 1.1, including completion
returns and clock evidence. Readers support 1.0 and 1.1 without inventing a storage
duration; those versions retain their original smaller queue limits. Older readers
must refuse 1.2. Unknown revisions, changed artifact hashes and inconsistent
capacities are refused. Inspection does not rewrite the source recording.

The owner exposes immutable prepared capacity and charged route bytes for a
future desktop presentation. These are off-callback queries.

## Acceptance evidence

Source: `6c28e9a35ddb4a4f8282f3942ff63bb2f1c366fa`, including the earlier planner
and prepared-queue commits. [The evidence packet](../tests/results/M2/2026-10-13-recording-storage-budget/index.json)
contains 118 compressed text/configuration/metadata members, hashes for 16 local
test executables, and binds 618 source inputs and retains
initial failures and repairs alongside passing qualification.

Release gates:

- Planner: geometry, 44.1 kHz, channel-dependent PCM cost, exact quota, invalid
  minimum and unsupported duration/slot count refusal.
- Prepared SPSC: unsigned counter wrap, full/empty behavior and 100,000 ordered
  concurrent items. Callback allocation/free/lock audit remains zero.
- Capture bank, native receipt lanes and planar capture/render pass. Receipt tests
  fill and drain 300 and 4,096 requested cells, preserve the first rejected record
  and accepted prefix, verify rounded payload cost and reject undersized quotas.
- Final owner/controller/state/stall selection: **4/4 in 9.79 s**. Normal recording
  fixtures explicitly request a three-second budget. Smaller queues remain covered
  by pressure tests and the explicit 50 ms overrun fixture.
- 350 ms injected pause with a 3,000 ms budget: raw 302 slots, receipt 302 slots;
  all accepted raw samples and finite monitor output remain intact, finalized,
  reopenable and verified bit for bit.
- 250 ms injected pause with a 50 ms budget: raw 7 slots and configured receipt
  capacity retained at 64. Raw `QueueFull` is reported; all accepted raw frames
  are committed and bit-exact. Native owners join and payload credit retires.

Two initial fixture setup failures were corrected: the three-second planner case
retained the preceding smaller exact quota, and the legacy-state probe read an
outcome artifact still modified by a corruption case. Another run exposed receipt
pressure before raw pressure when admission reduced the configured receipt lane;
preserving its larger configured capacity fixes that preparation error. This does
not guarantee that arbitrary metadata overruns preserve every accepted raw tail;
only verified committed prefixes can be offered for recovery.

Fresh ASan/UBSan core build (65 steps): **6/6 in 0.51 s**, with
`ASAN_OPTIONS=detect_leaks=1:halt_on_error=1` and
`UBSAN_OPTIONS=halt_on_error=1:print_stacktrace=1`. It covers the planner, prepared
SPSC, capture bank, receipt lanes and planar boundaries. One subsequent test-only
line break removes an indentation warning; its render gate is requalified
independently (**1/1 in 0.06 s**, source `ad21107`). No production source differs from the qualified engine revision.
The durable owner/controller suite is Release-qualified here, not claimed to have
run in this new sanitizer profile. Exact logs are in the evidence index.
No physical audio, native SDK stream, VM or Windows hardware is exercised locally
by this checkpoint. Windows CI separately includes the new capacity and stall gates.

## Next concrete implementation task

Add an inactive-only desktop storage budget control, showing duration, enforced
minimum callback sizes, effective raw/receipt capacity and charged payload before
activation. Do not infer a minimum from a maximum packet size. Refuse unsupported
requests visibly, refresh actual graph geometry and prepare a new route after a
quantum change. Update real localization extraction after any UI source edit.
Qualify the panel on Linux and Windows; keep capacity refusal and all smaller-queue
loss reporting tests. Then continue interrupted-original copy/review, native Windows
playback and remaining M2–M11 work.
