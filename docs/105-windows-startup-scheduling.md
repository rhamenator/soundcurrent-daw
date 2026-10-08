# Windows playback startup scheduling

This implements an explicit startup interval for WASAPI playback and qualifies
non-silent starts on the owned Windows endpoint. The full frozen DAW target is
unchanged; physical and sustained qualification remain open.

## Timing contract

Preparation reads the SDK's [default device period](https://learn.microsoft.com/en-us/windows/win32/api/audioclient/nf-audioclient-iaudioclient-getdeviceperiod)
and [stream latency](https://learn.microsoft.com/en-us/windows/win32/api/audioclient/nf-audioclient-iaudioclient-getstreamlatency)
as separate 100 ns quantities. The former is scheduling information, not a
documented fade duration. The latter is not assumed to equal physical latency.
Control-side admission validates rate, duration, enum and buffer limits, uses
checked bounded ceiling arithmetic, and refuses a startup interval larger than
native capacity. The admitted period is capped at one second, latency at one
second and native capacity at 65,536 frames; existing native capacity/DSP chunk
limits also apply.

Prepared streams remain inactive. Explicit transport activation commits one
native silent lease for the admitted device period before source callbacks.
This adds no source padding and invokes no DSP during that interval. Raw takes,
exports, graph/project frame positions and parameter receipts remain unchanged.
The stream then fills bounded content leases and starts normally. Stop is checked
before the startup lease and between content leases; join precedes state retirement.
The default product policy is `DevicePeriod`; the explicit `Immediate` policy
remains available to developer controls that reproduce the original failure.

`WasapiRenderClock.submittedFrames` includes the native startup interval;
`contentSubmittedFrames` excludes it. `startupFrames` makes their mapping explicit.
SDK [clock positions](https://learn.microsoft.com/en-us/windows/win32/api/audioclient/nf-audioclient-iaudioclock-getposition)
retain their own frequency and QPC timestamp; they are never treated as queue or
project frames. The SDK-free `nativeContentFrame` returns no content coordinate
inside startup silence. Callers add their explicit project range origin.

The native owner exposes immutable admitted timing. Controller snapshots carry
an optional copy after explicit output selection, resetting it on preparation.
An absent value means unavailable, not zero latency. Processing position still
means queued engine frames, not an audible hardware position. Future transport
alignment/duplex compensation must use these distinctions. No project schema or
PipeWire processing behavior is changed.

## Native samples and lifecycle

The endpoint reports a 100,000-unit period (10 ms / 480 frames at 48 kHz) and
stream latency 0. The latter is an SDK result, not evidence of zero physical latency.
The prototype's two period-start runs preserve frame 0 and all compared samples
exactly. Its same-binary Immediate control still reproduces altered frames 0–479.
The integrated default then passes:

- Direct nonperiodic noise from frame 0: zero measured residual, path gain 1.
- A single 0.04-full-scale sample at frame 0: preserved exactly, right channel silent.
- Cancellation while prepared: no source callback, submitted frame, completion,
  disk worker or activation after Stop; repeated Stop is safe.
- Native production-window/controller recording, live EQ/Undo, playback,
  save/reopen and WAV export. The independent source passes its own defined silent
  lead before recording begins; the raw take therefore starts with actual audio.
  All 480,000 raw and exported samples match independent expectations exactly.
  All 479,936 compared native playback frames match the actual live receipts with
  zero residual and path gain 1, including the non-silent first sample. The
  unselected right channel is silent. The unmatched final 64 source frames are
  below the oracle threshold; this does not qualify an arbitrary non-silent tail.
- Full native Qt UI, controller metadata propagation and 24 native timing checks.

The device prefix is 480 frames; fixture loopback alignment is 544 frames, which
also includes the prior 64-frame fixture offset. These are not physical recording
compensation. No source is altered to pass the sample oracle. The original
Immediate failure and [prior evidence](104-windows-direct-startup.md) remain retained.

No C++ allocation/free is observed in instrumented application processing scopes.
SDK internals, C allocation/locks, sustained deadline stress and monitoring/duplex
remain outside this qualification. Output/session/default volumes are untouched.
The owned independent development clone is shut down, with originals and template
preserved. No installed candidate or public binary is produced here.

## Retained evidence

[Receipt](../tests/results/X007/2026-10-08-windows-startup-scheduling.json) and
[103-payload capsule](../tests/results/X007/2026-10-08-windows-startup-scheduling.zip)
retain prototype/integrated media, actual lease samples, timing, exits, executable
identities, source hashes and build/test logs. Capsule size: 13,888,876 bytes.
No executable, DLL or credential file is included.

Both 342-input native snapshots match exact Git sources: prototype
`54354221720f247c0180fa1495b54bb6946943be`, integrated
`f4fd822774acab970dec958465a23557e12af89e`. The later tenth synthetic analyzer case
is frozen at `9422b625b679e6f9de7bf55099c28f352a341122`. Windows uses the existing
MSVC 19.44.35228.0 x64 Release / Qt 6.12.0 / libsndfile 1.2.2 SDK environment in
limited interactive session 1. The integrated direct fixture is 370,688 bytes,
SHA-256 `795aff14c1919e7e8283dadd97ec49646aec135c1c81516794fefc1af7162358`;
the desktop fixture is 2,329,600 bytes,
SHA-256 `e6f8c260dbbabc167d663244c0feb02072b2aefce4580cb9b9037cc201becf18`.

Linux timing/analyzer/controller/UI tests pass, including affected ASan/UBSan
checks. Ten synthetic analyzer cases cover corrected timeline mapping, a
single-sample impulse, the original ramp and poisoned/changed media. Existing
historical media remains independently verifiable. The hosted read-only verifier
relocates and recomputes both native phases without Windows or audio replay.

## Remaining work

Refresh the exact local installer/source pair and qualify its installed runtime.
Continue with repeated GUI Stop/reprepare, nonzero seek/range boundaries and
non-silent final-frame delivery, cancellation during activation, other device
periods and physical/sustained tests. The earlier independent 480-frame capture
discontinuity is still unisolated. Rate conversion, drift adaptation, Windows
monitoring/duplex, release/language gates and full frozen F/Q/C/N parity remain open.
Existing preview installers retain their earlier source until explicitly rebuilt.
