# Windows clean-installed workflow preview

## Local candidate

The ordinary per-user Windows 11 x64 setup/source pair has bounded installed
acceptance on a full independent clone of the pristine template. Source is
`b8cdb3e00da524e5be51b007c1f81d6e51806d72`, sequence `20261008100700`.
Setup is 33,757,321 bytes, SHA-256
`f7a1f75ce7b9052c07388caaac72fd3c233643ebbe330eac3089673b2a502518`.
The actual app is 2,275,328 bytes, SHA-256
`7dab2d068a9c1a5f3e715167c839f172fb60eefdb39f2136211b1f831222b559`.

The owner's local folder is
`/home/rich/Downloads/SoundCurrent-DAW-Windows-Preview-2026-10-08/`.
It contains setup, INSTALL.md, SHA256SUMS, the exact committed GPL application
source archive and complete pinned QtBase/libsndfile source archives. Copy the
whole folder to Windows and run setup. GitHub hosts source and evidence; **no
binary release was uploaded**. The previous Ubuntu DEB preview remains in its
separate local folder.

The SoundCurrent wrapper is unsigned. It installs in Local AppData Programs,
with build-specific desktop/Start-menu shortcuts and an Apps uninstall entry.
Microsoft's unchanged, separately signed runtime installer asks for elevation
only if needed; the SoundCurrent app stays unelevated. No compiler, Qt SDK,
VB-CABLE or kernel driver is required by this native WASAPI DAW preview.

## Acceptance evidence

[Receipt](../tests/results/X007/2026-10-08-windows-installed-preview.json) and
[retained capsule](../tests/results/X007/2026-10-08-windows-installed-preview.zip)
separate installation from native media verification:

- Windows 11 Pro 10.0.26200, normal interactive session 1; no compiler/qmake on
  the acceptance PATH. Original VMs and the pristine template were preserved.
- Initially no registered x64 runtime. Setup installed Microsoft 14.44.35211.00
  and verified all product payload hashes; setup exited zero.
- Desktop and Start-menu shortcuts resolve to this build's actual main executable.
  Main launches and closes normally before and after reinstall, with Qt
  Core/Gui/Widgets, qwindows and libsndfile loaded from the installed slot.
- Normal uninstall exits zero, removes exact owned payload/shortcuts/registration,
  preserves an unrelated project-file sentinel and application settings, and
  allows reinstall into the retained owned slot. Reinstalled main exits zero.
- A test-only executable compiles the production StudioWindow/native controllers
  with bounded capture and RT audit seams. It uses only installed product DLLs
  from the installed slot, confirmed by its own loaded-module inventory and
  executable hash bound to the retained test-tool receipt. QtTest was copied
  separately for testing but is neither imported nor loaded by this particular
  MSVC fixture; its inline wait helper uses QtCore. QtTest is not shipped in setup.
  The owned 48 kHz stereo loopback route records a mono take, edits live EQ/Undo,
  attaches the take, plays with live EQ/Undo, saves/reopens and exports WAV.
- An independent media oracle checks 480,000 raw/export frames with **zero** maximum
  error and 479,936 matching measured playback frames with **zero** residual,
  unity path gain and silent unselected channel. The 64-frame fixture alignment
  is not a physical latency claim.

`python3 tools/verify_windows_installed_preview.py` validates the capsule, source,
installation/main/dependency identities and retained failure, then independently
recomputes the media result. Hosted checks repeat verification; they never
activate Windows audio. Probe/oracle fields such as `installerQualified:false`
remain untouched: those generic helpers do not test setup themselves. The outer
receipt's scoped installed qualification combines their separate evidence.

The verifier compares the 201 refreshed native input hashes and 333 audited
conservative source inputs, including the native fixture C++ source, against
the exact frozen Git commit. The original merged audit retains one stale
auxiliary Python catalog test, which was not compiled into either executable;
it is explicitly excluded rather than represented as current compiled source.
CI fetches the recorded commit before checking historical evidence. Ten mutation
tests reject stale source hashes, five SDK-loaded product DLLs, an incorrect
fixture hash, and altered observer-failure claims.

A second unchanged native pass retains the fixture module inventory and a fresh
independent media check: 480,000 raw/export frames, 479,936 matched playback
frames, zero sample errors/residual, unity path gain and silent unselected
channel. The expanded 83-payload capsule preserves all 54 original payloads
byte for byte, along with both module-inspection harness failures.

## Retained failures and limits

The first installed audio test captured its full raw take, then the independent
playback observer reported discontinuity: next expected position 288,000,
received 288,480 after 287,520 committed frames. It failed and is retained.
An **unchanged** retry with the VM left undisturbed passed the full oracle. The
cause is unisolated; console inspection and VM/host scheduling are hypotheses.
This does not qualify sustained scheduling or erase the failed observation.

The first module-inspection harness used an unsupported `WaitForInputIdle`
check. Its replacement incorrectly required an unused QtTest DLL. Diagnostics
retain both failures. Direct module polling and the executable's import audit
correct those harness assumptions without rebuilding native bytes or relaxing
the media oracle. The independent clone is shut down; temporary transfer media,
listener and scoped firewall rule are removed.

On an earlier prepared source, declining Microsoft's approval left no preview
directory and showed the expected runtime-refusal message. The harness timed out
while waiting for that message to be acknowledged. Its exit-code qualification
is incomplete; final-source cancellation still needs explicit replay.

Open gates include physical inputs/outputs, non-silent startup, microphone
monitoring/duplex, sustained recording, loaded-app/deletion-failure/unowned-folder
runtime cases, same-sequence coexistence, seamless upgrades, installer languages,
native-speaker reviews and complete release/content/licensing qualification.
These remain an early workflow preview, not full Windows or frozen-reference
F/Q/C/N parity.

For physical tests, project and device formats must match the current native
adapter's supported sample-rate/channel layout. Separate input/output clocks and
automatic rate conversion remain open. Use an interface's hardware monitoring
when available; the Windows app's monitoring modes are currently limited.

Next preview task: improve and qualify the physical recording/playback workflow,
including clear format diagnostics and native startup/discontinuity behavior.
The full-suite goal remains active and incomplete.
