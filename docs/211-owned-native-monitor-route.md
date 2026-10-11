# Owned prepared native monitoring route

2026-10-11. M2 native monitoring composition. The full frozen suite, Linux/Windows
delivery, installation and European localization remain active and incomplete.

## Implementation

`prepareNativeMonitorRoute` accepts successfully prepared capture/render SDK
descriptors. It preserves their rates, widths, packet/FIFO geometry, channel
masks, loopback flag, identities, render frequency and timing. It derives mutually
consistent fanout, output, converter/drift, clock, control, capture-Stop and
immutable startup declarations. Different widths require an explicit matrix;
the builder does not guess a speaker layout or silently choose a device format.
The startup receipt binds the canonical matrix fingerprint and output width.

`PreparedNativeMonitorRoute` owns those banks/cells, processing and both native
callback contexts in stable, nonmovable storage. Dependencies outlive their
borrowers. One aggregate resource lease is acquired before audio-bank allocation,
with component ledgers disabled to avoid charging the same group repeatedly.
The conservative payload includes retained matrix declarations and fixed storage;
it is not an exact allocator/RSS measurement. Plans are immutable to callers;
copying an rvalue retains a valid declaration and ledger facade.

Preparation, declaration hashing, memory admission and retirement occur off RT.
Existing bounded callbacks and SPSC roles are retained. All capture, processing,
render and raw-consumer owners must join before group settlement/destruction.
Settlement preserves original SDK receipts, marks missing owners explicitly and
leaves queued raw PCM available to its joined disk owner. It cannot wait or join
external streams/threads. Raw source width, samples and stamps remain unchanged.
Opaque monitor ordinals are not device/QPC/project alignment maps.

## Acceptance evidence

The existing `native-monitor-startup` suite includes a separate owned-route
fixture; its preceding cases, fault/persistence work and concurrent owner test
remain present. Seven new runs cover mono at 44.1/48/96 kHz, those three rates
explicitly copied to stereo, and stereo identity at 48 kHz. Each waits for moving
clock evidence and primes the actual 4,097-frame FIFO with 4,160 frames before
authorizing content. Every raw sample is compared bit-for-bit, including float
headroom above unity. Native fills retain guards and finite output; the explicit
unity stereo map preserves equal channels.

Additional cases exercise exact aggregate admission, a refused second owner,
reservation release, immutable plan copies, 22 invalid declarations, wrong
matrix widths, insufficient FIFO/input storage, aggregate memory refusal,
missing-owner receipts and raw-prefix preservation after settlement.

| Qualification | Expanded startup suite | Evidence |
| --- | --- | --- |
| Release | Pass, 0.64 s | 1,016,991 new / 4,606,535 total checks |
| ASan/UBSan | Pass, 6.10 s | Same cases; allocation/free/lock guards zero |
| TSan | Pass, 8.39 s | Same cases plus preceding concurrent owners |
| MinGW | Compile/link pass | Four generated-command objects and changed persistence-enabled fixture; warnings zero |

[Local qualification packet](../tests/results/M2/2026-10-11-owned-native-monitor-route/manifest.json)
binds implementation `2c38699e04900176b43e7da249756b8c65af5b16`, 639 source/checkout
inputs, commands, configurations and executable/archive/object hashes. Only
source/log/hash assets are included. Release used a fresh development build and
an incremental qualification build. Sanitizers and Windows compiled changed
objects using actual generated commands and linked read-only qualified archives
from `9d5da1c5889dc57af20fe70408a43932fcee6f61`; each processing archive was copied
privately and extended with the new source object. Prior qualified directories,
binaries and source remain intact. Unchanged library inputs were checked before
reuse; this reduces rebuild disk work.

Two helper failures are retained: the sanitizer archive selector initially missed
libsamplerate's relative path; the Windows selector classified an import-library
output flag as an input path. Correcting these selectors required no production,
test, capacity or deadline change. The corrected qualification passes. These
failures do not establish an audio-engine defect or explain a Windows writer stall.

## Preceding native Windows results

[PR145](https://github.com/rhamenator/soundcurrent-daw/pull/145) passed all four
required jobs on run38175973115 at exact head
`067d5d1b828dc90f92d6984fd17921beff4b789b`, tested merge
`4310eb5ef2ffe98b0434d8f48ac8484838aca549`. Full original job logs, exact changed
blob bindings and seven selected artifact logs are retained in the
[native diagnostic packet](../tests/results/M2/2026-10-11-writer-backlog-timing-windows-runtime/manifest.json).
Native execution passed all 71 suites and three actual original 20-take/five-second
standalone completions. The held first write retained acquired256/capacity8192,
measured hold33,028,000 ns / completed maximum50,484,100 ns, QueueFull and an exact
8,192-frame raw prefix. The original 34,310,465-byte artifact has SHA256
`d60725b69bdcc7c9d876d8f1a5ea414f36740eeaffed77e461ab00d9c6d237cc`.
SDK operations are simulated; this is actual Windows execution of preceding
diagnostics, not Windows execution of this new owned-route source or hardware
qualification. The earlier intermittent writer-delay root remains unresolved.

## Remaining work and next implementation

This group does not own SDK streams, activate endpoints, launch/join workers or
open/finalize a raw disk writer. Monitoring EQ/mix/profile correction is not yet
integrated into this converter composition. The desktop factory remains disabled.
The existing 256-slot/64-frame output bank refuses FIFO capacities above16,320
frames; an explicit larger-geometry design/qualification is still required.
Other memory/postroll/format refusals remain visible rather than falling back.

Next: implement a finite Windows owner that prepares SDK streams, builds this
group from their actual descriptors, creates pending original recording state
before activation, binds callbacks and owns processing/disk workers. Exercise
partial construction/start failures, explicit Stop/cancel, missing/failed native
ends and destruction ordering with simulated SDK operations before physical
tests. Then integrate prepared monitoring EQ and its latency/state contract,
qualify the actual Windows path and enable the reviewed desktop workflow. Full
M2–M11, F/Q/C/N, imports, profiles and localization completion remain open.
