# Direct Windows startup investigation

This checkpoint narrows the non-silent startup defect. It does not fix it or
qualify the full Windows audio path. The frozen suite target remains unchanged.

## Experiment and observations

The opt-in `sc-wasapi-startup-fixture` uses production `WasapiRenderStream` on
the explicitly selected owned 48 kHz stereo HDA endpoint. It invokes no DAW mixer,
EQ, playback reader or GUI. A deterministic nonperiodic signal below 0.05 full
scale occupies the left channel; the right is silent. Endpoint, default-device
and session volume settings are untouched. Preparation remains inactive.

The renderer fills its native queue before Start, following Microsoft's
[documented rendering sequence](https://learn.microsoft.com/en-us/windows/win32/coreaudio/rendering-a-stream).
This alone does not establish transient fidelity. A separate production capture
adapter and raw worker retain stereo loopback. Preallocated storage retains actual
SDK lease samples and bounded queue/clock observations, read only after native
threads join. No callback logging, disk I/O or dynamic diagnostic growth is added.
No C++ allocation/free is observed in instrumented scopes; SDK internals, C
allocations and sustained deadlines remain outside that audit.

Three native runs use the same 360,448-byte MSVC fixture, SHA-256
`29b5cc5bb6e54087000e6940195ede570666ad723858fc5d386f0840d5b07565`, in limited
interactive Windows session 1:

| Signal | Independent result |
| --- | --- |
| Non-silent from frame 0, first run | Frames 0–479 altered; maximum residual 0.04969424841692671 |
| Defined 12,000-frame silent lead | All compared samples match exactly |
| Non-silent from frame 0, repeated run | Same altered frames and maximum residual |

Every submitted sample equals the independently regenerated source. Interior path
gain is 1, all compared samples after frame 479 match exactly, and the right channel
is silent. Fixture alignment is 64 frames, comparison covers 95,936 frames, and
the remaining source tail is defined silence. These offsets are not physical
latency or production recording compensation. Finalized observer journals carry
actual WASAPI timing origins and no discontinuity/invalid-sample fault.

Attenuation occurs between the correctly filled SDK lease and retained loopback
even without mixer/EQ. This narrows the earlier [desktop observation](99-windows-desktop-workflow.md),
without identifying a Windows engine, driver, VM or observer component. The
silent-lead comparison does not establish a universal duration or a correction.

## Evidence and limits

[Receipt](../tests/results/X007/2026-10-08-windows-direct-startup.json) and
[37-payload capsule](../tests/results/X007/2026-10-08-windows-direct-startup.zip)
retain WAVs, source/submitted samples, timing, actual exits/executable identities,
input hashes, analyzer results and build/test logs. The capsule is 3,045,012 bytes;
no executable, DLL or credentials are included. The 338 native inputs match
`7b497fa19d0c0d7b689cd7b2a1f4c278bd0359ca`. The analyzer's additional timing-origin
check and synthetic tests are frozen separately at
`3fe56d1fea75e98448a813c2c2d1d4cdd7aaa07c`.

Windows uses the existing Qt 6.12.0/MSVC 19.44.35228.0 x64 Release environment and
matching libsndfile 1.2.2. This is developer evidence, not an installed candidate.
The owned full independent clone is shut down; originals and pristine template
remain preserved. Two host extraction attempts misread PowerShell's backslash
directory markers. Normalizing them extracted the unchanged archive without a
native rerun or media changes.

The analyzer checks independent source regeneration, submitted buffers, finite
media, hashes, bounded timing/queue observations and journals, and aligns against
a nonperiodic interior segment. It reports startup alteration explicitly. Seven
synthetic cases cover exact/silent-lead/ramped media, rehashed late NaN/Inf and a
changed submitted lease. Linux output and route contracts also pass.
`tools/verify_windows_direct_startup.py` relocates and recomputes actual evidence
without audio replay. Green diagnostic checks preserve the non-silent failure;
they do not establish successful transient playback.

## Next implementation task

Design explicit startup scheduling that preserves project frame 0 and separates
native queue, device clock and project timeline positions. Evaluate device-period
pre-roll in an owned experiment before adopting it. Do not alter source takes,
exports or full-range sample thresholds to accept lost transients. Cover short
impulses, repeated Play/Stop, seek, drain, cancel and parameter receipts across the
startup boundary. Retain admitted startup latency and its timing mapping if a
correction is adopted. SRC/independent clocks, physical/sustained tests, Windows
monitoring/duplex and full-suite parity remain open. Installers retain earlier code.
