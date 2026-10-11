# Bind capture after actual native preparation

`WasapiMonitorCaptureStream` now prepares the inactive Windows capture source,
exposes its admitted format and buffer size, binds a matching prepared context,
and then permits activation. This is original GPL-3.0-only C++20 code with no
new dependency, driver, project schema or GUI activation. The desktop duplex
factory remains disabled pending the native workflow gates below.

## Preparation, ownership and retirement

The actual successful SDK preparation publishes sample rate, channels, channel
mask, loopback mode and **actual GetBufferSize capacity**. The configured packet
ceiling is not substituted for that capacity. The immutable route identity is a
controller-declared endpoint/stream/generation token, not a hardware clock ID.
The new wrapper requires nonzero identity fields. Existing unbound capture
callers retain their prior behavior.

Construct the fanout, clock handoff, Stop cells and callback context from this
descriptor before activation. Admission matches rate/channels/packet capacity,
capture identity, run generation and monitor slots; previously used or ended
components refuse. Exact descriptor equality, including mask and loopback,
is checked again at binding. These APIs select no channels, remap no layout and
manufacture no clock readings or project anchors.

One serialized control owner prepares and binds; one SDK owner produces packets,
availability failures and the original end. The stable callback binding exists
before the native stream is constructed. Its Unbound-to-Bound publication races
the SDK owner's transition to Ending. If binding wins, the SDK reads the prepared
context only after acquiring that publication. If the SDK end wins, the original
end is retained and binding/activation refuse; that owner never reads the
unpublished context pointer. There is no reset or multiple SDK producer support.

Packets before binding or after end preserve their original scalar metadata as
a protocol fault without retaining callback backing or publishing PCM. The
first availability HRESULT is retained separately; it requests Stop without
closing source EOF. Duplicate native ends cannot replace the original receipt.

The wrapper destroys/joins its SDK stream before retiring the binding. External
context, fanout, clock and Stop objects must outlive source and consumer joins.
After all joins, an unreported context settles as OwnerLost, without borrowing
an unbound stream's end or inventing a successful Stop HRESULT. Optional resource
leases charge the whole context and binding objects before ownership transfer;
the surrounding controller must separately budget streams, queues and DSP.

## Packet, clock and completion ordering

Each callback first copies the original packet into the raw/monitor fanout, then
publishes the original device-position/QPC reading through the bounded capture
clock lane. Timestamp-error or zero-QPC readings retain Missing reliability;
discontinuity remains explicit. No conversion, extrapolation, fitting or
normalization takes place in the adapter. Floating-point headroom and original
raw bits remain intact. Silent packets do not read stale native backing.

Monitor or clock pressure cannot erase a raw packet already accepted on that
callback. Their separate first faults and accepted counts remain observable.
The original SDK end is published into the Stop cell **before** clock/capture
EOF. Availability or unhealthy native end also marks the clock producer failed.
The processing owner validates the Stop receipt before pumping that secondary
clock failure, preserving CaptureEnd or an earlier processing cause. Raw EOF,
callback delivery, raw acceptance and successful monitoring are distinct facts.

Prepared callbacks use bounded copies/POD queues/atomics, with no SDK calls,
allocation, free, blocking lock, disk I/O or logging. Event signaling, activation,
binding and joining remain control operations outside this callback contract.

## Frozen component evidence

Source `67b700402f628549f0f14b1cbc2805b68bc760d5`, tree
`242bc5953ce03c1bb25923cbf59e59d3115a7e59`, passed six suites per profile:

| Profile | Seconds | Observed context checks | Capture composition checks |
|---|---:|---:|---:|
| Release | 0.47 | 2,758 | 7,747 |
| ASan/UBSan | 2.39 | 2,860 | 7,747 |
| ThreadSanitizer | 4.03 | 2,358 | 7,747 |

Each profile ran 1,000 actual control/SDK-owner bind-versus-end races. Observed
check totals vary with the winner; they are not fixed coverage or performance
counts. All profiles also passed the existing 6,158 control, 489,463 processing
and 9,251 combined-recording checks plus packet-copy. Guarded C++ and interposed
libc allocation/free/blocking-lock audits were zero.

The [62,240-byte/17-member evidence packet](../tests/results/M2/2026-10-11-native-monitor-capture-context/summary.json)
binds 581 selected Git/checkout inputs, 18 Linux executable hashes, seven
Windows object/library hashes, complete build/CTest/LastTest logs,
configurations and qualification script. The actual SDK wrapper and complete
core-only Windows target set compiled. This packet includes no executable or
media and claims no Windows execution of the new context.

The composed fixture retains the earlier direct-callback 48 kHz cases and adds
nine cases through the production binding/context and original packet-copy
release path, disk writer, processor and render-bank consumer. It uses four
actual owners and 400 barrier-paced rounds. Healthy monitor lengths 0/7/512/3,072
retain raw lengths 64/512/960/3,520 and match an independent direct buffer bit for
bit. Buffer fault, failed SDK Stop, combined faults and cancellation retain
separate original results; raw sample hashes and nonfinite payloads survive.

The new composed source is **synthetic 64 kHz**, so each 64-frame packet has an
exact 10,000-unit QPC increment. It starts with an explicitly synthetic prior
four-second clock fit; this permits a deterministic PCM oracle while routing
new original capture observations. It does not qualify normal native startup,
continuous hardware clocks or uncontrolled disk/render pacing. Original
discontinuity/timestamp-failure transport is tested separately, without silently
clearing those flags to accommodate the prior fit. Stdout postroll is a nominal
fixture count difference, not an admitted raw-to-project map or hardware latency.

## Earlier Windows execution and protected MAIN

The earlier capture-Stop source independently passed MSVC job114455972193,
run38134654956: 65 suites/150.11s, including capture Stop/5.38s. Its
[47,862-byte original-job packet](../tests/results/M2/2026-10-11-monitor-capture-stop-windows/summary.json)
binds the exact stacked merge/tree/parents and 28 reviewed blobs. This tests
Checkpoint195 portable fake-SDK cells, not this new context or SDK runtime.

PR129's protected squash `f1f025d33c0f4cffa324aac7925e4150355259ae` matches
the qualified whole tree, base parent and eight reviewed blobs from run38134067183.
Its [139,977-byte packet](../tests/results/M2/2026-10-11-monitor-recording-current-main/summary.json)
retains all four original complete jobs and admission/post-merge receipts.
PR130's protected squash `26700297151cb1b8284596c9bcf9fa0ce27406a4` similarly
matches all 18 reviewed blobs and tested tree from run38135204341. Its
[142,315-byte packet](../tests/results/M2/2026-10-11-monitor-finite-current-main/summary.json)
preserves the exact jobs and receipts. UTF-8 BOMs are retained for byte-exact
hashes. Neither admission changed protection or used admin bypass. These fresh
successes do not resolve the earlier Windows owner-failure root causes.

## Remaining native gates and next task

Implement finite native render lease/padding/drain and bind the original SDK end
durably into the recording transaction. Then qualify short/long Stop, cancellation,
device loss, joins and retirement on actual adapters and installed previews.

Native startup also needs an admitted capacity/pacing policy while the initial
clock fit gains span/confidence. The small steady-state target/ring is not proof
that startup can retain that interval. Do not substitute synthetic prior clocks
or relax drift confidence to enable the factory. Test actual initial
discontinuities, timestamp availability, fit readiness and queue bounds together.

Raw-to-project alignment, playable finite-prefix admission, repeated punch/loop,
EQ/mix execution, processing quality, European language qualification and the
full M2–M11 product scope remain open. See [ADR128](decisions/128-native-monitor-capture-context.md).
