# Manual recording disk and group owner

Date: 2026-10-06. P004/M2 framework-independent production control owner.

## Ownership and flow

`ManualRecordingRun` owns an immutable prepared session, one `MixPlaybackRun`,
one `ManualPunchBridge`, immutable arm/delay/monitor/writer options, and up to
eight outstanding capture/result slots. `prepareTake()` allocates a fresh group
with fresh recording asset IDs and already admitted deferred capture pools.
No recording job is created before its actual audio start is published.

The serialized off-audio control owner calls `service()`. It consumes reliable
bridge replies, starts `RecordingWorker` consumers from each exact published
`recordingConfig()`, polls and joins finished consumers, verifies results, and
reclaims capture pools. It accepts starts serviced while raw capture is running,
during latency preroll, or after capture has already completed. Filesystem work,
hashes, journal inspection, worker construction/join and model edits stay outside
the callback and GUI. The native adapter/Qt controller must provide this control
worker; they are the next implementation, not established by the engine tests.

`process()` delegates to the existing continuously running mix/reader/bridge.
It does not reset playback or EQ when a take starts, closes, retires or is adopted.
Audio alone produces raw samples and publishes the immutable per-lane accepted
raw extent immediately before dropping its last reference and publishing Retired.
Control reads `retiredFrames()` only after that acquire publication; it never
reads the pipe's audio-only cursor. Every disk consumer is joined and its lease
released before pool release. Native callbacks must be joined before `stop()`,
`cancel()` or destruction, as with the existing duplex owner.

## Bounded receipts and adoption

Each accepted command retains one of 64 application reply credits. Transferring
replies from the bridge into the control inbox does not return the credit; the
application explicitly consumes `acknowledgement()`. Full submission is refused
before mutation. Unused preparations can be abandoned only when no commands
retain them. Stop drains accepted commands with terminal receipts.

Capture pools are released after retirement, consumer joins and verification.
A completed group continues occupying its result slot until `takeGroup()` consumes
it. Eight unconsumed groups apply backpressure, without dropping a result or
retaining unbounded disk workers/pools. This is a bound on outstanding results,
not eight total takes. Twenty serial groups are replenished while an independent
audio thread preserves continuous nonflat EQ/output.

The group retains logical begin/end, exact independent raw extents, declared
input delay, actual device origin, writer result, inactive journal checkpoint,
job path and original/verification errors separately. Successful media is checked
against persisted journal identity, sample-prefix hash, complete header extent,
origin, end reason and whole-file asset hash. Runtime recovery slab settings are
not mistaken for persisted capture metadata.

`withManualRecording()` creates a transactional model copy. The caller can adopt
that candidate as one `EditHistory` operation and save it independently. This does
not rebuild or mutate the active graph: canonical state and the current immutable
playback generation deliberately differ until explicit transport re-preparation.
Full-group adoption requires every lane to have its full verified range and a
range-complete end reason. Fault prefixes/empty lanes require an explicit partial
choice; only verified successful lanes are attached. Canceled groups cannot be
attached through that helper. Failed checkpoints require explicit recovery before
separate adoption. Missing lanes remain visible in the original receipt.

## Empty takes and failures

A retired zero-frame lane serviced late creates no fake job, asset, origin or
padding. If its writer was started during latency preroll, empty finalization
can fail: retain its inactive zero-frame journal and exception as an Empty lane,
without presenting that expected stop as the initiating disk failure. Constructor
failures remain Failed even for zero raw frames. Mixed valid/empty fault groups
preserve each valid independent prefix and expose the empty lane.

First/partial writer-construction errors, active write errors and errors after
audio retirement are retained and request capture termination. Cleanup and journal
verification errors cannot overwrite the first control/disk error; they remain
separate per-lane evidence. Engine status/callback fault and reader error are
reported independently. Stop joins all consumers; cancel broadcasts cancellation
before producer finishing and preserves each independently durable checkpoint.
A writer that finalized before cancellation can retain completed media, but the
pending group is still marked Canceled. Already delivered ready groups remain
completed receipts.

The [later desktop worker checkpoint](75-manual-desktop-worker.md) defines this
delivery boundary as `takeGroup()`'s cancellation acquire observation. It covers
Cancel during writer/verifier finalization and after Stop, before transfer.

Raw capture can have accepted a prefix in a callback that subsequently faults
before advancing playback. Keep that exact raw prefix even when it extends beyond
the last successfully mixed logical position; do not clamp or pad it to fabricate
a common group duration. Queue-full acceptance verifies this distinction.

## Acceptance and limitations

[Dated receipt](../tests/results/M2/2026-10-06-manual-recording-control-owner.json).
The new `manual-recording-control` acceptance uses real readers/writers:

- Three repeated take workflows at 256/127/31-frame partitions, with 0/41/200/4097
  frame delays, two windows in one callback, late/completed-before-service startup,
  aliased input/output, continuous nonflat EQ and a sample-frame parameter change.
- Every raw sample, exact independent device origin, journal identity, alignment,
  grouped Undo/Redo, passive saved-original preservation and Save/reopen.
- Early/late empty and mixed delayed Stop, first/partial constructor failures,
  active write failure, late finalized hash failure and checkpoint recovery.
- Stop, device loss, cancellation, deliberately exhausted capture pools, 64
  retained replies, eight unconsumed results, unused preparation release and
  twenty takes serviced/reclaimed with a separate audio callback thread.

Synthetic callbacks must have zero instrumented allocations/frees/blocking locks,
and continuous output must match the independent oracle within 1e-6. Floating
headroom above unity is preserved. These functional tests are not native timing,
physical audio, Windows runtime or desktop qualification. Previous 27 observations,
including sustained failures and unlogged combined assertions, remain unchanged.
Observation 28 preserves the initial active-write-failure combined assertion,
its frozen source/executable, original project/journals and separate debugger
replay. The journal before any audio write has a valid nonfinalized zero-frame
checkpoint without a timing origin; validation now accepts that empty checkpoint
without relaxing origin checks for positive prefixes/finalized results. The
original individual assertion terms were not logged and are not presented as
original observations.
No dependency, project schema (1.6), journal schema, frozen contract or F/Q/C/N
promotion changes here. All 92 frozen contracts remain unpromoted. Finite prepared
transport/FIFO still needs indefinite/loop/seek/quantized recording, complete
monitor policies, take/comp workflows, native and Qt integration, independent
Windows runtime/installers, imports/profiles and European translation/review/UI.

Final Debug31/31 (28.86 s), ASan/UBSan/LSan31/31 (94.26 s) and Windows media
cross-compilation pass. No native, desktop manual-control, sustained or physical
qualification is inferred from these functional checks.

Observation29 retains the first protected PR9 Linux CI failure (30/31 pass),
tested commit4bd9606, downloaded test logs and exact source. The concurrent test
logged only a failure flag, so its original callback/oracle status is unknown.
Remote executable and test project were not uploaded by that workflow and are
unavailable; do not claim they were retained. A separate local diagnostic under
one-CPU affinity passed and does not identify the remote cause. Source inspection
found a 0.2ms callback sleep for256 frames at48kHz (about27x accelerated) and a
200000-frame finite horizon. The functional fixture now uses nominal256/48000
cadence and a2000000-frame horizon, with first-status diagnostics and unchanged
strict Running/output/zero-RT checks. This corrects an unsuitable functional
workload; the exact original remote failure subtype remains unproven. All29
observations remain retained, and native sustained timing gates remain open.

Current cadence-qualified Debug31/31 (28.86 s), ASan/UBSan/LSan31/31 (77.09 s)
and Windows acceptance compile pass. Earlier results and CI failure remain in
the dated receipt; neither observation28 nor29 is erased.

Protected PR review additionally identified an unserviced late-cancellation gap:
the initial owner skipped all unstarted consumers when canceled, even if their
retired pipes contained positive accepted raw prefixes. The owner now starts and
drains those bounded prefixes after callback join, retaining finalized media and
verified checkpoints while still marking the group/lane Canceled and refusing
implicit/partial canceled adoption. Already running consumers keep their existing
checkpoint cancellation policy. Zero-frame late lanes still create no fake job.
New real-writer tests cancel before any service for two valid lanes and a mixed
valid/delayed-empty group, verify every retained sample/checkpoint, recover the
prefix independently, and preserve the saved original. This is a review finding,
separate from the29 retained runtime/CI observations.

Final late-cancellation Debug31/31 (28.99 s), ASan/UBSan/LSan31/31 (76.63 s)
and Windows media cross-build pass. Previous qualification/failure evidence remains
retained; native/runtime/desktop manual recording gates are still open.
