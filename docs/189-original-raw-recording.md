# Original stamped recording and common-prefix verification

`OriginalCaptureWriter` persists the original raw fanout route directly. It runs
on one disk/control owner, accepts a leased `CaptureBankView`, and leaves lease
release to the caller after `append` returns. It never feeds original samples
through the older sanitizing capture pipe. Existing engine-frame recordings and
their finite-media validation remain separate.

## Ownership and persistence

Prepare the writer on control; transfer exclusive ownership to the disk thread;
join capture, disk and monitor owners; settle the fanout; then transfer ownership
back to control for finalization. All file operations, hashes, JSON, allocations
and callbacks remain outside RT guards. There are no internal threads or waits.
Configuration and raw accepted/written extents must match. Finalization refuses
before joined settlement or full raw draining without preventing a later retry.

The owned `media/original-<asset UUID>` job contains float32 RF64 audio, fixed-size
packet records and a versioned journal. File creation, no-follow/reparse checks,
exclusive job leases, checked flushes and publication reuse existing media I/O.
No destination is overwritten. Resource credit is reserved before workspaces or
files, and inspection reserves its channel-dependent page before opening audio.
Logical credit covers declared workspace; it is not a measurement of allocator
overhead or third-party library memory.

Checkpoint order is audio header/flush, packet-record flush, then durable common
journal publication. Default checkpoint spacing is one nominal second; admitted
spacing is at most 60 seconds. Uncommitted tails are ignored during inspection.
Finalization closes media, publishes `take.wav`, then publishes the final journal.
A process interrupted between these steps retains a verifiable earlier prefix.
This is process-interruption evidence, not a guarantee for power loss on every
filesystem or storage device.

## Versioned original evidence

The strict `soundcurrent-original-recording` journal uses schema 1.0 and declares
`ieee754-f32-le`, native capture frames and QPC ticks in 100 ns units. Context
binds project/track/asset UUIDs, endpoint/stream/generation, layout/channel count,
nominal rate, maximum packet size, initial sequence and declared input latency.
Latency is a declaration; it establishes no recording alignment.

Each 80-byte little-endian record has these fields:

| Offset | Field | Bytes |
|---|---|---|
| 0 | `SCRP`, version 1, reserved zero bytes | 8 |
| 8 | Original packet sequence | 8 |
| 16 | Offset in original raw audio, in frames | 8 |
| 24 | Original native capture position | 8 |
| 32 | Original QPC value, 100 ns ticks | 8 |
| 40 | Original frame count | 4 |
| 44 | Original packet flags | 4 |
| 48/56/64 | Endpoint/stream/generation | 8 each |
| 72 | Reserved zero | 8 |

Packet identity, sequence, contiguity, extent, increasing QPC and admitted flags
are verified before writes and while inspecting. First-packet discontinuity is
allowed; later discontinuity or timestamp error is refused. Native integers are
never converted through double, fabricated project positions or monitor ordinals.
The journal records frame/packet counts and independent incremental SHA-256
digests of original float bytes and packet records.

Original nonfinite values remain evidence. `finitePrefixFrames` marks the first
frame containing a nonfinite channel; it does not sanitize or make the job a
playable Asset. Native Silent packets contain exact positive zero bits. Finite
headroom, channel order and float payloads are preserved. This is float32 evidence,
not a promise to preserve arbitrary integer PCM encodings.

Final status retains the original fanout failure disposition, including an extra
raw packet accepted before monitor loss. A consumer loss without an attempted
packet does not fabricate one. A raw-accepted attempted fault stamp must match
the inspected last packet. Capturing journals cannot claim a final outcome.

Inspection is inactive-only, checks context before audio, streams one fixed record
and at most 1,024 sample frames at a time, and verifies both digests and the finite
prefix. JSON is capped at 16 KiB, bounded depth/events/keys, no arrays or duplicate
keys; unknown schema, units, enum values and unsupported/malformed contexts fail.
Credit and leases retire on success, cancellation and failure. Inspection returns
original evidence; recovery copying, project attachment and timing mapping are
not implemented by this API.

## Frozen acceptance

Source `08bc409287fb8a05f787ba75e6c5093699ef773d` passed Linux Release, ASan/UBSan with leak/UB checks and TSan with
halt-on-error. The two focused suites took 0.14, 1.00 and 1.08 seconds respectively.
Observed raw checks were 594/610/606; polling checks depend on process scheduling.

- Actual concurrent capture, disk-writing and monitor-reading owners verified
  400 variable packets, 6,358 frames and 19,074 float bits per consumer. Source
  credit waits/yields are outside prepared methods, not a native scheduling test.
- Ten actual child-process exits covered writes, audio/metadata flushes, common
  journal publication, media publication and final journal publication. Inspection
  verified the committed prefix after each exit, never the uncommitted tail.
- Three-channel and 256-channel recordings, large original positions/sequences,
  headroom, sample poisoning, paged nonfinite detection, silent packets, Unicode
  paths, invalid stamps, journal/record corruption, duplicate keys, active leases,
  no-overwrite publication, finalization order and credit retirement were tested.
- Existing fanout concurrency and guarded allocation/free/lock checks passed in
  all three profiles. Disk I/O is outside those guards. Existing recording,
  fault-persistence and export regression results are retained in the packet.

The 62,213-byte/19-member [retained packet](../tests/results/M2/2026-10-11-original-raw-recording/summary.json)
binds the source/tree, 562 selected Git and checkout inputs, six executable hashes,
actual compilation/test logs and profile configuration. No executable or test
media is uploaded. Existing unrelated compiler warnings remain visible in logs.
Windows implementation paths are present. See [ADR121](decisions/121-original-raw-recording.md).

Independent actual MSVC job114426318508/run38124626721 passed60 synthetic suites
in141.76s, including original raw recording1.11s. The
[65,713-byte/three-member packet](../tests/results/M2/2026-10-11-original-raw-windows/summary.json)
retains its full job/API evidence, exact tested merge/tree/parents and17 reviewed
Git file hashes. Per-check counts and executable hashes are not exposed or claimed.
This includes the Windows process-interruption paths, not current-MAIN admission,
native SDK, installed/physical/load or complete-product qualification.

## Next implementation

Compose the bounded processing owner with capture admission, actual clock/control
freshness, causal reservoir/controller and pending output retention. Establish
clocks before monitoring, handle finite/clock/deadline loss explicitly, and join
all owners before retirement. Separately admit a finite original prefix and map
original capture timing into a portable project Asset with a recovery-copy
transaction. Monitoring resampling must never decide recorded alignment.
Native/installed/physical/load qualification, earlier DSP quality gaps, M2–M11,
F/Q/C/N, imports/profiles and European localization remain open.
