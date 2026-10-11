# Combined original recording and clock-driven monitoring

The portable fixture now composes `OriginalCaptureWriter`, the raw/monitor fanout,
original clock handoff, processing owner and output bank. It uses four actual
owners: capture plus capture-clock publication, disk writing, processing plus
clock consumption, and rendering plus render-clock publication. This is an
integration acceptance fixture, not a new device adapter or installed workflow.

## Schedule and original data

Three fixture barriers per round establish publication, bounded processing and
rendering phases. Capture publication and disk consumption may overlap; the
processing/render phases are intentionally scheduled. Each prepared call is
guarded separately; barriers, disk I/O, checks and vector insertion are outside
those guards. This staged schedule does not qualify uncontrolled concurrency,
native callback pacing, priority or deadlines. No test delay is added to production.

Each original clock source first publishes three declared observations spanning
four seconds. The processing owner receives them through its bounded pump; it
does not call a direct synthetic seed. Subsequent readings advance in their own
original native positions and QPC100ns domain. Current logical control time is
later than the received timestamps. Integer positions above2^63 and QPC/monitor
ordinals above2^54 remain distinct. These prior synthetic observations represent
already-running clocks; collecting actual native startup fits remains open.

The three-channel capture source publishes256 variable1–31-frame packets, with
original integer timestamps, reused/poisoned scratch and floating-point headroom.
The disk owner verifies each original sequence/position/sample bit before append
and retains its lease through the disk write. Checkpoints commit every512 frames.
Finalization occurs only after every owner joins and fanout settlement; independent
inactive inspection verifies the common audio/metadata prefix. The inspected
sample digest is compared with the expected original source bytes, including the
nonfinite payload in its separate failure case. First/last stamps, context, packet
extent and terminal fault evidence are also checked. No playable asset or project
alignment is manufactured from monitor ordinals.

## Priming, pressure, explicit end and failure

Rendering starts only after two actual64-frame slabs have been published. Its
17-frame partial pulls deliberately produce pressure. Repeated OutputFull calls
must preserve reservoir statistics, pending frame count and service count; no
input/DSP progress can hide pressure. Original clock delivery continues.

The healthy run writes4,004 original frames/256 packets and renders3,600 monitor
frames after an explicitly supplied end and real404-frame captured postroll.
It consumes518 original clock observations, observes435 pressure calls and primes
at logical round41. All rendered samples are finite and headroom above unity
survives. The final partial grid is drained without invented input or silence.

The first experimental3,072-frame end was supplied after3,596 frames had already
been generated and was correctly refused. Its complete source/log is retained.
The final fixture declares a later3,600-frame end within its real captured
postroll. This does not implement advance reservation of a live Stop/punch/loop
output boundary. Native finite-end planning must prevent overshoot and account
for real postroll before activation; that production policy remains a next task.

Two separate failure runs introduce a nonfinite original sample and an actual
capture-clock producer loss. Both retain1,999 raw frames/129 packets, the original
terminal receipts and bit-verifiable durable evidence. The nonfinite case records
the exact finite prefix while preserving the NaN payload in raw evidence. The
clock-loss receipt has no fabricated attempted reading. Previously rendered
monitor PCM is not treated as proof of raw recording alignment.

## Evidence and limits

Frozen source `618d5ef50fb0796fb30caa932601e8e1546079c8`,
tree `47fd5cecb7590a46c9700a56239aed51c452ce3a`, passed10,832 checks in
each profile: Release0.05s, ASan/UBSan0.32s and TSan0.38s. Four owner audits report
zero guarded C++/interposed libc allocations/frees/pthread blocking locks. Disk
and fixture synchronization are explicitly outside those audit scopes.

The [60,948-byte/18-member packet](../tests/results/M2/2026-10-11-monitor-recording/summary.json)
binds569 selected inputs, three executable hashes, complete build/CTest/LastTest
logs and profile configuration. It retains the first warning/failure and the
explicit-end refusal source/log. Standalone gzip/member and frozen Git/checkout
hashes passed. Executables and test media are excluded.

This fixture does not test native SDK startup/stop,
physical routes, independent clock drift/occupancy feedback, uncontrolled disk
stalls, installed UI, EQ/mix graph execution, punch/loop, recording alignment,
finite-prefix playable admission, DSP quality or full product parity. Those gates
and European translation/native-review/UI gaps remain open.
See [ADR124](decisions/124-combined-monitor-recording.md).

## Next implementation task

Define and implement an admitted finite-monitor-end policy before native
activation: reserve an explicit output boundary early enough to avoid overshoot,
collect and drain actual bounded postroll, retain original raw timing and test
short/long Stop, cancellation, clock loss and insufficient postroll. Qualify
Windows synthetic execution separately, then compose actual SDK clock acquisition
and owned endpoint startup. Keep the native duplex factory disabled until those
SDK and desktop workflow gates pass. Portable finite-prefix admission and
original-to-project timing transactions remain separate recording work.

## Independent Windows observation

MSVC job114435986700/run38127902259 passed63 synthetic suites154.46s,
including combined monitor recording0.60s. Tested merge
`5e232827ac732c74ccf1459dd958521efc33319c` binds head43bd15c/basedd46d91 and
tree4a7c74cc4905e477be3fa6db5f8da8493349e56e. The
[54,455-byte/three-member packet](../tests/results/M2/2026-10-11-monitor-recording-windows/summary.json)
retains full native job/API evidence and eight reviewed blobs. No per-check count
or executable hash was exposed or inferred. All four stacked129 checks passed.
This tests the earlier3,600-frame explicit-end fixture, without checkpoint193's
new early end reservation. It is not current-MAIN admission, native SDK/duplex or
installed workflow qualification.
