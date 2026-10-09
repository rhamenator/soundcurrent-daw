# Isolated independent pitch/time-stretch rendering

2026-10-09, frozen SC-DAW-BASELINE-2026-10-05. CLI worker foundation;
application controls, native/reference processing-quality parity remain open.

## Actual workflow

`sc-stretch-render-worker` receives a bounded JSON request through stdin and
trusted command-line project/jobs roots, process memory, input frame, output byte
and deadline policies. The request names an immutable owned asset/hash, source
span, rational duration multiplier, integer milli-cents and formant option.
Time and pitch are independent. Output stays at the source physical rate;
project-rate conversion remains the existing shared reader's job.

The worker verifies the source, creates an exclusive operation directory and
publishes intent. A `ready` event includes operation and content/processor render
key. The parent writes `start.request` to acknowledge that identity. A
`cancel.request` cancels before opaque work, during study/process/drain, and
before the completion marker. A process watchdog also stops blocked opaque work.
These are trusted owned-directory control tokens, not foreign-project paths.

The result is floating RF64, preserving headroom, plus an immutable source/settings
bound completion receipt. Requested output duration uses exact integer ceiling.
The backend ratio is target/source; actual drained output must equal that target.
No implicit truncation or padding repairs mismatches. The4816*4/3 counterexample
therefore produces6422 frames. A stable content key excludes operation/root paths.
Raw recordings are unchanged. Existing operation names are never overwritten.

Only a validated `complete.json` marker authorizes later asset attachment.
Canceled/killed/failed jobs remain inspectable, with no Session transaction.
Audio publication alone is not completion. Journal errors report
publicationMayHaveCommitted; a parent must inspect the bound marker instead of
assuming an unpublished failure. File-flushed is the stored minimum durability;
full directory durability depends on platform/publication outcome. There is no
resume, orphan cleanup, artifact garbage collection or project attachment yet.

## Resource and thread boundary

Vendor FFT/constructor/process/drain, hashes, decoder/writer and all allocations
run in a disposable worker. Linux enforces RLIMIT_AS and disables core dumps;
Windows assigns the process to a Job Object with a process-commit ceiling.
The metrics differ and are explicitly reported. One sleeping watchdog belongs
to this process; vendor internal threading/timing checks are disabled. Request
JSON is16KiB/depth-bounded, duplicate keys and invalid numeric types refuse.
Known scratch/cache payloads use ResourceLedger before allocation/media reads.
Opaque workspace is bounded by the OS ceiling, not a guessed payload allowance.

Parent aggregate memory admission must reserve the whole child ceiling before
launch and hold it through reap. That launcher/lease integration is next work;
it is not implemented by this standalone CLI. Output frame/byte grants are
checked before destination mutation. The byte estimate includes1MiB header
allowance, exact sample writes and final size verification; this is no filesystem
quota or free-space reservation. ENOSPC/abrupt storage failure remains a failure
with inspectable unverified artifacts. This resource containment is not a
filesystem or system-privilege sandbox.

Live callbacks never call Rubber Band. Actual completed artifacts were read by
the existing MixReader/MixPlayback and offline exporter. Callback partition,
seek/split/crop and export waveform equality, Save/reopen and callback
allocation/free/locking audits pass. This is persisted bounced-media playback;
it does not yet persist an editable independent-stretch processor/anchor model.

## Dependency decision and admitted scope

Rubber Band4.0.0 is selected for this worker using the unmodified single unit,
builtin FFT/BQ resampler, R3 offline and no fast math. The same62 actual
Linux/Windows compile inputs plus COPYING are retained at their original relative
paths, with exact hashes and original GPL-2.0-or-later notices. The laterGPL3 option
fits the application GPL3-only distribution. No optional FFT/codec/getopt source
or runtime is added. The official source archive/tag pin and preceding bounded
probe are in [checkpoint133](133-pitch-stretch-feasibility.md).

Current admission:8k–192k,1–256 channels, constant time1/4–4 and pitch±2400 cents,
at most1e9 input/target frames under explicit lower caller grants. Mono/stereo
use ChannelsTogether; discrete channels use ChannelsApart. The actual current
media workflow qualifies stereo48k; other layouts/rates/formants need their own
quality/phase tests. Limits are provisional backend admission, not reference
parity or removal of required broader features.384k fails explicitly; no hidden
bandwidth loss. Dynamic warp maps, expressive pitch editing, formants, spatial
image, transients/tails, algorithm bakeoff and reference quality remain required.

## Evidence and next task

[Exact local receipt and raw logs](../tests/results/M2/2026-10-09-stretch-worker/)
record Release2/2,55 Python workflow checks and992 shared live/export checks,
three actual completed owned-WAV jobs, independent RF64/sample hashes,
headroom/channel-pitch points, exact rounding, cancellation, deadlines, memory
refusal, malformed requests and existing-job protection. Callback audit counters
are0. Native Windows for this worker is pending protected CI; no VM, native audio,
installed preview or full F/Q/C/N claim follows. ASan's huge virtual mapping is
incompatible with the worker's address-space ceiling; no sanitized worker run is
claimed. Prior source1428389 passed CI102Linux/36Windows core/10Windows Qt and
merged as1232106 with identical tree; those logs retain their own input scope.

Next: versioned editable source-span anchor/processor state and supervised
launcher. Charge child/old-new artifacts to aggregate policy; bound IPC; handle
ready/start/cancel/exit/deadline and uncertain completion; verify marker/audio;
atomically attach a derived asset while retaining raw source and Undo/Redo state.
Desktop clip controls, project cache reuse/invalidations/recovery and Windows
native/installed workflows follow. Keep all full parity and higher-rate gates open.
