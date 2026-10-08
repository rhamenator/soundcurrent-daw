# Windows desktop foundation for the recording preview

Checkpoint: 2026-10-08. This advances the Windows preview; it is not an installable
Windows release or complete Windows/Linux parity. The existing Ubuntu package
remains the available local recording/EQ/save/reopen/export preview.

## Implemented

The desktop uses native WASAPI playback and single-track recording owners.
Preparation and endpoint selection remain inactive. Recording activation creates
the writer before starting capture; Stop joins native capture, finishes the raw
pipe, drains/finalizes the writer and retains its result or recovery job. Raw takes
remain before EQ. Playback prepares its reader/graph first and its explicit output
stream after selection. Live EQ and receipts use the existing shared engine.

AudioPort carries backend, stable device/channel identities, native rate and
channel count independently of display names. PipeWirePort remains an alias for
existing callers. Saved Windows routes use the SDK endpoint identity and channel
index. A renamed device preserves saved intent, while preparation revalidates
the full fresh inventory. Stale/cross-device/duplicate/wrong-direction/rate
choices are refused. There is no default-device or driver change.

New projects offer a sample rate, default to 48 kHz, validate 8–384 kHz before
project I/O, and prepare their initial EQ below Nyquist. Existing projects retain
their saved rates. Device choices show their native mix rate. A 44.1 kHz capture
project still needs the future conversion adapter to play through a 48 kHz output.
This is explicit rate selection, not automatic sample-rate adaptation.

Windows monitoring On/Auto choices are disabled for this owner. Off remains
selectable when a portable project saved an unavailable mode. Windows shared-clock
project recording, duplex monitoring and punch remain open requirements. The
application uses the Windows GUI subsystem and closes its workers normally.

## Late storage failure

Native testing found that a writer can fail after processing has reached Complete.
The processing terminal status remains Complete, but the controller previously
waited for a backend fault and could leave the storage failure hidden. It now
checks raw single/lane capture status independently, joins the owners, shows the
original disk error, retains other finalized takes and keeps the failed writer's
recoverable prefix. No callback clock guard or processing status is weakened.

A deterministic fixture holds the second writer at its injected 4,096-frame
failure until processing completes all 8,192 frames. The original controller times
out; the fix reports Fault, preserves the first complete take and exposes the
second lane's original error/job/checkpoint. It passes on Linux and native Windows.

## Evidence and limits

[Receipt](../tests/results/X007/2026-10-08-windows-desktop-foundation.json) and
[67-payload capsule](../tests/results/X007/2026-10-08-windows-desktop-foundation.zip):
73,941 bytes, SHA-256
`92e789702c5e2662f6eb3adc8d9962adb72763d55d00cafda221b2d237319828`.
Membership, CRC and every payload hash were checked. No executable, DLL,
credential helper or screenshot is included. Its 331-file conservative code/resource
closure matches the hash-verified native developer snapshot and overlays.
It is not a corresponding-source release archive.

- Independent Windows 11 VM, OS 10.0.26300.9550, limited interactive session 1.
- MSVC 19.44.35228.0, Visual Studio Build Tools 2022, x64 Release `/MD`.
- Qt 6.12.0 MSVC SDK, native Windows QPA and Windows fonts.
- libsndfile 1.2.2 built with the same MSVC/UCRT; optional codecs disabled.
- **8/8 native checks pass:** project controller (358 checks), recording controller
  (211), port admission (15), playback controller, localization, equipment editor
  (2,233), export UI (1,988), and repeated discovery in a COM STA.
- Localization loads 33 catalogs, exercises decimal editing/save/reopen and Unicode
  media/screenshot paths, and checks export samples and RTL charts. All 32 non-English
  catalogs remain partial unreviewed drafts. There are 546 source keys and 256 draft
  translations. No language is fully qualified.
- The actual main executable launches and closes normally with exit 0. SHA-256:
  `b79cd84844b5e02cc69d292f82a44483233c2aead7324c1a677b31c0335a0f48`
  (1,915,904 bytes), empty-project developer-runtime check.
- Linux changed controller/route/catalog checks pass 7/7; subsequent late-writer/
  localization/catalog checks pass 4/4. MinGW builds the native owners and fixture
  after the SDK GUID change.

UI audio-control tests inject synthetic endpoints and use real disk workers.
They do **not** establish native audio through the GUI factories. SDK paths are
supplied by the harness: no compiler-free runtime, installer, upgrade/removal,
physical/sustained or full workflow qualification is claimed.

Original failures remain in the receipt/capsule: split CMake policy argument;
MSVC unresolved extern audio GUIDs (now SDK `__uuidof`, also checked with MinGW);
refused hash-harness substitution; ANSI screenshot-path conversion in the test;
late-writer controller failure; and null process-exit observation subsequently
retested with a retained handle. The VM powered off during collection. Windows
records system winlogon power-off, reason 0x500ff; cause is not established.
Logs survived and were collected after restarting the clone.

## Next preview gate

Exercise the actual native desktop factories with an owned Windows source and
explicit loopback input: prepare/arm/record/Stop/attachment, playback with live EQ
and Undo, save/close/reopen, and independently verified WAV export. Then prepare
a compiler-free local installer/source pair and qualify it on an independent
clean-install clone. Review the 18 changed borrowed equipment/localization inputs
before delivery. Earlier capture's 480-frame gap, sustained/physical timing, full
frozen parity, localization review and installer trust remain open.
