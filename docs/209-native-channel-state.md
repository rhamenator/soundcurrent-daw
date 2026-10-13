# Native monitor matrix state and composed startup

2026-10-11. M2 engine progress. The full frozen suite, Linux/Windows parity,
installation and European localization remain open.

## Scope and ownership

Native auxiliary state 1.2 now declares an immutable monitoring channel matrix.
Raw capture, causal conversion and retained source PCM keep their original input
width; only the monitoring output has the declared destination width. Startup
admission binds that width and the exact matrix fingerprint before owners or PCM
are admitted. This extends the bounded mapper in [checkpoint208](208-explicit-monitor-channel-map.md).

No normal desktop factory is activated. Tests use owned synthetic capture and
simulated SDK operations. Physical audio, installed native recording, recording
alignment and hardware deadlines require independent qualification.

## Canonical state

The canonical matrix artifact starts with eight bytes `SCMONMAP`, then
little-endian uint32 major 1, minor 0, input width, output width and route count.
Ordered routes contain uint32 source, uint32 destination and IEEE754 float64 gain.
The exact length is 28+16*routes: at most 1,048,604 bytes for 65,536 routes. Widths
remain 1–256, pairs are unique and gain is finite with magnitude at most 64.
Unknown versions, impossible counts, nonfinite gains, duplicate or out-of-range
pairs, truncation and trailing bytes are refused before processing.

Native 1.2 stores a compact reference to the fixed `monitor-map.bin` basename,
length, artifact version, widths and SHA256. The existing off-thread SHA provider
is reused: OpenSSL3 on Linux, bcrypt on Windows. No external dependency is added.
The journal's 16KiB/depth 12/event 4096/key 1024 limits are unchanged. Artifact reads
use the existing owned file descriptor in chunks of at most 64KiB and require EOF;
the general 128KiB metadata-read cap is unchanged.

The artifact is exclusively created, flushed and published without replacement,
with a directory flush, before the pending native journal. An interruption before
the journal leaves raw recovery with no invented native readiness; an interruption
after it preserves the pending declaration. Inspection verifies the artifact,
canonical digest, raw prefix and native journal under the existing inactive job
lease. No filesystem or hashing work enters real-time callbacks.

Startup admission carries only fixed-width receipt data. The processing owner
compares it with the actual immutable plan before claiming ownership. Admitted
channels mean the output width; converter/source stamps still mean raw input
width. Ready publication preserves the receipt through native save/reopen. No
matrix editing during a prepared lifetime or automatic speaker/pan mapping is
introduced.

## Compatibility and migration

New writers default to auxiliary 1.2. Explicit 1.0/1.1 writing remains available
and refuses a matrix before creating a raw job. Their exact old keysets and
readiness semantics remain intact. Raw and general project schemas are unchanged.

Explicit migration supports targets 1.0/1.1/1.2, refuses downgrade and unsupported
versions, and is idempotent at the selected version. Migration preserves original
observations, SDK outcomes and absence of mapping; it never guesses a matrix or
adds an artifact. A real 1.1 startup outcome migrated to 1.2 changes only the new
null matrix field and schema version. Legacy unequal-width runs without startup
remain unknown routing evidence rather than becoming a fabricated mapping.

## Acceptance

Frozen source `9d5da1c5889dc57af20fe70408a43932fcee6f61`, tree
`ee45d03e4ef5d97b11096c62a05082c5c5815ca0`.

| Profile | Selected suites | Time | Composed checks | Processing checks | Guarded allocate/free/lock |
| --- | ---: | ---: | ---: | ---: | ---: |
| Release | 4 | 2.34s | 3,589,544 | 498,462 | 0 |
| ASan/UBSan | 4 | 34.82s | 3,589,544 | 498,462 | 0 |
| TSan | 4 | 31.66s | 3,589,544 | 498,462 | 0 |

The selected suites are original native outcomes, composed native startup,
bounded startup and bounded monitoring processing. Native outcome check counts
include actual child wait observations and vary between profiles. The
[qualification packet](../tests/results/M2/2026-10-11-native-channel-state/summary.json)
binds 636 exact Git/checkout inputs, 12 Linux executable hashes, 18 Linux object
hashes, seven Windows object hashes and three Windows executables plus the SDK
archive. Commands, configurations, complete original logs, warning-source
provenance and the original development failures are retained. Unchanged
mixing-source/system-header warnings remain visible; no suppression or
false-positive diagnosis is claimed. No executable, object or media file is
included in the public packet.

Acceptance includes the independent literal binary/SHA golden; mono 1→2,
stereo 2→1 and the full 65,536-route state round-trip; malformed reference/artifact
refusal; unchanged raw hashes; explicit legacy admission and migration; and two
actual process interruptions around matrix publication alongside the original
four native and two migration interruption boundaries.

Composed startup tests cover mono capture to stereo monitoring at 44.1, 48 and 96kHz,
a failed Stop, seven mapped diagnostic/partial outcomes, mismatched receipt
admission and malformed complete outcomes. The fixture uses actual prepared
converter/processing/startup/output-bank code, full-FIFO readiness, SDK warmup
and persistence. Expected output is independently converted at the raw width and
explicitly duplicated; it does not call the production mapper. Complete generated
PCM, original raw samples/stamps and saved receipts are compared. Successful
mapped runs have zero reference error. Guarded allocation/free/lock counts are 0.

Two initial development failures are retained: the versioned-field helper still
rejected 1.2, and the first artifact reader exceeded the existing metadata cap.
The corrections extend the exact versioned keysets and read bounded chunks;
no journal cap, production acceptance deadline, fixture cohort or capture buffer
was relaxed.

The Windows SDK adapter and three fixtures compiled and linked. Seven separate
changed source/fixture objects, including the persistence-enabled fixtures,
compiled without warnings. These Windows binaries were not executed locally;
Windows process interruption, native SDK runtime and physical hardware behavior
remain unqualified for this source revision.

## Open Windows acceptance gate

[PR143](https://github.com/rhamenator/soundcurrent-daw/pull/143), run 38171414742,
failed the native Windows required job after 71 CTests passed and two standalone
20-take/five-second runs completed. The third run failed at take 16/identity 17 with
CaptureFailed and QueueFull. Its original writer observation was WriteHashBegin,
written 0/committed 0. This establishes queue exhaustion while the first write
phase was pending; the cause of the delay is unresolved. All four byte-exact job
logs and 36 selected log/JSON artifact members are retained in the
[failure packet](../tests/results/M2/2026-10-11-native-channel-map-ci-failure/summary.json).
The failed candidate remains draft and does not satisfy the protected merge gate.

Next retain the writer's existing backlog observations and measured before/after
write times to investigate that failure without changing capacities, deadlines
or the original three 20-take acceptance runs. Then qualify a concrete correction
on native Windows before enabling the owned recording factory. Successful
simulation or cross compilation does not establish native runtime parity.

## Subsequent native CI qualification

Run38174473151 for draft [PR144](https://github.com/rhamenator/soundcurrent-daw/pull/144)
subsequently passed all four required jobs at source
`f8c6b8bdb598748e852c3af2dfc6ef0f3bf46c3e`, tested merge
`f7914b2975ee521c9d7b05a068339e3b8c1a3c24`, tree
`d8fcdf2831579396aa14c808d99e14a970e68d5b`. All 162 reviewed blobs and the 636
qualified inputs match that tested tree. The native job executed 71 CTests in
183.10s and three actual original 20-take/five-second standalone runs. Its
artifact records 12 composed runs, four mapped cases with error0, real legacy
startup migration and the original four native, two migration and two matrix
publication process interruption boundaries. Guarded allocation/free/lock is0.

The [CI/runtime evidence](../tests/results/M2/2026-10-11-native-channel-state-windows-runtime/summary.json)
retains all four complete original job logs, exact head/base/merge/tree binding
and selected native artifact logs; no executable or media is uploaded. This
qualifies the simulated SDK/state workflow on native Windows. It does not qualify
physical devices, enable the ordinary factory or resolve PR143's intermittent
writer delay. The candidate remains draft and no ordinary merge is admitted here.
[Checkpoint210](210-writer-backlog-timing.md) adds original backlog/timing evidence
for the next native investigation; that newer diagnostic code is not tested by
this preceding run.
