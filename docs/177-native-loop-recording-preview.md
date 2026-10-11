# Owned native Linux loop recording preview

This checkpoint exercises checkpoint176 desktop loop recording through the actual
Linux duplex adapter, installed PipeWire and owned generated source/sink nodes.
It adds integration acceptance, not complete P005/M2 or processing-quality parity.

## Workflow and checks

The opt-in `sc-pipewire-duplex-ui-fixture` now accepts `loop` and `until-stop`
modes alongside its existing three/32-input timed workflows. Three mono inputs
record continuously while a stereo bus mixes a saved backing clip with monitored
in-process EQ. A non-quantum-aligned 17,773-frame loop starts at project frame137.
The finite workflow requests thirteen passes. The other mode stops explicitly
and retains the partial final pass only after enabling its checkbox.

Preparation activates no callbacks or raw jobs. Routes are selected explicitly
and their accepted GUI state is awaited before Record. Context metadata must be
present during capture and must match each original finalized journal/origin
later. End reasons distinguish range completion from user Stop; origins identify
PipeWire. There is one original raw job per input, not one job per pass.

Live gain changes and Undo are replayed independently through an EQ oracle; every
raw input sample is compared with the generated device-clock source. Every
captured stereo output sample is compared with independent EQ plus float64 matrix
mixing and periodic backing media. The preview cannot automatically attach or
save takes. Two explicit retention pages verify inactive source offsets/lengths,
shared pass groups and Unicode names. Undo/Redo preserve current EQ/routes and
raw hashes; explicit Save and project reload preserve the retained model.
Callback allocation/free/blocking-lock audits cover native owner/source/sink.

## Reproduce and evidence

Build with desktop/media/PipeWire enabled. Invoke
`python3 tools/run_native_loop_recording.py --mode loop`, then `--mode until-stop`.
`--mode normal` checks the existing timed native workflow. Each invocation creates
an isolated installed PipeWire daemon at48kHz/1024 frames, with no session manager,
portal, JACK detection or physical routes. Before/after inventories must contain
only dummy/freewheel drivers; owned nodes, links and daemon must retire. Logs,
configuration and media remain local even on failure. The runner rejects runtime
paths exceeding the Unix socket limit rather than falling back to user audio.
Configuration derives from the distribution file; its upstream Expat notice is
retained in the [packet notice](../tests/results/M2/2026-10-11-native-loop-recording/PIPEWIRE-NOTICE.txt) and THIRD-PARTY-NOTICES.md.

Committed source `9457f8747afb80a70ac4a096efacba4babef8f5c` passed all three
native workflows and three relevant synthetic regression suites (1.24 seconds).
Finite capture has231,049 frames per raw input (13 passes); until-Stop retained
240,640 frames (13 complete passes plus9,591 partial frames). Timed capture has
240,000 frames. All captured input/output samples matched exactly and callback
allocation/free/lock counts were0. The60,284-byte [packet](../tests/results/M2/2026-10-11-native-loop-recording/summary.json)
retains25 log/configuration/context/project/hash members,504 selected source
inputs and four executable hashes. The committed incremental build had no work,
after the fixture was compiled/linked from identical frozen source bytes. Packet
validation and optional source-byte comparison passed. No audio/UI replay,
historical Git dependency or signature claim follows from these checks.
Variable until-Stop lengths are observations, not deterministic promises.
Timing summaries describe these short idle-machine runs, not load/performance
qualification. All PCM remains local; public evidence contains logs and hashes.

## Remaining work

These workflows use generated native inputs with zero configured input latency.
Delayed inputs and very short periods have synthetic core evidence, but native
latency/postroll, loss/clock/fault/context-publication failure, interrupted recovery
and installed/physical recording still need qualification. Native Windows duplex
ownership and equivalent workflows are missing. Recovered-copy reconciliation,
portable context copying, audition/crossfades, MIDI takes and full modes remain
open. No European catalog is promoted to native-reviewed/full UI qualification.
The full frozen F/Q/C/N target and prior processing-quality failures are unchanged.

Next: independently qualify native nonzero input latency/postroll and original
context refusal, then implement the native Windows duplex recording owner.
