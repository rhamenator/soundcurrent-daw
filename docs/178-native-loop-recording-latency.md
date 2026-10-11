# Native loop recording alignment and postroll

This opt-in checkpoint extends the owned native Linux recording fixture with
three declared/generated input delays:7,29 and2,053 frames. The last delay spans
more than two1,024-frame native quanta. Finite loop capture must preserve thirteen
full17,773-frame raw windows per input while the native owner continues through
all delayed postroll. Until-Stop must preserve the unequal full raw prefixes,
retain only their common available pass/partial extent, and keep their hashes.

## Clock and sample oracle

The generated source emits each input at its declared delay. Each original raw
journal's timing origin is already offset to its first captured native sample;
adding the latency again would double-count it. Subtracting the declared latency
from its device position must recover the common playback/sink origin. A late
input's origin can occur in a later native cycle; cycle numbers need not match.
The raw oracle compares every delayed-source sample from the ORIGINAL origin.
The independent monitor oracle reconstructs the delayed device stream at the
playback origin and replays actual live EQ/Undo receipts before float64 matrix
mixing and periodic backing. It does not treat a delayed raw file's first sample
as the monitor's first sample or rewrite raw origins into transport origins.

All original sidecars/journals/end reasons are verified before explicit two-page
inactive retention. Source offsets, lengths, grouped pass IDs and Unicode names
are checked; every asset remains a full original receipt. Undo/Redo and explicit
Save/reload preserve the result. Native callback allocation/free/lock audit covers
source, sink and production duplex owner. Preparation and route choice stay
explicit. The runner continues to isolate installed PipeWire from user audio.

Source `c104d06b51fbca182e6fda4de9656a022334f7a6` passed three native
workflows and three synthetic regression suites in1.20s. Finite postroll used228
native callbacks and retained231,049 frames on every input. Until-Stop used240
callbacks and preserved full lengths245,753/245,731/243,707; the common prefix is
13 complete passes plus12,658 partial frames. Timed regression retained240,000
frames/input. All input/output samples matched exactly; allocation/free/lock
counts were0. The60,724-byte [packet](../tests/results/M2/2026-10-11-native-loop-latency/summary.json)
retains25 logs/configurations/context/project/hash members,504 selected source
inputs and four executable hashes. Incremental build was a no-op after identical
fixture bytes had been compiled/linked. Standalone packet validation and optional
source-byte comparison passed; they provide no audio/UI replay or signature claim.
Reproduce with `python3 tools/run_native_loop_recording.py --mode loop --delayed-inputs`,
then `python3 tools/run_native_loop_recording.py --mode until-stop --delayed-inputs`. Zero-delay timed
recording remains a native regression workflow. No production DSP/schema or
third-party dependency change was required.

## Independent Windows desktop observation

Checkpoint176's actual Windows CI job ran19 desktop suites in66.24s, including
36 loop GUI checks, on tested merge473db99da4a0d1bf0f220e3e0af3aab4533d3c2a
(head01b0dc8d/baseaab9ca5b). Its bounded [packet](../tests/results/M2/2026-10-11-desktop-loop-recording-windows/summary.json)
retains the terminal job log and59 exact reviewed changed-file hashes. Packet
validation and optional byte comparison against that desktop worktree passed.
These are Windows OS/Qt/controller/writer workflows with simulated endpoints;
native WASAPI duplex and installed/physical recording remain unqualified.

## Remaining work

Short native generated-device runs qualify this shared PipeWire clock workflow,
not independent-device drift, hardware calibration accuracy, load performance,
fault/context-publication refusal or interrupted loop recovery. Recovered-copy
reconciliation/context portability, audition/crossfades, MIDI takes and full
P005/M2/F/Q/C/N remain open; prior quality failures are unchanged. European
language extraction/drafts are not native-reviewed/UI-qualified translations.

Next implementation: add native Windows duplex ownership with explicit capture/
render clock synchronization and bounded buffers. Native capture and render clocks
must not be passed to the shared duplex bridge as if they were one PipeWire clock.

The Windows implementation boundary is recorded in [ADR114](decisions/114-windows-duplex-clock-boundary.md).
Its first concrete task is a bounded, framework-free typed clock correlation
primitive; native owners/monitor buffers/drift handling and desktop integration
remain required afterward. This does not enable Windows duplex yet.
