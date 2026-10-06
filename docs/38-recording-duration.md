# M2d4a: paced 32-track recording duration and recovery

This opt-in qualification uses the real framework-independent duplex engine,
bounded capture pipes and independent disk writers. The device is a private
synthetic clock; it neither connects physical audio nor changes PipeWire routes.
It provides the ten-minute synthetic component of P001/M2. It does not qualify
the separate 30-minute native, physical alignment, load/deadline or Windows gates.
No production implementation or new dependency is introduced by this checkpoint.

## Declared workload and oracles

`sc-duplex-duration-fixture` prepares 32 mono tracks at 48 kHz with Post-EQ
monitoring, the existing default unity EQ and an explicit signed sparse stereo
matrix. Each track maps to a different input plane using `(lane * 7) % 32`.
Independent coordinate-derived pseudorandom float samples exceed unity; an
independent float64 matrix sum checks both output planes without clipping.
This duration workload exercises the default EQ path, not a non-flat EQ/event
stress test. Every track is armed; simultaneous unarmed-file playback is covered
by earlier finite duplex tests rather than inferred from this workload.

Project start is 137 frames; the private device and monotonic origins are
10,000,000,000 frames and 20,000,000,000 ns. Clock ID 17, driver delay 777 and
explicit per-track input latency (200 frames on lane 0, 41 on others) remain
distinct. The producer cycles through 1024/256/2048/512/1023-frame callbacks,
with contiguous device positions and a sample-derived monotonic time. The finite
normal range ends exactly even in a partial callback. Slabs are fixed and meters
are drained with a finite 64-observation control-side bound per callback.

Only `DuplexRecordingRun::process` is inside the RT audit. Source generation,
output comparisons, pacing, progress, file inspection and hashing are outside it.
The producer sleeps to absolute sample-derived wall deadlines without waiting
for writer queues. Late cycles and maximum producer lateness are reported; a
contiguous private clock does not prove that a physical driver met its deadline.
Sanitizer probes check memory correctness, not scheduling performance.

After callback quiescence and every writer join, the fixture streams every raw
sample back in bounded chunks and compares its exact source coordinate and plane.
It independently verifies RF64/float headers, extent, sample rate, file hashes,
inactive journals, track/project/asset IDs and common timing origins. All verified
takes are attached to a separate canonical copy; the on-disk initial project
must remain unchanged until explicit Save. Save/reopen and media verification
then preserve 32 identities, offsets and lengths. Lane 0 clips to project frame
zero with raw source offset 63; other clips start at frame 96. Raw media keeps its
original source coordinates. Driver delay never substitutes for input alignment.
Paths and project/track names include Unicode.

## Bounded fault and recovery cases

| Case | Required observation |
|---|---|
| `normal` | All 32 lanes capture/write exactly the requested range, no rejected/invalid/missing frames, joined finalized journals, exact raw/output checks, save/reopen. |
| `cancel` | All successful receipts are withheld, original canceled error retained, each durable inactive prefix independently copied/verified/reopened; original checkpoints unchanged. |
| `writer-failure` | Lane 17 throws at its disk write boundary; initiating error retained, other 31 receipts independently finalize, failed lane's durable prefix is copied and verified. |
| `stall-overflow` | Lane 17's writer blocks off RT; callback consumes its fixed 32-slab pool and retains the correct failed lane/rejected count. Control releases the gate before writer joins. Each valid prefix is verified separately, without asserting equal lengths. |
| `kill-target` / `verify-killed` | Supervisor waits for all writers to cross a checkpoint interval, sends SIGKILL only to its freshly spawned PID, joins it, discovers all 32 inactive journals and verifies copied durable prefixes plus original journal/media hashes. Save/reopen succeeds. |

These cases inject deterministic writer errors and bounded pool exhaustion.
They do not establish ENOSPC/filesystem/power-loss/worker-kill behavior. A killed
process cannot provide a normal destructor/leak result; the separate recovery
process is qualified normally. Original and recovered media are retained under
the owned `.cache/sc-duration-*` project directory on success and failure.

## Reproduction

Build the opt-in target with the existing media dependencies. It is deliberately
excluded from ordinary CTest because its default run takes ten real minutes and
creates about 3.69 GB of raw media. The fixture requires payload plus 2 GiB of
available space before creating a fresh project directory. The supervisor uses
argument lists, bounded waits and only its own child PID.

```sh
cmake --build .cache/build-desktop-release --target sc-duplex-duration-fixture
python3 tests/verify_duplex_duration.py \
  --binary .cache/build-desktop-release/sc-duplex-duration-fixture \
  --seconds 600 --modes normal \
  --output .cache/m2-duration-ten-minute.json \
  --failure-output .cache/m2-duration-ten-minute-failure.json
python3 tests/verify_duplex_duration.py \
  --binary .cache/build-desktop-release/sc-duplex-duration-fixture \
  --seconds 2 --modes normal cancel writer-failure stall-overflow kill-target \
  --output .cache/m2-duration-short.json \
  --failure-output .cache/m2-duration-short-failure.json
```

The supervisor's kill orchestration is Linux-specific. The fixture also compiles
and links in the existing MinGW headless build; neither that nor compilation of
Unicode path handling establishes Windows runtime/UI/native audio parity.

## Qualification limits and next task

The [evidence manifest](../tests/results/M2/2026-10-06-recording-duration.json)
records measured results, source/log hashes, the initial
fixture expectation correction and every inherited unresolved observation.
All frozen family acceptance/quality/reference/F/Q/C/N contracts stay unchanged.
M1/M2 and the complete product remain incomplete.

The optimized 600-second run passed: **28,800,000 raw frames on each of 32
tracks**, **921,600,000 exact verified raw samples**, 29,613 callbacks,
zero missing/rejected/invalid frames, zero audited allocations/frees/blocking
locks, exact output checks, zero dropped meter observations, and Save/reopen.
Stream wall time was 600.042 seconds; complete verification took 647.910 seconds
from stream start (648.163 seconds including supervisor setup/teardown).
Peak output was 4.08854, retaining float headroom. The producer recorded
12 late cycles, maximum 7.867691 ms. These are whole fixture-cycle observations,
including generation/comparison work and scheduling, not native callback
elapsed-time or deadline measurements. The declared host was Xeon Gold 6130,
64 logical processors, Linux 7.0.0-38-generic/ext4, Release `-O3 -DNDEBUG`;
background workload was not controlled.

Five two-second normal/cancel/write-fault/stall-overflow/kill-recovery cases
passed on Release, Debug and ASan/UBSan/LSan builds. Sanitized normal pacing
reported 99 late cycles, maximum 933.916285 ms, with 2.937620-second stream wall
time; its correctness result is not a performance pass. Killed recovery found
all 32 durable 49,152-frame prefixes and verified original/copy hashes and
Save/reopen. A later supervisor-only diagnostic revision preserved the killed
child's signal status separately from recovery and captured timeout stdout/stderr;
final Release normal/kill and a bounded owned-child timeout probe passed. The
initial timing-field assertion confused clock ID 17 with delay 777; the journal
was correct and the fixture expectation was corrected without production changes.

Next: **M2d4b declared 30-minute native device/load qualification**. Extend the
owned source/production duplex owner/independent sink fixture with streaming
long-range verification, bounded timing coverage for every callback, nearest-rank
99.9th percentile and maximum elapsed time, actual quantum/rate/scheduler/load
records, complete fault diagnostics and unchanged defaults/routes. Preserve
strict gap/origin/extent checks. Native software routes do not qualify physical
round-trip alignment; that needs a separately declared hardware workflow.
Punch/loop/Auto monitoring/take lanes/comping, disk-full and the rest of M2 remain
required alongside Windows, X004, X005 and all-Europe qualification.
