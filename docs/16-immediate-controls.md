# S6b immediate control ingress

**Implemented engine/adapter foundation; Qt application and whole SLICE-001 remain incomplete.** Mouse/keyboard edits need their own ingress path: reading the audio-owned frame counter to invent a timestamp can make a slow control task submit an already-late event. `EqLiveDriver::submitImmediate` accepts a prepared event and a positive, strictly increasing control revision. Its timestamp is deliberately ignored, including stale/future/negative values; generation, target and finite/stable coefficients are still validated through the immutable prepared mapping. Coefficient preparation and session validation remain outside callbacks.

## Ownership and bounded application

One session/control owner produces both scheduled and immediate edits; one audio owner processes them. Preparation and object destruction remain governed by the existing generation/retirement contract. Calling submit on a retired object is not permitted. A successful enqueue means **accepted**, not yet applied or audible. Terminal transport state must cause the future controller to stop sending to that generation.

The scheduled queue retains4096 records and its1024-due-event block limit. Immediate edits have a separate128-record SPSC queue. `Full` preserves queued edits and the revision admission state, allowing a retry with the same revision; `Invalid` and `OutOfOrder` also leave that state unchanged. There is no overwrite, spin, lock, allocation or coefficient computation inside processing. Control revisions may have gaps; they must not wrap/restart within a live driver. A new generation can restart its revision sequence.

For each valid nonempty callback, audio snapshots the immediate queue's published count once. It consumes only that prefix, at most128 commands; later producer publications wait for the next valid block. Invalid buffers and zero-frame callbacks retain immediate edits. A scheduled timing/budget fault terminates processing and does not claim those edits applied. The combined fixed scratch/direct-processor limit is1152 events (1024 scheduled plus128 immediate); increasing that bound does not change the scheduled overload limit. This remains a per-processor count bound, not a CPU/deadline certification for maximal graphs.

Events merge in this order:

1. Scheduled automation exactly at the current block boundary.
2. The snapshotted immediate edits, in submission order, at that same boundary.
3. Scheduled events at later samples in the block.

Thus the last same-target manual edit wins at the boundary, while later automation remains sample accurate. Band coefficients and enable retain the existing10 ms ramp and filter histories. A far-future scheduled event cannot hold up a manual edit. Full automation read/touch/latch/write modes and their overrides are M3 work; this primitive's deterministic ordering alone does not implement them.

## Applied receipts and replay

After successful processing, a64-record SPSC diagnostic queue publishes `ImmediateAcknowledgement`: generation, last applied revision, exact session-frame block boundary and number of immediate commands applied in that block. A single control consumer polls it without reading audio-owned counters. One receipt represents the FIFO prefix applied in that block; intermediate same-target values may have been superseded before the first sample, as with same-frame scheduled events.

Receipt pressure drops the new receipt and increments a lock-free loss counter; it never stops audio or stalls command ingress. It does **not** provide a durable automation/performance log. The controller must retain prepared payloads and check receipt losses if it needs exact replay. A missing receipt cannot prove a command was unapplied, and a later receipt cannot recover a lost earlier block timestamp. Future durable automation recording needs its own admission/backpressure policy.

`AudioBridge`, `PlaybackProcessor` and disk-side `PlaybackRun` forward submission and receipt APIs. Recording still captures raw input; monitoring/playback use the private in-process EQ. These operations neither save the project nor perform undo themselves; the S6 controller must integrate semantic gestures, queue-pressure presentation, retired-generation rebinding and session persistence.

## Evidence

[Results](../tests/results/SLICE-001/2026-10-05-immediate-controls.json) cover:

- Gain, frequency, Q and enable at8/44.1/48/96/384 kHz and16/64/127/512/2048-frame blocks, including a10-billion-frame start.
- Mixed automation/manual ordering, same-target FIFO, stale timestamps, far-future automation, invalid generation/nonfinite commands, retry after full, empty/invalid blocks, receipt loss, timed overload and the full1152-event combined bound.
- Published-prefix queue counts across uint32 wrap and publications after a snapshot.
- A separate producer concurrently submits20000 edits; receipts reconstruct the exact applied-frame events in a private offline instance. Every output sample matches; callback ordinary C/C++ allocation/free/direct mutex counters remain zero.
- Shared recording bridge and file read-ahead playback integration, with offline replay and rejection of old-generation immediate edits after seek.
- An opt-in real PipeWire owned file-player → owned sink workflow submits a gain edit from the control thread during playback. Its applied-frame receipt drives a reference render with a different block partition; all captured samples match. The harness preserves existing links/defaults and removes its nodes. Sink removal still surfaces explicitly.

The native `submit_to_receipt_poll_ms` is an **observed control-submit to receipt-poll duration**. It includes the2 ms control polling interval and scheduler delay; it is not GUI-to-speaker latency or a worst-case deadline. Callback application uses the exact reported session frame. No physical port, default-device change, subjective quality, Qt event loop or Windows runtime is qualified here. Normal PipeWire unload-memory qualification remains open; native sanitizer evidence uses the existing retained-module diagnostic, not a production setting.

## Next implementation

Continue S6 with the asynchronous preparation/session controller and Qt transport/arm/monitor/EQ controls. Consume receipts/errors/meters on the control owner, connect semantic undo gestures, provide keyboard/focus behavior and desktop integration, and measure real GUI-to-engine latency. S7 transactional WAV export and all frozen-reference/Windows/localization/import milestones remain required.

One initial native sanitizer attempt stopped at134400 captured frames with a playback gap; the original diagnostic did not distinguish sink-clock loss from reader underflow. Its log is retained in the evidence manifest. The added source-quantum and sink-clock/missing-frame diagnostics support further qualification; a subsequent successful run does not establish the cause or remove the deadline/load gate.
