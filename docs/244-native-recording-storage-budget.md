# Native Windows recording storage budgets

Checkpoint 244 extends prepared disk-pause admission to the native WASAPI route.
Full Linux/Windows professional-suite parity remains active and incomplete.

## Capture storage is independent of output fill sizes

After successful SDK descriptor preparation, native route admission uses actual
capture sample rate, channels and maximum packet frames, plus an explicit minimum
capture packet size and requested pause duration. Raw slots are
`ceil(rate * milliseconds / (minimumPacketFrames * 1000)) + 2`, bounded at 4,096.
PCM quota and complete route payload quota are both checked before audio-bank
allocation, binding or activation. Already prepared native SDK owners retire on
failure. Minimums are not inferred from descriptor maximums.

WASAPI output lease size can vary with padding or be zero. It contributes no disk
receipt lane and no packet-count claim. Existing output/startup/clock bounds remain
separate. The native raw disk consumer retains original captured samples and stamps.
Both fanout banks enforce the prepared minimum before publication. A too-small
packet faults InvalidFrames with its original stamp, publishes no raw/monitor
frames, and signals capture clock failure without publishing a valid clock record.

The shared panel enables duration and minimum input packet controls for native
routes. Output minimum is disabled there. Actual rate/width/maximum packet and
payload are checked by the preparation worker; the panel does not query SDK formats
on the GUI thread. Record is available only after successful preparation. Quota
and format refusal produce visible messages. Immutable capacity/payload snapshots
and retired-memory handling are shared with the earlier PipeWire implementation.

## Persistence

Policy-enabled native takes use schema **1.4**, storing duration, enforced minimum
and effective raw slot count. Counts must exactly match the declaration. Capture
and render descriptors, raw-prefix binding, EQ/matrix artifact rights and original
joined outcomes retain their existing meaning. Outcome fields remain compatible
with the 1.3 representation. Legacy writers still default to1.3; old versions0–3
remain readable. Explicit migration to1.4 preserves absent policy and original
outcomes rather than inventing a duration. Unknown versions and inconsistent
capacity are refused. The raw capture journal format is unchanged.

## Qualification and known gaps

Focused native storage test passes2.55 s using actual production disk/processing
workers and simulated prepared SDK owners:

- 350 ms disk pause,3,000 ms budget:302 raw slots,99,840 committed raw frames,
  finite monitoring complete and raw float bits exact.
- 250 ms disk pause,50 ms budget:7 raw slots,3,360 committed frames; raw QueueFull
  reported. Accepted prefix remains exact and joined owners retire their credit.
- PCM quota refusal prevents activation; actual descriptor geometry controls the
  plan. Saved1.4 policy reopens, forged slot count refuses, idempotent migration
  preserves raw hashes. Callback allocation/free/lock audit is zero.

Planner, native outcome/interruption/migration, capture-bank, WASAPI capture binding
and WASAPI render gates passed in the first selection; the initial storage scenario
failed monitoring clock startup and remains retained. Two runs with a2,400-frame
monitor horizon were shorter than the4,097-frame native startup fill. They finalized
exact raw96960 but failed render clock startup. Using the established8,192-frame
finite range isolates storage qualification. It does **not** fix or qualify short
takes; this remains a concrete required implementation task.

Final source `c871ea598639bf940abf15e0a29dd892486cb1f0`: fresh Release desktop
build189steps plus final incremental stamp validation. Desktop panel13.21 s,
localization0.66 s and native storage2.57 s pass together3/3 in16.44 s.
Final changed headless planner/native-state/native-capture gates pass3/3 in0.63 s.
The state suite includes legacy-to1.4 migration without invented policy and actual
process interruption checks. Saved policy inspection scans all committed packet
stamps under the inactive lease; writer append also refuses violating packets.
Forged self-consistent minima/counts which contradict recorded raw geometry refuse.
Native UI device selection uses an explicit three-second policy and confirms actual
admitted capacity, setting locks, completion and retired-memory clearing.

Independent Windows CI for this new source remains required. A focused ASan/UBSan
profile covers the changed raw bank and native capture boundary when complete;
its initial resampler-disabled profile lacked the monitor test target, so the
required monitor-processing dependency is enabled explicitly. No sanitizer pass
is claimed from that initial build refusal. No
native hardware, VM, physical audio or driver change is exercised locally here.
New UI text receives real Linguist extraction; European translation review and UI
qualification remain incomplete. Concurrent manual-owner pool exhaustion is a
separate original failure and is retained without a blanket fix claim.

Original PR177 source3af394c, run38339523567 is terminal:Linux157/157 in450.87 s,
Windows native84/84 in216.45 s plus separate concurrent-owner diagnostic successful,
Windows desktop20/20 in85.31 s, cross-build successful. Original APIs, all job logs
and selected Windows artifact metadata are retained; original archives remain local.
These successes qualify that source and do not establish the cause or resolution
of earlier PR176/175 failures or native storage changes in this checkpoint.

Focused fresh ASan/UBSan build64steps passes **3/3 in0.30 s**: raw-storage planner,
stamped capture bank and native capture binding. Leak detection is enabled,
ASan halt_on_error1; UBSan halt_on_error1/print_stacktrace1. This is not a sanitizer
claim for the new disk/UI/schema pipeline. Release worker/persistence evidence
covers those separately. The initial profile target refusal remains in the packet.

[Evidence index](../tests/results/M2/2026-10-13-native-recording-storage-budget/index.json)
contains77 compressed text/configuration/metadata members,531 source/localization
input hashes and12 local executable hashes. Original failed short-take metadata,
logs and original PR177 terminal jobs are included as text. No audio/executable/
archive payload is published. Real localization extraction/audit:34 catalogs,
1008 source keys,3168 draft terms, native review0/full UI qualification0.

## Next concrete work

Retain the new source's independent Windows/native/desktop CI results. Implement
short native-take startup completion with partial/zero/variable output fills and
real final slack, preserving original receipts. Admit and expose the manual
capture reserve through its existing bounded API, with within/beyond pause tests
and exact prefix retention; inspect original failed concurrent-owner state rather
than claiming all old failures fixed. Then continue interrupted-original verified
copy/review and the full M2–M11, quality/content/compatibility and European language
work. No installer/hardware/whole-suite completion claim is made here.
