# Checkpoint174: native Linux loop preview

The opt-in `sc-pipewire-ui-fixture` loop mode opens a saved schema1.17 project,
prepares the real desktop PipeWire endpoint, explicitly selects an owned capture
sink and plays a non-quantum-aligned region [1003,18776). Its original raw clip is
96000 frames long; the sink captures480000 frames while playback keeps running.
The fixture requires at least27 wraps, checks monotonic stream/project/pass status,
applies an EQ edit and Undo at actual acknowledged stream frames, and compares
every captured sample against an independent modulo cursor plus continuous EQ
replay. It does not use the production loop mapper for the expected waveform.

Stop retires native playback. Disabling the saved loop preserves locators; a
second native preparation must be linear and is stopped without activation.
Asynchronous close/discard must preserve original project and raw source bytes.
Callbacks are audited for allocation/free/blocking locks; timing is retained as
observed elapsed wall time, not a load or processing-quality guarantee.

## Reproduce locally

Build `sc-pipewire-ui-fixture` with desktop and native PipeWire enabled. Run
`python3 tools/run_native_loop_preview.py --mode loop`. The runner starts a private
instance of the installed PipeWire daemon with a separate socket/runtime, fixed
48kHz/1024-frame quantum and no session manager. It disables JACK detection,
portal and X11 bell modules and requires only the dummy/freewheel drivers before
and after the fixture. It never selects a physical output or alters user audio
metadata. Its configuration is derived from the installed distribution file and
its original/modified hashes are retained. No PipeWire source is forked or replaced.

All owned media, logs and private configuration remain in a unique local folder,
including failed runs. `--mode normal` exercises the existing linear native GUI
workflow in the same private setup. The runner has bounded startup, fixture and
cleanup deadlines. This opt-in fixture is not automatically run by daemon-free CI.

## Scope

A private native graph qualifies real Linux endpoint/link/callback integration and
sample comparison, not physical sound, stress tolerance or installed distribution.
It uses the actual desktop controllers and Qt widgets with offscreen rendering;
manual usability/accessibility review remains separate. Native Windows, automatic
recorded-pass scheduling, comp audition/crossfades and all broader M2–M11/F/Q/C/N
requirements remain open. The source-bound result is recorded separately after
the committed-source run; earlier development runs are not that frozen gate.

## Committed-source observations

Source `2b571181ca2166ccfac481a8eb1d71e14639ff1c` passed both private native
workflows. Each captured480000 samples, with zero difference from its independent
source/EQ replay and zero missing frames. Loop playback reached stream480256/pass27
and remained playing until Stop. Both applied an EQ change and Undo. Source and
saved project remained unchanged. Callback allocation/free/blocking-lock counts
were zero; no unavailable inputs occurred after capture start.

At the observed1024-frame quantum, loop callback elapsed maximum was1.258ms and
linear maximum was3.685ms. These are two short observations, not general performance
bounds. A development run before this source freeze is retained locally but is not
used in this committed-source gate.

The [bounded result packet](../tests/results/M2/2026-10-11-native-loop-preview/summary.json)
is36,442 bytes and contains13 text/JSON/configuration members,499 input-file hashes,
the local fixture hash, exact invocations and owned-media hashes. PCM stays local.
The standalone verifier checks retained log consistency; optional `--source-dir`
compares frozen source bytes. That comparison passed locally before the evidence
commit. It does not execute audio, verify a signature or qualify Windows/physical
playback. No executable, installer or release was uploaded.
