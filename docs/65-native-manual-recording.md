# Native manual recording owner

Date: 2026-10-07 UTC. P004/M2 partial implementation; frozen baseline unchanged.

## Ownership

`PipeWireManualRecording` adapts the framework-independent `ManualRecordingRun`
to the existing PipeWire filter. It owns the filter and the run; it does not fork
PipeWire. Select explicit packed input and master output ports before activation.
Missing routes are refused while the run remains Ready. No hardware default,
sample rate or quantum is changed. Preparation reserves deferred pools without
creating recording jobs. Disk workers bind actual starts during later service.

One serialized **off-audio, off-GUI control owner** calls preparation, command
submission, service, acknowledgement, group consumption, parameter ingress and
shutdown. `service()` can construct workers, inspect/hash media and join them;
the GUI must send bounded messages to a future controller rather than call it.
The adapter does not create an additional service thread or expose a mutable
`run()` escape. Atomic status, position, missing-frame and callback-fault reads
are the explicitly permitted observer operations. Graph access is for the sole
serialized parameter producer. Callback audit hooks must themselves be RT safe.

The existing native data callback performs raw capture and continuous mix/EQ.
Transport, EQ state and graph generation remain live across repeated takes and
canonical result adoption. The existing bounded application replies (64) and
pending/unconsumed groups (8) retain their backpressure rules. A native fault
latches the corresponding device/quantum failure. Shutdown requests stopping,
joins filter data/control callbacks, then finishes raw producers and drains/joins
disk consumers before capture reclamation. Explicit cancellation delegates the
engine's canceled-group/checkpoint policy after the native callback join.

## Exact finite acceptance

[Receipt](../tests/results/M2/2026-10-07-native-manual-recording.json).
The opt-in `sc-pipewire-manual-fixture` uses 32 mono arms plus an existing file
track, 32 owned source channels, and an owned stereo sink. Routes use a channel
permutation, declared delays 0/41/200/4097 frames and Off/PostEq/recording-only Auto.
Every track has nonflat EQ; a sample-frame EQ event is applied at 96154.

The same graph runs over [137,480137). Manual windows are [48150,48238),
[48254,48848) and [144168,192164). The first two may start/end within one callback
while longer-delay lanes overlap in postroll. After two verified result groups
are consumed, the third slot is prepared and submitted during continuing playback.
Group adoption/Undo/Redo happens without replacing the immutable running graph.
The late-service case starts its first disk service after frame 60137, when the
first two takes have already retired. It still preserves their exact raw data.

Both Debug synthetic oracle cases and optimized native cases verify all 96 raw
recordings (1,557,696 samples), all 960,000 stereo samples, exact per-lane origins
from the actual callback/offset, journal/header/hash/alignment, six reliable
replies, grouped Undo/Redo, original project/media preservation and Save/reopen.
The output oracle processes in independent 127-frame partitions. Both native runs
have zero maximum sample difference, preserve float peaks above unity and record
zero instrumented callback allocations/frees/blocking locks.

The native runs negotiated 512 frames at 48 kHz. Early service lasted 10.96 s,
with owner maximum wall time 1.211 ms; late service lasted 10.95 s, owner maximum
1.103 ms. All source/owner/sink callbacks have complete wall/CPU/thread-resource
and current-cycle coverage. p99.9 <60% and maximum <80% of their negotiated period
and actual callback end within the current cycle period all pass. Two existing
external links and default metadata remain intact; no observed owned/unowned
cross-link and no surviving owned nodes/links after cleanup. These are finite
owned-route observations, not sustained or physical-device qualification.

The verifier retains each Unicode project, stdout, stderr, executable/verifier
hashes, scheduler samples, route observations and failure receipt. Twelve altered
receipts (including wrong origins/extents, lost replies, clipped headroom and
missing timing coverage) are refused. Source/executable snapshots are frozen
before native launch, with all owned CPU build/test handles terminal.

To run explicitly after ordinary builds/tests finish:

```sh
cmake --build .cache/build-desktop-release --target sc-pipewire-manual-fixture --parallel 2
python3 tests/verify_pipewire_manual.py \
  --binary .cache/build-desktop-release/sc-pipewire-manual-fixture \
  --output .cache/manual-native.json --failure-output .cache/manual-native-failure.json
```

Add `--late-service` for completed-before-service native startup; `--synthetic`
for oracle checking without routes. Native tests are opt-in, outside CTest.

## Remaining acceptance

This checkpoint qualifies repeated complete takes and late startup. The new native
adapter still requires **its own** Stop/cancel/late-cancel, source/sink loss and
initiating active/retired disk-error tests, independent checkpoint recovery and
cleanup. Prior engine/static-adapter fault tests cannot establish that scope.
Qt manual controls and their bounded control-worker interface are next after those
gates; the existing desktop still uses prepared fixed-window recording. Current
CI builds with `SC_BUILD_PIPEWIRE=OFF`, so it does not compile/test this adapter.
Windows media cross-compilation does not establish a native Windows manual owner.

All 31 historical observations remain retained, including the export comparison
and initial RF64 correction failures in [repeatable exports](66-repeatable-export.md).
No source schema (1.6), journal schema, dependency or frozen F/Q/C/N promotion.
All 92 frozen contracts remain unpromoted. Indefinite/loop/seek/quantized recording,
full monitor/take/comping, sustained/physical, Windows native/Qt/installers,
X004/X005 and European translation/review/UI requirements remain open.

Final local Debug31/31 (32.49 s), ASan/UBSan/LSan31/31 (82.66 s), both
new sanitized synthetic manual oracles and Windows media cross-compilation pass.
The optimized current-tree native fixture is byte-identical to the executable
used by both retained native runs; the unrelated export processor is not linked
into it. No Windows execution, desktop manual-control, sustained or physical
qualification follows from those checks.
