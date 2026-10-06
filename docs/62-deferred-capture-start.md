# Prepared capture pools with a deferred recording start

Date: 2026-10-06. P004 / M2 manual-punch prerequisite; no manual-punch UI claim.

## Contract

`CaptureConfig::deferredStart` reserves and touches the capture pool before audio
activation, with a zero placeholder start. The configuration remains immutable.
The audio owner calls `CapturePipe::beginAt` once at the admitted actual project
raw-start frame. A release/acquire publication exposes a separate resolved
`recordingConfig()` snapshot. It is absent before start and never changes afterward.
Invalid negative starts, repeated starts, stopped pools and writer faults cannot
activate or reset a take. Pushing before start fails explicitly rather than
recording guessed frame-zero audio. Native timing origin remains independently
published immediately before the first raw push.

Control/disk reads only the published configuration. It can then create a
`RecordingWorker` with the exact resolved start while audio fills the already
prepared pool. The pool retains full slabs and a partial slab before disk startup;
normal admitted queue exhaustion remains a fault, not overwriting. Worker/writer
binding compares the resolved recording configuration, and journals, media,
attachment and recovery retain the actual start. An unresolved recording spec or
mismatched worker spec is refused before creating a job. No recording or project
schema change is needed: deferred reservation is runtime state, not persisted
recording metadata. The project writer remains schema 1.6.

Existing fixed-start bridge/duplex/native owners explicitly reject deferred pools.
They still require their previous preparation and writer-before-activation policy.
A subsequent manual-punch owner must adopt the deferred protocol deliberately.
The methods add no callback allocation, deallocation, locks, disk work or logging.
[ADR 048](decisions/048-deferred-capture-publication.md).

## Acceptance and open work

Core fixtures cover immutable publication at zero, nonaligned and near-int64-limit
starts; exact partitioned raw packets; one-shot refusal; stopped/faulted pools;
missing origin and pre-start push rejection. Real stereo disk-worker fixtures delay
job creation until after 508 raw frames, including an entire finished capture
before worker creation. They verify exact samples, start/origin/alignment journals,
partial slabs, grouped Undo/Redo, passive original state and Save/reopen.

Debug and ASan/UBSan/LSan suites pass 29/29; the focused capture, bridge,
duplex, punch and recovery groups pass 5/5. Windows core and media targets
cross-compile; native Windows execution remains unqualified. An isolated existing
fixed-start 32-track native Auto/punch regression verifies 3,072,448 raw samples
and 480,000 output samples exactly, zero instrumented RT allocation/free/locks,
and complete short wall/CPU/resource/cycle gates. Owner maximum elapsed 1.345767 ms
and CPU 1.343078 ms at 512/48 kHz; the 7.01-second run preserves two external
links and default metadata and retires all owned routes. All 152 launch pins match
after terminal; twelve altered receipts are refused. This is regression evidence
for the unchanged fixed-start owner, not native qualification of deferred capture.
[Dated receipt](../tests/results/M2/2026-10-06-deferred-capture-start.json).

Native manual punch, repeated takes while playback runs and reference functional
parity remain unimplemented. All previous 25 observations, sustained deadline failures and the
92 frozen contracts remain intact. Native Windows, physical alignment, European
translations and the full DAW objective remain open.

## Next implementation: manual-punch owner

Keep the active `MixPlaybackRun` and EQ state across take boundaries. Prepare bounded
capture slots and per-lane input views on control. An audio command selects the
logical punch sample B, derives checked raw B+L for each lane, and publishes each
one-shot start. Reliable start acknowledgement capacity must be reserved before
accepting the command. Control opens writers from those immutable starts; pool
admission covers startup latency. Disk failure is reported through the existing
sticky capture fault route.

Punch-out at E ends logical Auto monitoring immediately and closes each raw window
at E+L. Capture may continue after the logical switch; finishing writers must not
stop the mix. Repeated punch commands require ready-slot credits, consistent
ordering and checked int64/device timestamp arithmetic. Audio retires a take only
after its last per-lane postroll reference; control joins writers and admits a
verified group before destroying or replenishing resources. Stop/device failure
must preserve each independently durable prefix. No object may be freed by audio
and no worker may mutate the active graph. Apply the existing live/file selection
before the continuously running EQ.
