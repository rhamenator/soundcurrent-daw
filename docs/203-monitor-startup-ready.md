# Immutable monitor startup readiness

2026-10-11. M2 progress. The full frozen functional, processing-quality, content
and project-compatibility baseline remains open, including M2–M11, imports,
equipment profiles, installation and European localization.

## Preceding protected integration

PR136 passed all four required jobs on run38153219168. Its head
`26ce7a12369dffcc2ee3080eacf3a01884bc948b`, base
`e92736e28192ad01bae9b8bda4d30bb8bf66fe10` and tested merge
`2b01c9046c948f9d87346438a43772efeaa1d6f1` were bound to the original full logs,
whole tree and34 reviewed blobs. Required checks, enforced protection, clean head
and resolved review threads were checked before an ordinary matching-head squash.
Protected MAIN `e926e275c22b169cb8829e8bdaf396d7c8e96e8c` matches tested tree
`8b9917aca18f1a45da9427b2ffba1bdf2ff8925c`, base parent and all34 blobs.
[Protected receipt](../tests/results/M2/2026-10-11-monitor-startup-protected-main/summary.json)
preserves all four original UTF-8 job logs byte for byte, including byte-order
marks, admission and post-merge API/remote bindings. Native MSVC passed70 suites
in212.15s and three additional original20-take diagnostic workflows. These are
bounded non-reproduction; the original timeout and interrupted-parent discrepancy
remain unresolved. They are not Windows execution evidence for the new handoff.

## New prepared handoff

`PreparedMonitorStartupHandoff` owns one immutable receipt. Preparation admits
run generation, both stream identities, rates, channels, render clock frequency,
actual native capacity, maximum fill and original-QPC wait budget. The caller
must obtain actual native geometry; this portable object does not query a device.
The processing constructor checks the exact admission and claims one publisher
only after allocating all prepared storage. Failed admission does not consume
that ownership. A second processor cannot reuse it.

After the original startup observation and transfer have populated the full
native FIFO, processing release-publishes the admitted tuple, original startup
statistics, published extent and original `asOf` time. Acquire readers copy only
this fixed immutable receipt. They never read mutable processing statistics.
Invalid, incomplete, stale or overflowed dispositions cannot publish. No reset
or reuse is available; allocation, retirement and joining remain outside RT.

Pending, Ready, Fault, InputEnd, Cancelled and OwnerLost remain distinct. Control
cancellation is atomic intent; an in-flight publication can race it. The original
receipt remains readable after cancellation or fault for diagnosis, but does not
authorize further playback. Consumers must check terminal state and the existing
output-bank, clock, cancellation and fault domains before content leases. After
the producer joins, an unpublished/nonterminal handoff becomes OwnerLost. All
readers and owners must join before storage retirement.

The receipt is converter startup context, not an audible-frame/project alignment
map. Native format, period and latency still belong to the existing exact native
descriptor binding. No SDK warmup hook or new native transaction schema is wired
in this change. The optional handoff leaves existing callers unchanged; the
native monitoring factory remains disabled.

## Qualification

Full production/startup-fixture source
`1617560d96b8715cfb0b5e03fd4048761f49d30c` passed16 selected suites per profile:

| Profile | Full suites | Time | Startup checks | Concurrent reader cases | Direct PCM error | Guarded allocation/free/lock |
| --- | ---: | ---: | ---: | ---: | ---: | ---: |
| Release | 16 | 3.33s | 19,698 | 6 | 0 | 0 |
| ASan/UBSan | 16 | 23.02s | 19,698 | 6 | 0 | 0 |
| TSan | 16 | 28.15s | 19,698 | 6 | 0 | 0 |

Each actual concurrent reader observes the same first full-FIFO tuple, including
generation and original provenance. Cases cover1/2/8/256 channels at48kHz and
stereo44.1/96kHz, unseeded original moving observations over the existing two-second
confidence span, exact raw bits, trimming/packet offsets and converter comparison.
Reader RT audits are zero. Short input, cancellation, timeout, missing publisher,
abandoned producer, duplicate owner, wrong identities/geometry, malformed receipts,
overflow and resource refusal cannot invent readiness. Joined retirement returns
resource credit.

GCC15 emitted optional-copy initialization warnings in fixture configurations.
Explicit value initialization removed them. Final source
`d89d4501c46ae98a281fe386e74933ed57a3a27c` changes only the existing processing
fixture after the full16-suite run. Its one affected suite was rebuilt and passed
again without compiler warnings in all three profiles. Production and the other
15 executables per profile remain byte-identical to the full qualification.
Original warning/build/test logs remain retained; compiler flags, assertions and
deadlines were not weakened. The evidence distinguishes these source layers.

Windows core targets compile/link, the changed legacy fixture compiles/links, and
three new or modified source objects compile without warnings. This is cross-
compilation, not native MSVC execution, SDK activation or installed audio testing.
[Readiness evidence](../tests/results/M2/2026-10-11-monitor-startup-ready/summary.json)
binds595 selected Git/checkout inputs,48 final executable hashes, original full-run
hashes, all logs, configurations, scripts and original merge conflict stages.
Protected MAIN inheritance preserved the whole qualified source tree after three
reviewed conflicts. No executables or media are included. Builds were sequential,
single-job and low priority; no VM or physical audio route was used.

## Next implementation task

Add the optional native pre-lease Content/NativeWarmup/Abort decision. Warmup must
use the actual SDK SILENT lease, release and client Start, publish original moving
clocks and obey finite frame/QPC budgets without processing PCM or advancing
monitor/project positions. The native content path must acquire this handoff and
verify the actual descriptor binding before ordinary filling. After Live,
underflow must not return to warmup.

Then explicitly migrate auxiliary native state1.0 to1.1 for warmup/readiness,
source dispositions and actual SDK outcomes, retaining exact raw-journal/hash
binding and inactive exclusive leases. Old1.0 reads must not invent receipts.
Qualify short take/EOF/Stop, cancellation, faults, stale identity, failed SDK
Start/release/Stop, clock pressure, owner loss and joined retirement. Independent
native/installed Linux and Windows, professional latency, full EQ/mixing, alignment
and punch/loop qualification remain required.
