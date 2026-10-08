# Preview follow-up: published silence at input startup

Three bounded **production single-track owner** observations on private PipeWire
1.6.2 now distinguish published neutral input from missing data. The installed
Ubuntu candidate and all original recordings remain unchanged. This is a test
fixture and diagnosis; automatic recording alignment is **not implemented**.

## Observed workflows

Each normal-UID/capabilities-zero probe records 96,000 mono frames at 48 kHz into
an owned project on the read-only Ubuntu Base runtime, using private PipeWire,
no physical audio device and the existing native production adapter. The source
is an owned nonperiodic PCM16 LCG sequence with an encoded 4,096-frame silent
passage. It cannot conceal missing entire periods like the earlier 1 kHz tone.

| Route / executable | Published startup silence | First source frame in matching suffix | Every later sample | Encoded silence recorded |
|---|---:|---:|---|---:|
| Direct `pw-play` / prototype | 2,048 frames | 0 | Exact | 4,096 frames |
| Fed virtual source / prototype | 2,048 frames | 8,192 | Exact | 0; this passage had already played before recording |
| Direct / newly CMake-built fixture | 2,048 frames | 0 | Exact | 4,096 frames |

The first native lease in every run has `SPA_CHUNK_FLAG_EMPTY`, acquisition
**Silence**, known mapped ownership and successful matched return. The raw take
contains the corresponding zeros. Later leases are **Ready**, with a contiguous
exact source suffix. All 47 callback clocks per run are continuous at a measured
**2,048-frame quantum**. Thus this is one observed neutral callback, approximately
42.67 ms, rather than an assumed two-callback delay.

All owner/observer allocation, free and blocking-lock counters are zero; all
leases are returned once. Both prototype launchers and the CMake launcher exit 0,
with unchanged redacted full-object host default/link fingerprints. Each take
completes its 96,000-frame target, with no first fault or rejected frames. These
observations do not establish real-time deadlines, sustained/physical recording,
Qt controls, installed-app behavior or native Windows qualification.

The current online [PipeWire scheduling documentation](https://docs.pipewire.org/page_scheduling.html)
(version 1.6.9 when consulted) describes extra-cycle latency for asynchronous
links and latency propagation. Its [latency documentation](https://docs.pipewire.org/page_latency.html)
explains port latency propagation. The owned player advertises `node.async=true`.
This makes route startup/latency a plausible explanation, **not a measured total
latency or a guarantee for every device/graph**. No port latency or source PTS
observation was collected. The SDK dependency remains 1.6.2; no source was forked.

## Preserved evidence and limits

The [immutable receipt](../tests/results/X007/2026-10-08-input-acquisition-observation.json)
and adjacent ZIP retain source WAVs, all raw projects/journals, acquisition/chunk/
clock observations, launchers/config/commands/logs, executables, the CMake source
snapshot and linked-archive hashes, independent verifier and redacted host inputs.
The ZIP is **23,570,726 bytes**, with **190 logical entries**, CRC and every payload
hash checked. Static-library payloads are not included; the CMake snapshot and
executable are retained. The earlier prototype libraries were reused from the
qualified native-v2 production cohort, whose source/header hashes were unchanged.

The first prototype stopped before activation because `pw-play` DSP ports had not
been configured. Its original source, binary, log/project and exit 1 are retained;
there is no raw take from that setup refusal. The first independent reader lacked
extensible-float GUID support and refused both valid takes; strict GUID/mask/valid
bits support corrected the reader without altering media.

Read-only replay of the three relocated originals passes; the registered local
CTest gate passes **1/1 in 0.44 s**. The same oracle refuses
**33 altered waveform/clock/ownership/extent/audit/path/container claims**, including
missing/repeated samples and an erased encoded silence. Run:

```sh
python3 tests/input_acquisition_verifier_tests.py
ctest --test-dir .cache/build-desktop -R '^input-acquisition-evidence$' --output-on-failure
```

The opt-in `sc-pipewire-input-acquisition-fixture` uses existing test-only public
API interposition. It must run on an explicitly owned private daemon with configured
`sc-preview-wav-player:output_MONO` or `sc-preview-source:capture_MONO`, never a
physical route. The retained launcher is an exact local reproduction example;
its rootfs path is local infrastructure, not an end-user setup dependency.

Original installed-candidate callbacks were not observed at this boundary.
These new runs reproduce its 2,048-zero-frame symptom but cannot supply missing
original evidence or resolve native71. No amplitude admission gate, fixed startup
skip, silence trimming, latency correction, package change or parity promotion.

## Next implementation task

Add versioned native input latency observations and an explicit acquisition /
recording-start alignment contract, preserving raw silence and truthful timing
origins. Qualify stable, changing and unavailable latency plus silent sources;
never infer invalid data from amplitude or assume every graph has this one-quantum
prefix. Then continue native Windows recording/playback and installer previews.
All frozen F/Q/C/N, X004/X005/X006/X007 and Europe completion gates remain open.

## Review correction: complete payload and timestamp verification

The original replay test checked waveform/position/cycle data, but did not enforce
all capsule hashes or later `nsec` values. Protected PR38 review correctly found
these gaps. The current gate first verifies archive size/SHA-256/CRC, unique and
exact manifest membership, and every one of the190 payload size/hash pairs. It
then checks every callback timestamp as a positive uint64, strictly increasing,
and origin-relative sample-rate consistent within one sample (20,834ns at48kHz).
This allows integer-frame/start-phase quantization; it is not a deadline gate.
The original direct startup deltas have phase offsets of20,810ns/8,538ns; no
recorded time was rewritten to pass an exact-period assumption.

[Separate review evidence](../tests/results/X007/2026-10-08-input-acquisition-integrity-review.json)
replays all three original observations and refuses41 changed claims, adding
negative/backward/rate-wrong timestamps and unextracted executable/source/whole
archive claims. All original capsule/receipt/media bytes remain unchanged. No
new native run, product change, package or broader timing qualification.
