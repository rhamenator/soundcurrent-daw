# Native buffer readiness, extent and ownership

Production Linux adapter checkpoint, 2026-10-07 UTC. The full DAW goal remains
incomplete. See the [receipt](../tests/results/M2/2026-10-07-native-buffer-acquisition.json)
and [controlled mechanism](72-controlled-native-startup.md).

## Implementation

The adapter now uses public `pw_filter_dequeue_buffer` / `pw_filter_queue_buffer`
with prepared per-port metadata and explicit callback-local leases. It first
requires synchronous `SPA_IO_Buffers` readiness. Input acquisition requires
`HAVE_DATA` and a valid buffer ID; output acquisition permits `OK` / `NEED_DATA`.
Missing IO, unconsumed output, stopped/drained/error status and asynchronous IO
produce unavailable views without an SDK dequeue. Every logical channel is still
attempted in its original ordinal, including unavailable ports.

Acquired native objects remain owned through processing. Each is returned once
after processing, including invalid layouts. Before exposing a view, the adapter
certifies one mapped mono F32 data area, permissions, alignment, real byte extent,
and input chunk size/offset/stride/flags. It admits contiguous nonwrapped input
chunks and honors their offset. Output metadata is set only after certification.
Invalid output metadata is cleared without writing its sample memory. Acquisition
or return faults latch a backend refusal; the existing zero-capacity callback
fault path stops the engine. The control-side diagnostic identifies native buffer
refusal. Rich per-shape diagnostics remain a gap: the existing engine status may
still describe this through its generic capacity fault.

No allocation, destruction, logging, waiting, clock reads or locks were added to
the production helper. DSP mathematics, declared input latencies, sample clocks,
project state, output defaults and hardware routes are unchanged.

## Lifetime and supported dependency

The existing dependency is PipeWire >=1.6.2; actual native observations use1.6.2.
The exact upstream [filter implementation](https://github.com/PipeWire/pipewire/blob/1.6.2/src/pipewire/filter.c)
updates its synchronous port IO before emitting the public IO notification. Its
convenience getter dequeues and immediately queues a buffer without checking IO
readiness or the requested extent. The production adapter no longer uses it.
The SDK [filter API](https://docs.pipewire.org/group__pw__filter.html) exposes the
dequeue/queue operations; [events](https://docs.pipewire.org/structpw__filter__events.html)
separate processing and notification contexts. Current online documentation may
describe a later SDK; exact1.6.2 source/header hashes are retained separately.

Prepared port descriptors live until native loops join. The atomic IO pointer
publishes readiness; it does not reclaim the pointed-to SDK memory. The adapter
relies on the SDK's synchronous per-port IO lifetime and serialized replacement /
removal, as the SDK processing implementation itself does. The retained exact
client-node / remote-node / impl-port source assessment distinguishes stable
port IO from replaced mix mappings. This is not qualification of every later SDK,
arbitrary asynchronous graphs or hot port reconfiguration. No PipeWire source is
copied into the product and no dependency is added.

## Test-only observations

The original logical marker positions are preserved. Version2 observations
separately count actual SDK dequeues, returns and queues, and retain exact native
object matching, IO state, byte extent and chunk metadata. Addresses are not
serialized. Legacy version1 archived observations remain readable.

The source23 off-RT first-buffer notification hold now uses `production-ready`.
The genuine audio callback sees one allocated buffer with no published IO. The
production helper returns unavailable with **zero SDK dequeues/queues**, without
test API suppression. After IO arrives that same logical port becomes usable.
The audio thread never waits for this test-only hold.

The observer still admits four filters,64 ports,64 native identities and8,192 rows
per filter, allocating its storage before activation. Its added per-query fields
increase observer memory: the measured64-bit build uses104 bytes per query and
55,902,208 bytes per8,192-row filter array (167,706,624 bytes for three such arrays,
excluding the other observer storage). This diagnostic fixture is not the production engine's
per-track memory estimate or a sustained-performance claim. Outer RT violations
and wrapper wall/CPU timing remain independently checked.

## Actual native results

All runs use owned generated32-channel input and a stereo sink at48kHz /256 frames.
Original waveform, recovery, current-cycle, priority interruption, deadline, RT
and routing gates remain unchanged.

| Run | Full raw / recovered samples | Output samples | Owner wrapper max wall / CPU ns |
|---|---:|---:|---:|
| Held startup, production readiness |37,696 /37,696|99,328|725,335 /710,452|
| Ordinary early Stop |37,696 /37,696|99,328|675,109 /672,116|
| Ordinary early Cancel |37,696 /37,696|99,328|719,791 /715,175|

Every sample in these original nonflat stereo output oracles matches exactly.
All32 current-cycle channel markers pass, including the previously affected23.
Default-device metadata and pre-existing links are preserved; owned routes are
removed on termination. These bounded runs qualify the measured startup and
interruption workflows, not full M2 or sustained studio workloads.

Synthetic helper tests exercise readiness/status transitions, sparse32-port
logical coverage, empty SDK queues, malformed/short/misaligned/unreadable or
unwritable areas, exact return identity, unchanged guard samples, integer overflow
and return failure. Debug, Release and ASan/UBSan/LSan results are retained.
The production-startup verifier refuses60 altered evidence cases, including
readiness, SDK counts, ownership, extent, chunk flags, logical ordinals, clock
domains and original waveform/timing gates. Retained projects can be analyzed at
an explicitly relocated path without changing original receipt bytes.

## Retained runtime failure48

The ordinary repeated-take run failed after the sink detected a skipped native
cycle. Before diagnosis its entire original project, logs and exact source /
executable manifest were frozen. The owner's maximum wall / CPU callback was
4,344,185 /4,342,779ns:81.4535% of its5,333,333ns period, exceeding the unchanged80%
budget. Its callback also ended after its clock cycle. The stage responsible for
the CPU outlier is not observed in this fixture; acquisition is not established
as its cause. EQ already clears tiny recurrence state; denormals are not an
established explanation.

Read-only independent analysis verifies all96 original lane files across the
three takes: **1,557,696 raw samples** match exact expected timing without shifts.
The saved **452,608 stereo prefix samples** match the original prepared nonflat
mix oracle exactly, with peak3.0062 and retained float headroom. All298 frozen
payload files remain byte-identical. The480,000-frame target did not complete;
these checks do not establish full output, Save/reopen, grouped Undo/Redo,
recovery or sustained timing success for this failed run.

Runtime observations are now48. Original46/37 still lack their original IO/queue
terms. Earlier CPU, clock, queue and sustained failures remain open. Native
Windows, physical audio, late-Cancel finalization, actual Qt manual-control / Undo,
X004/X005/X006 and European localization remain unqualified. All92 frozen
contracts remain unpromoted; the current256-track implementation ceiling remains.

## Reproduction and next work

Run local CTest `native-buffer-acquisition` on the existing Linux native build.
Hosted native-off CI does not run this C++ helper test. Build the opt-in
`sc-pipewire-manual-startup-fixture`, use
`SC_NATIVE_STARTUP_POLICY=production-ready` with the manual fault reader's
`early-stop` mode, then run `verify_native_buffer_startup.py` and
`native_buffer_startup_verifier_tests.py` with its receipt. Both accept an explicit
`--project` for relocated original evidence. Ordinary Stop/Cancel use the handoff
fixture without a startup hold. Live runs require termination of all owned local
CPU builders/test producers first; preserve failures before analysis.

Next: instrument the actual repeated-take processing stages to locate the CPU
outlier while retaining full waveform/cycle gates, then complete manual Qt
controls and late-Cancel / monitoring Undo acceptance. Track scaling continues
under [X006](67-track-scalability.md); no larger track-count claim is made here.
