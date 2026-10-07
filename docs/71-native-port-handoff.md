# Native manual recording: public buffer handoff observations

Scoped diagnostic checkpoint, 2026-10-07 UTC. See the
[receipt](../tests/results/M2/2026-10-07-native-port-handoff.json). Full goal incomplete.

## Retained problem and source assessment

Original 46 remains a failed native Stop run. Its independent marker analysis
places the channel 23 delay before capture: all 194 advancing owner callbacks receive
the previous 256-frame cycle on that channel, while the other 31 channels match.
All 1651 durable samples of its affected lane 17 match that extra delay. Its owned
route maps output 24 to input 24 and reports zero latency. Original 37 has a separate
one-channel 512-frame delay with missing route terms; a common cause is unproven.

The installed PipeWire is 1.6.2 (`1.6.2-1ubuntu1.2`). Exact upstream
[filter.c at 1.6.2](https://github.com/PipeWire/pipewire/blob/1.6.2/src/pipewire/filter.c)
shows that `pw_filter_get_dsp_buffer` dequeues then immediately queues a buffer.
The node process skips output publication while its IO area is absent or already
has `HAVE_DATA`. Queued output during partial negotiation is therefore a hypothesis
to investigate. Original 46 did not retain IO areas/status or private queue state;
those terms remain missing. This source assessment does not establish its cause.

Two allocated buffers alone do not establish an asynchronous link: exact upstream
[impl-link.c at 1.6.2](https://github.com/PipeWire/pipewire/blob/1.6.2/src/pipewire/impl-link.c)
uses the asynchronous allocation flag separately from its actual link/IO selection.
The current [scheduling documentation](https://docs.pipewire.org/page_scheduling.html)
is newer than the installed runtime; do not substitute it for the pinned source.
No PipeWire implementation is forked or incorporated into production.

## Opt-in observer

`sc-pipewire-manual-handoff-fixture` retains the unchanged original callback and
arguments, adding a process wrapper and a secondary public listener with no
process function. Original nonprocess callbacks keep their original context. GNU
API interposition observes public port IO, buffer lifetime/extent and the actual
pointer returned by `pw_filter_get_dsp_buffer`. Pointer addresses are not serialized.
No samples, queues, routing, timing offsets or native scheduling are modified.

Admission prepares four filter observers, 64 ports/filter, 64 public buffers/port
and 8192 rows/filter outside audio callbacks. Bounds, missing clocks, unknown
events/queries, buffer lifetimes and dropped rows remain visible and deny complete
observation qualification. Unsupported/short/removed IO areas remain unknown;
only the public synchronous `SPA_IO_Buffers` structure is sampled. The pointer
lifetime relies on the installed implementation's node parameter/process
serialization, as does its internal port IO access; this is not a general observer
qualification for every PipeWire version.

Outer RT audit segments cover observer lookup, bookkeeping and cleanup separately
from the unchanged inner callback audit. DSP hook work lies inside the inner audit.
Wrapper wall/CPU/usage timing includes row bookkeeping, original callback, hooks
and cleanup; its bounded initial observer lookup is audited but excluded from
timing. Private queue depth and post-callback publication are not observed.
Serialization occurs only after all callbacks/readers/disk workers have joined.

## Acceptance and limits

The fake public provider tests unchanged listener contexts/callbacks, port/API
arguments and actual buffer returns; 64-port and four-observer admission; failed
port creation; known, short, asynchronous and removed IO; buffer removal; unknown
events; null clocks; query/row overflow; full clock fields and JSON escaping.
Callback allocations, frees and blocking locks are zero. Final Debug4/4 includes
existing manual control/interruption and marker regression tests. Release and
ASan/UBSan/LSan pass the observer test; leak detection is explicitly enabled.

One owned native Stop experiment, after all owned local CPU work terminated:

| Retained finite result | Value |
|---|---:|
| Actual rate/quantum |48 kHz /256 frames|
| Owner/source/sink callbacks |196 /287 /195|
| Advancing owner callbacks with 32-channel exact correspondence |194|
| Raw/recovered samples verified, each |37,696|
| Entire nonflat mixed-output samples verified |99,328|
| Wrapper owner maximum wall/CPU time |968,761 /965,079 ns|
| Wrapper source maximum wall/CPU time |211,685 /206,400 ns|
| Wrapper sink maximum wall/CPU time |50,465 /46,729 ns|
| Outer RT allocations/frees/locks, all roles |0 /0 /0|
| Unknown events/queries, row drops and query overflow |0|

Source startup has five all-unavailable generated rows and one partially available
generated row. No observed returned output pointer has unknown IO or pre-API
`HAVE_DATA`; 31 first-available queries have status 0 and 6416 have `NEED_DATA`.
Owner inputs have `HAVE_DATA` and known backing buffers. Source outputs have two
public buffers; owner inputs have one. Full clocks and returned-pointer presence
match the independent source/owner marker rows.19 altered retained handoff traces
are refused without rerunning audio. The existing waveform, recovery, priority,
stage/cycle and owned-route gates pass independently.

**This new pass does not resolve original46**, reconstruct its missing IO/queue
terms, or establish a shared cause for earlier failures. There is no retry loop,
sample compensation, production fix, parity promotion, sustained/physical claim
or native Windows claim. The first missing-header build is preserved separately
as a development compilation failure; historical runtime observations remain 46.
All 24 reviewed equalizer inputs and the 92-row frozen parity projection are unchanged.

## Reproduction and next task

Build `sc-handoff-tests` and run `native-port-handoff-observation` with CTest.
The observer and opt-in fixture require GNU/Clang Linux x64, the existing native
PipeWire development headers and media dependencies. Hosted CI with
`SC_BUILD_PIPEWIRE=OFF` excludes these targets; local native evidence is separate.

Run `verify_pipewire_manual_fault.py` with the handoff fixture, `--mode early-stop`
and both `--output` and `--failure-output`. Then run
`verify_pipewire_manual_priority.py`, `verify_native_port_handoff.py --project ...`
and `native_port_handoff_verifier_tests.py --project ...`, supplying their output
paths. Preserve original source/executable/project/logs before diagnosis on failure.

Next: a controlled native startup/backpressure experiment that distinguishes
buffer availability from IO publication readiness, retaining these observers
and all existing waveform/cycle gates. Establish a causal regression before
changing the adapter or fixture admission. Then finish the serialized Qt manual
controller, late-Cancel adoption policy and actual monitoring edit/Undo widgets.
X006 still requires coordinated model/parser/mix/recording/UI scaling beyond the
current 256-track implementation; no fixed product/license ceiling is the target.
