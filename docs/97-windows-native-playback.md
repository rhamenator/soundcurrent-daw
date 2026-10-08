# Native Windows playback toward a useful preview

## Implemented boundary

`PreparedWasapiOutput` shares `MixPlaybackRun` and its in-process EQ with Linux
playback and offline processing. It prepares planar banks on control, explicitly
maps each graph channel to one native channel, preserves floating-point headroom,
and silences unused channels and final-block slack. Invalid map/resource/lease
admission leaves the engine position unchanged. Live parameter events retain
their revision and applied-frame receipt. No GUI, disk access, allocation, logging
or blocking locks belong in this prepared processing boundary.

`WasapiRenderStream` prepares an explicitly selected shared-mode WASAPI endpoint
on one COM owner thread. A native wake fills at most sixteen bounded chunks,
checking Stop between leases. GetBuffer/process/ReleaseBuffer stay on that thread;
every acquired nonempty lease is released once, including Abort. Native channels
and project rate must equal the endpoint mix format; unqualified rate conversion
is refused. Preparation is inactive; initial priming occurs only after Activate.
Finish drains the SDK queue before stopping and publishing Complete. User Stop
joins the SDK owner, then cancels/joins disk read-ahead before any banks retire.

Capture and render owners require successful MMCSS `Pro Audio` registration and
HIGH task priority during preparation, and revert it on their own thread. No
registry, system timer, device defaults/volume, other application sessions, cables
or drivers are changed. This does **not** establish the cause or resolution of the
earlier capture discontinuity in [the capture foundation](96-windows-capture-foundation.md).

## Timing domains

The output engine consumes project frames. `submittedFrames` is an SDK queue
sequence; it is **not** the physical played position. Native observations retain
the raw IAudioClock position, its own frequency, QPC100ns timestamp, and a separate
padding snapshot. No capture timing origin or recording compensation is invented
from these output observations. Empty-queue observations are diagnostic, not proof
of xruns. Completion means SDK queue drain, not physical amplifier latency.

The contracts follow Microsoft's
[render lease acquisition](https://learn.microsoft.com/en-us/windows/win32/api/audioclient/nf-audioclient-iaudiorenderclient-getbuffer),
[release rules](https://learn.microsoft.com/en-us/windows/win32/api/audioclient/nf-audioclient-iaudiorenderclient-releasebuffer),
[padding](https://learn.microsoft.com/en-us/windows/win32/api/audioclient/nf-audioclient-iaudioclient-getcurrentpadding),
[device clock units](https://learn.microsoft.com/en-us/windows/win32/api/audioclient/nf-audioclient-iaudioclock-getposition),
[MMCSS registration](https://learn.microsoft.com/en-us/windows/win32/api/avrt/nf-avrt-avsetmmthreadcharacteristicsw),
[task priority](https://learn.microsoft.com/en-us/windows/win32/api/avrt/nf-avrt-avsetmmthreadpriority)
and [reversion](https://learn.microsoft.com/en-us/windows/win32/api/avrt/nf-avrt-avrevertmmthreadcharacteristics).

## Qualification on 2026-10-08

Linux Debug output-contract test: pass, 0.03s. ASan/UBSan/LSan output-contract:
pass, 0.10s. Explicit reordered stereo-to-four-channel output, excluded-channel
silence, float overs, live revision17 at frame64, final partial block, malformed
extents, resource refusal and unchanged saved project/raw source are checked.
Prepared Linux C++/C allocation/free/lock hooks are zero in the audited boundary.

Native Windows11 `10.0.26300.9550`, independent `soundcurrent-win11-dev` VM,
MinGW-w64 GCC13 win32 x86_64 Release, explicit ordinary **Speakers (High Definition
Audio Device)** endpoint, 48kHz stereo, interactive limited session1:

| Workflow | Actual result |
| --- | --- |
| STA discovery + prepared output contracts | Pass; three inventories/five endpoints, caller STA retained |
| Four-second file-backed mono -> selected left output | 192,000 engine/queued frames, SDK drain, 192,000 stereo loopback frames |
| Live +6 to -3dB EQ edit | revision17, actual receipt frame24,480 |
| Native loopback waveform | measured gain1, fixture offset64 frames, 191,936 overlap frames exactly match independently evaluated live-ramp DSP; unmatched final source frames are silent |
| Intentional Stop | 48,480 engine/queued frames; 43,680 captured, 4,800 queued frames intentionally not drained; Stopped remains distinct from Complete |
| Stop waveform | 43,616 overlap frames exactly match; actual live receipt frame24,960 |
| Unselected right native channel | Silent in both retained captures |
| Native audited prepared processing | C++ allocation/free counts0; no claim about allocations/locks inside SDK DLLs |
| Preservation | raw source/project hashes and default endpoint identities unchanged; repeated Stop safe |

The 64-frame offset is a **fixture observation**, not a production latency estimate,
silence trim or alignment correction. No physical interface, long session, large
channel hardware, duplex monitoring, GUI or installer is qualified by these runs.
The earlier 480-frame capture fault remains open; these short successful runs do
not prove sustained scheduling stability.

The [receipt and immutable capsule](../tests/results/X007/2026-10-08-windows-native-playback.json)
retain original media, journals, sources and failures. Executable/DLL payloads stay
in local original archives; their actual guest manifests/hashes are retained.
Replay authenticates archive bytes/membership/CRC/every payload before relocating,
verifies normal and cancelled media, and rejects twelve altered claims/media.
Hosted replay is **not** native Windows execution.

```sh
python3 tests/windows_playback_verifier_tests.py
```

Preserved first failures: slab128 exceeded the admitted contract (corrected to256);
the first native fixture incorrectly reused mono clips as stereo capture and was
refused before audio; its PowerShell direct native-error handling also prevented a
result receipt. The corrected fixture uses a separate valid capture model and
Start-Process output/exit capture. The initial cancellation oracle demanded48,000
captured frames even though Stop deliberately discarded the 4,800-frame queued
tail; its original rejection is retained. Cancellation now checks an explicit
shorter prefix and cannot pass as completion.

## Next concrete delivery task

Add a native Windows recording control owner and device factories to the existing
Qt recording/playback UI, with persistent endpoint/channel identities and visible
native-rate admission. Adapt output preparation to route selection while preserving
canonical live parameter reconciliation, Undo, Stop/Close and recovery. Qualify the
actual MSVC/Qt/runtime combination in the independent VM: VC x64 workloads are
already installed in BuildTools2022 and Community2026; a Qt development SDK is not
yet located. End users must not need either SDK or compiler.

Then build a local compiler-free Windows installer and test Start-menu launch,
record/EQ/save/reopen/WAV export plus upgrade/uninstall preservation. Review the
eighteen changed equalizer localization/profile-editor inputs before the GUI reuse
milestone. Full frozen-reference F/Q/C/N, European localization and X004–X007 gates
remain required. The existing Ubuntu preview is unchanged; no binary release upload.
