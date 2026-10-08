# Native Windows single-track desktop workflow

The frozen full-suite goal is unchanged. This checkpoint qualifies one owned
recording/EQ/project/WAV desktop workflow, not Windows parity or an installer.

## Workflow and independent samples

The opt-in `sc-wasapi-ui-fixture` uses the production default Windows controller
factories and desktop controls. No fake audio endpoint is injected. It prepares
inactive capture, explicitly selects the left loopback channel of an owned
48 kHz stereo HDA endpoint, arms, records ten seconds with monitoring Off,
changes the first EQ band from +6 to −3 dB, receives its applied-frame receipt,
and uses Undo to return to +6 dB. Raw input remains independent of those edits.

Stop drains and attaches a finalized mono take with a WASAPI timing origin. The
desktop prepares playback, explicitly selects the same device's left output,
plays, edits the EQ and uses Undo again. A separate native stereo loopback
observer records its output. The window saves and joins all workers on close.
A new window reopens the unchanged project without arming or activating audio,
and its export dialog writes a 480,000-frame float WAV at a Unicode path.

The source generator is test-only SDK control work on a separate MTA; GUI events
cannot starve it through a delayed timer. It generates a bounded, nonperiodic
stereo signal with a silent lead/tail. It changes only its ephemeral session
volume, never endpoint/default/other-application settings. It is not a product
renderer or scheduling qualification. Existing four-second source defaults are
unchanged. Native hardware tests are never registered as automatic CTest tests.

`tests/verify_windows_desktop.py` independently regenerates the original source
and evaluates time-varying peaking coefficients and the 480-frame smoothing
recurrence. It does not invoke the C++ engine or replay native audio. Results:

- All **480,000 raw samples** match a contiguous selected source subset exactly.
- All **480,000 exported samples** match the independently evaluated saved EQ exactly.
- **479,936 native playback frames** match both actual live edit/Undo receipts
  exactly, with measured path gain 1. The fixture output offset is 64 frames;
  the unmatched source tail is below the comparison threshold. The unselected
  right output channel is silent. These offsets are not production compensation
  or a physical-latency measurement.
- No C++ allocation/free is observed in the instrumented DAW processing scopes.
  SDK internals, C allocation, locks and sustained deadlines are not audited by
  this Windows interposition.

## Original failures remain visible

The first native build failed because the Windows `min` macro expanded a C++
call in the fixture. Its target now defines `NOMINMAX`/`WIN32_LEAN_AND_MEAN`.
The first GUI run completed recording/playback/reopen but correctly refused an
export path inside the project outside `exports`. The fixture path was corrected;
the product's export policy was preserved.

A subsequent fixture returned success, but independent sample checking refused
it: a non-silent playback start showed attenuation over its first **480 frames**.
At fixture offset 64, its maximum initial residual was **0.050122939832363045**;
the remaining compared samples matched exactly. The cause has not been isolated.
The accepted run uses a defined silent lead-in, so it does **not** qualify that
non-silent startup transient. The original media, report and refusal are retained;
an internal success flag is not sufficient evidence.

The owned development VM also powered off unexpectedly, with System event 1074
identifying `winlogon.exe`, SYSTEM and reason `0x500ff`. The log does not establish
an application crash or licensing cause. Testing used limited interactive session
1 after restart, without changing Windows power/licensing policy or original VMs.

## Evidence and remaining gates

[Receipt](../tests/results/X007/2026-10-08-windows-desktop-workflow.json) and
[77-payload capsule](../tests/results/X007/2026-10-08-windows-desktop-workflow.zip):
24,280,446 bytes, SHA-256
`053e8006e7b9c4bdf6049e30c683e984580f4cdcd0dd14bd50519cb936225fbc`.
Membership, CRC and each payload hash are checked. No executable, DLL or credential
helper is included. Its 330 conservative code/resource inputs match the final
guest snapshot; two unrelated Linux synthetic manual tests were excluded because
they were not compiled/invoked by this fixture. This is developer evidence, not a
corresponding-source release archive.

Windows 11 10.0.26300.9550, MSVC 19.44.35228.0 x64 Release `/MD`, Qt 6.12.0
MSVC SDK and matching MSVC/UCRT libsndfile 1.2.2. The final fixture is 1,967,104
bytes, SHA-256 `b7dc651191419328d4261bc1435b06ff75cb554defef8f2b711c6c305fed6a3b`.

`tests/windows_desktop_verifier_tests.py` relocates the capsule and checks nine
altered claims/media cases, including rehashed audio, changed live receipts,
fabricated source, synthetic timing origin and an absolute Windows path. Hosted
Linux checks run this read-only verification; they do not replay Windows audio.

Physical microphone input, monitoring On/Auto, Windows shared-clock multitrack
duplex/punch, rate conversion, independent clocks, startup transient, sustained
stress and full Windows F/Q/C/N parity remain open. The SDK PATH supplied the
runtime; compiler-free deployment, installation, upgrades/removal and original
hardware have not been qualified. No Windows preview or binary release is uploaded.
The existing Ubuntu preview remains available.

Next: review pending equalizer localization/equipment inputs, prepare the local
Windows installer/source pair, and qualify it on a full independent clean-install
clone. Continue investigating non-silent native startup separately.
