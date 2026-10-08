# Native playback endings

The [direct ending experiment](106-windows-stop-boundary.md) found 64 non-silent
source frames missing from loopback even after the native queue reported drained.
This implements and qualifies a bounded native-only end guard on that owned
endpoint. It does not resolve the separate production-EQ active-Stop failure.

## Timing and lifecycle

`WasapiRenderOptions.end` defaults to `NativeRenderEnd::DevicePeriod`; an explicit
Immediate control remains available. Control-side admission validates the end
enum and ceiling period-to-frame conversion against native capacity. The timing
POD carries the requested end guard alongside startup, period and stream latency.
The SDK-free helper accepts an explicit end policy (its historical omitted-end
default remains Immediate); the product renderer always supplies its selected policy.

After a source callback returns Finish, the native thread submits at most one
silent guard lease per wake, limited by available capacity and remaining guard
frames. No source/DSP callback, parameter receipt or project frame advances.
The guard's [silent release flag](https://learn.microsoft.com/en-us/windows/win32/api/audioclient/nf-audioclient-iaudiorenderclient-releasebuffer)
lets the SDK treat the packet as silence without filling its sample storage.
[Padding](https://learn.microsoft.com/en-us/windows/win32/api/audioclient/nf-audioclient-iaudioclient-getcurrentpadding)
determines writable native capacity. The renderer marks normal drain only after
the entire guard is committed, padding reaches zero and native Stop succeeds.
This observation is not physical latency or a universal final-sample guarantee.

Stop is checked before each guard lease and in the existing event wait. Fault,
overflow and two-second stall refusal paths remain terminal; guard submission
never converts cancellation into normal completion. Prepared cancellation still
performs no processing. Join still precedes bank/object retirement.

Total native submitted frames include startup, callback leases and guard.
Callback content coordinates include certified final-lease slack but exclude
both native boundary intervals; actual project frames remain the engine's domain.
`endGuardSubmittedFrames()` exposes only successfully committed guard frames.
Exact paired accounting is read after join or normal drain. SDK clock units and
QPC retain their separate meaning. `nativeContentFrame` now requires an explicit
finite source range, returning no coordinate for startup, end slack or guard.
Immutable controller timing carries the prepared guard; no project schema or
PipeWire behavior changes.

## Owned native results

MSVC 19.44.35228.0 / Qt 6.12.0 / libsndfile 1.2.2, Release x64, limited interactive
session 1 on the existing full independent development clone. Fifteen processes
exit zero, including direct renderer controls, production playback, real desktop
workflow, native UI/controller/timing checks and actual main launch/normal close.
All 344 source input hashes match
`87a74cb196b4ccd746e5d237e73e13d574598129`.

The direct control and guarded modes share one executable: 376,832 bytes,
SHA-256 `de2d40f48748883cc05f969e1995a97fdcec7e2c1e88895c0c5e84df2ee8f06c`.
All compared direct samples have zero unity-gain residual and path gain 1.

| Case | Source frames observed | Unobserved submitted source | Guard committed | Normal end qualified |
| --- | ---: | ---: | ---: | --- |
| Immediate end control | 95,936 / 96,000 | 64 | 0 | No |
| Guarded end, run 1 | 96,000 / 96,000 | 0 | 480 | Yes |
| Guarded end, run 2 | 96,000 / 96,000 | 0 | 480 | Yes |
| One-frame range | 1 / 1 | 0 | 480 | Yes |
| 31-frame range | 31 / 31 | 0 | 480 | Yes |
| Cancel after guard submission | 91,616 / 96,000 | 4,384 | 480 | No |
| Active cancellation | 43,616 | 4,864 | 0 | No |

Both cancellation cases remain not drained; queued source is not called played.
Source banks, lease samples, queue observations and original capture remain
unaltered. Fitted gain is diagnostic only; fidelity requires unity within 1e-6
and original-sample residual within 5e-5. The Immediate control's missing ending
remains retained, not renamed successful.

The production playback probe now independently matches all 192,000 source
frames at normal completion, zero residual. The real window/controller workflow
records 480,000 unchanged raw samples, applies live EQ and Undo during playback,
saves/reopens and exports WAV. All 480,000 playback frames and all raw/export
samples match independent expectations exactly. Native prepared cancellation,
28 boundary timing checks and complete UI/controller executables pass. Linux
focused tests and ASan/UBSan checks pass; 18 synthetic Stop/end cases cover guard
accounting, short ranges, changed/scaled/poisoned samples and cancellation.

The actual main executable launches and closes normally: 2,282,496 bytes,
SHA-256 `ecba1d7d2643cf0bda8910a6b669ebabe116c5e1ddbc9c5449bba510bbbb051c`.
This uses the developer SDK path, not an installed package. Source epoch remains
the exact native build epoch above, not a later documentation commit.

The production-EQ active cancellation probe still exits zero internally but is
**refused** by strict independent sample verification. No threshold is relaxed;
its altered tail and original failure are retained. Direct cancellation's exact
captured prefix does not resolve that different pipeline. Native application
C++ processing-scope allocation/free counts remain zero; C/SDK internals and
sustained deadlines are outside qualification.

## Retention and next work

[Receipt](../tests/results/X007/2026-10-08-windows-end-guard.json) and
[150-payload capsule](../tests/results/X007/2026-10-08-windows-end-guard.zip)
contain native media, source hashes, actual executable identities/exits, original
task-observation race, normal main close, sources and independent analyses.
Compressed size: 18,319,211 bytes. No executable, DLL or credentials are included.
The hosted verifier relocates and recomputes samples without native audio replay.
Original startup/Stop evidence remains verifiable after these changes.

Next: source-paired installer refresh and installed-runtime qualification with
explicit preview limitations; trace actual production EQ lease samples at active
Stop and retain failed repeats. Extend qualification to repeated transport/seek,
different device periods and physical/sustained routes. The owned VM originals
and pristine template are preserved. Binary release upload, all hardware/language
qualification and full frozen F/Q/C/N parity remain open.
