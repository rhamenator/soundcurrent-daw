# M2d4b1: native duration qualifier and route admission

The new opt-in Linux qualifier uses an existing PipeWire daemon, a 32-plane
owned source, the production duplex owner, one unarmed file and an independent
stereo disk sink. It never changes defaults, rate, quantum or hardware links.
This checkpoint qualifies the fixture, bounded timing and short regressions.
The first declared 30-minute run failed after about 55 seconds with capture-pool
exhaustion. A subsequent instrumented two-minute run passed all sample checks
but exceeded the maximum callback-time threshold. The duration/deadline gates
remain failed/unqualified; no buffering or checkpoint-policy change is claimed.
M1/M2, Windows and frozen-reference functional/quality parity remain incomplete.

## Native route admission

Creating a local link proxy did not prove remote format/buffer negotiation.
`PipeWireFilter` now owns a listener for every selected link, with lifetime held
on the control owner until listener removal/proxy destruction. PipeWire links
negotiate format/buffers and publish explicit states; see the
[official Link API](https://docs.pipewire.org/group__pw__link.html) and the installed
1.6.2 headers used in this build. This uses the existing public API, without a
server replacement or fork.

Native activation is required to complete negotiation. Activation therefore has
two phases: activate the native filter with engine admission closed, then wait
on the control owner for every owned link to report Active. Admission opens with
a release/acquire atomic flag. Until then, capacity-certified output planes get
bounded silence; no DSP call, capture prefix, engine cursor or common origin is
advanced. A three-second timeout/error rolls back through existing owner joins.
Generators/sinks with no self-owned links admit immediately. A previously admitted
route becoming inactive/error clears admission and reports DeviceLost rather
than silently restarting a generation. All listeners, waits and retirement stay
outside callbacks. The callback adds bounded silence and atomic admission checks,
without allocation, blocking, logging, disk or GUI work.

This resolves a concrete admission deficiency. It does not establish the precise
cause of the original delayed-plane observation or waive any prior failure.
Successful reruns and timing observations remain scoped evidence.

## Workload and media verification

The finite range is 1..1800 seconds at 48 kHz, project start frame 137, with
32 mono raw/Post-EQ arms and one existing mono file. Every track uses its three
default bands with alternating non-flat gains ±1/±1.25/±1.5 dB. A signed sparse
stereo matrix maps tracks in canonical order; raw input planes are permuted by
`(lane * 7) % 32`. Source samples are coordinate-derived pseudorandom floats
with headroom above unity. Capture uses 4096-frame fixed slabs, a 2048-frame
prepared callback maximum and a one-second raw journal interval.

The sink checks every admitted clock, contiguous position/ID/rate and mapped
input shape, retaining first failure facts and its valid prefix. The normal
control workflow waits for both graph and sink completion before native teardown.
All raw writers, the file reader and sink writer join before verification.
Every RF64 header, extent, raw source sample, finalized inactive journal, common
origin, rejected/invalid count and media hash is checked. Streaming bounded chunks
replace whole-file vectors. Private `PreparedEq` instances replay each track
offline; an independent float64 sparse matrix sum compares every stereo output
sample exactly. EQ implementation is shared for live/offline consistency; its
separate processing-quality contracts are not inferred from this replay alone.

The previous project/media remain unchanged until explicit Save. All 32 takes
attach to a local canonical copy, with explicit latency 200 frames on lane 0
and 41 on others: clip0 starts at zero/source63 and other clips start at96/source0.
Save/reopen/media hashes preserve identities, lengths, EQ and matrix. These
synthetic offsets do not measure physical round-trip latency. Unicode paths and
names are used; no Windows execution/Qt/native support is inferred.

## Complete bounded timing

Test-only timing admits one million fixed records per source/owner/sink before
activation, about 16 MB each. One callback writer records elapsed nanoseconds and
its own native period; sorting and reporting happen after join. The native
filter's optional clock observer runs inside its existing audit scope, including
admission/shutdown callbacks; the production duplex owner forwards it only to
configured instrumentation. Production does not acquire timing clocks or logs.

Nearest-rank quantiles use `ceil(N*p)`, including 99.9%. Full per-callback period
ratios qualify variable quanta; measured elapsed time includes preemption and
fixture hooks, rather than being labeled DSP CPU time. Missing periods, clock
failure, capacity overflow or no samples deny complete deadline qualification.
Maximum elapsed remains retained even after storage overflow. The finite workload
thresholds are p99.9 below 60% of each callback period and maximum below 80%.
Short threshold success cannot establish the required 30-minute gate or general
worst-case events/graph replacement/plugin/hardware scheduling.

The supervisor preserves stdout/stderr in owned files, samples route/default
preservation, driver-node identity, owned thread scheduler policies and load
observations, and retains owned projects on success/failure. Only its own newly
spawned PID may be killed at its bounded timeout; observation expiry never starts
a replacement process. Sample correctness and deadline threshold results remain
separate. Existing observations identify Dummy-Driver at 1024/48000: native
PipeWire software-route execution, not a physical interface qualification.

## Qualification and retained failures

All 26 ordinary Debug groups passed in 23.53 seconds and all 26 ASan/UBSan/LSan
groups passed in 64.97 seconds. Headless Windows compilation/linking passed;
native code is Linux-specific and Windows native/Qt/UI parity remains open.
Timing unit cases cover nearest-rank99.9%, exact maximum budget boundaries,
variable periods, missing periods, empty results and overflow denying admission.

Final serial two-second normal and sink-removal cases passed. Normal verifies
3,072,000 raw and192,000 stereo output samples exactly, hashes, Save/reopen,
float headroom and all source/owner/sink callback periods, with zero host-owned
allocation/free/blocking-lock counts. Owner observed p99.9/max is3.921337 ms
at 1024/48000 (18.3813% of period); this is finite evidence, not a duration pass.
Sink removal retains DeviceLost and verifies every valid raw prefix/hash.
Five existing native duplex-owner modes, three/32-arm native desktop cases and
single-track playback/removal regressions also passed with unchanged defaults
and owned-node cleanup.

Initial normal passed, but sink-removal inspection found lane 26/input 22 shifted
by -1024 frames for all 48,128 captured samples from frame0; the other 31 lanes
were exact. This predates removal. Its precise original cause is unproven;
files, complete diagnostics and the comparison analysis are retained. The first
attempt to wait for Paused links before native activation timed out, demonstrating
that this topology needs native activation for negotiation. The two-stage gate
was added without changing source coordinates, DSP, timestamps, tolerances or
trimming raw prefixes. A later run passed media checks but exposed two missing
shutdown clock periods in source/sink instrumentation; observers now cover those
callbacks. The original plane mismatch remains an unresolved reliability
observation alongside the seven inherited failures.

## Failed long run and instrumented diagnostic

The first 1800-second attempt terminated with CaptureFailed after2,637,824
engine/sink frames, about 54.9547 seconds. The initiating owner receipt identifies
lane 1 rejecting1024 frames with a contiguous1024-frame clock, ID30,
1/48000 rate, no XRUN/discontinuity and zero missing file-track frames. Some
healthy lanes accepted an additional1024 frames before generation shutdown.
All writers joined with written=captured. The32-slab4096-frame raw pool admits
131,072 frames (2.730667 seconds,512 KiB per mono lane); it was exhausted.
This establishes backlog, not its original disk/worker/scheduler cause. The
original run has no checkpoint-boundary timers. Complete per-lane/clock/timing
facts and its original source hashes remain retained; nothing is restarted
because an observation wait expires.

The failed owner's complete2577-callback timing sample has p99.9=4.770007 ms,
maximum10.417214 ms (48.8307% of the21.333333 ms period), with no timing sample
loss. Passing timing thresholds for this failed prefix is not duration acceptance.
The existing one-second durable checkpoint policy remains in force. Its worker
already hashes incrementally; it does not rehash the whole recording at every
checkpoint. Each durable checkpoint flushes the RF64 header/audio descriptor,
publishes a flushed temporary journal by rename and flushes its directory.
These steps stay off RT;32 independent writers and the stereo sink may perform
such work concurrently. Original evidence does not isolate their cost.

Fixture-only worker-boundary instrumentation now retains maxima for the current
slab's write/hash/header/flush interval and the journal-start-to-next-write
interval. The latter also includes idle waiting/scheduling; it is not a pure
journal fsync measurement. Initial construction journals are excluded. Each
worker owns one fixed statistics slot; inspection follows join. Production
writer/checkpoint policy, memory budget and capture slab sizes are unchanged.

The120-second diagnostic run verifies5,760,000 frames per raw take/sink,
all 184,320,000 raw and11,520,000 output samples exactly, hashes, canonical
Save/reopen and peak4.30698. Stream wall time120.143 seconds; supervisor time
137.2911 seconds. All native period records are retained. Worker maxima are
115.297016 ms for write/hash/header/flush and81.513784 ms for journal-to-next-write,
well below this pool's horizon in this sample. They do not explain the original
failure. Owner p99.9=4.675373 ms but maximum17.834714 ms is83.6002% of its
period, exceeding the80% gate. The supervisor reports sample correctness as
passed and deadline thresholds as false. No original preemption/scheduling cause
is established by these wall-clock measurements. Controlled load and physical
interface qualification remain absent.

`verify-retained` is an offline, read-only mode for joined failed fixture projects.
It requires the original canonical33-track/one-asset project, finalized inactive
journals, exact RF64 extents, raw track identities/latencies and a common native
origin. It independently checks every full raw prefix's source coordinates and
sample hash, then compares the existing file/EQ/matrix replay against the common
stereo range. Unequal full raw lengths remain unchanged. A separate supervisor
hashes every original file before/after; verification never saves, recovers a
copy or starts native nodes. Release, Debug and ASan/UBSan/LSan verify all 84,424,704 full raw samples and
5,275,648 common output samples exactly. Full raw lengths remain2,637,824 to
2,638,848 frames; the journals retain18,432 rejected frames in aggregate. Output
peak4.01682 is retained. Release verification takes9.6383s, Debug24.3431s and
sanitized73.6858s; these offline timings are not native performance evidence.
SHA-256 snapshots of all 105 original files match before/after. An owned negative
copy changes the common device origin by 1024 frames in32 copied journals while
hardlinking immutable audio; its sample hashes remain valid but the independent
source-coordinate oracle rejects it. Original journal hashes remain unchanged.
No success is inferred from checksum consistency alone. Results and final fixture
source hashes are recorded in the checkpoint evidence.

## Reproduction and next task

```sh
cmake --build .cache/build-desktop-release --target sc-pipewire-duration-fixture
python3 tests/verify_pipewire_duration.py \
  --binary .cache/build-desktop-release/sc-pipewire-duration-fixture \
  --seconds 1800 --modes normal \
  --output .cache/m2-native-duration-thirty-minute.json \
  --failure-output .cache/m2-native-duration-thirty-minute-failure.json
```

Run native fixtures serially after CPU builds/tests finish. The normal 30-minute
workload produces about12.10 GB of media across35 planes, with an additional
2 GiB free-space admission reserve. Verify exact86,400,000-frame ranges on32
raw tracks plus the sink, all2,764,800,000 raw samples and172,800,000 output
samples, full timing coverage/thresholds and original project/take identities.
Record the actual driver/scheduler/quantum/rate/workload, source revision/hashes,
result, exceptions and remaining gaps before promoting any duration note.

The first 30-minute attempt is a retained failure, and the 120-second result is
a diagnostic rather than a replacement acceptance run. Do not increase buffers,
loosen timing thresholds or repeat until a lucky pass without explaining the
change and preserving these failures.

Next: **M2d4c writer-backlog and callback-tail diagnosis**. Add bounded worker-only
phase/queue occupancy observations covering audio flush, journal publication and
slab-drain scheduling; reproduce a declared stall under the same32-track budget.
Use that evidence to choose and test any durable-checkpoint/burst-buffer policy,
then rerun the unchanged30-minute sample/deadline contracts.
Physical round-trip alignment, controlled overload/graph-event stress, ENOSPC/
filesystem/power-loss/unload, Windows, punch/loop/Auto/takes/comping and every
other M2/full-product workflow remain required. All 92 frozen family contracts,
X004, X005 and all-Europe qualification remain unchanged.
