# Checkpoint172: separate looped project position from the stream clock

This is a finite, in-process audio-loop foundation for M2/P005. The shared live
and offline reader now repeats arrangement audio. Desktop loop controls, native
recording qualification and complete frozen-reference parity remain open.

## Timing and ownership

`PlaybackTimeline` is prepared off the audio thread with a nonnegative stream
range and an optional half-open project loop. Stream position always advances.
Project position wraps to the loop beginning; pass number is an unsigned value.
Caller-owned segments stop at a wrap or the finite stream end. One-frame loops,
multiple wraps inside a block and a partial final pass require no segment vector
or callback allocation. Invalid/overflowing pass queries leave their outputs
unchanged. A terminal cursor is a position, not another playable frame.

`TrackReader` admits clips, asset handles and resampling buffers for the project
loop range, even when the stream origin lies far outside the arrangement. Its
existing disk owner subdivides a slab at loop boundaries, reads the original
source coordinates and evaluates clip-relative fades before summing overlapping
voices in their existing order. The published slab keeps its monotonic stream
origin. Read-ahead queues, shared cache budgets and retirement ownership remain
bounded. Disk reads, allocations and error reporting stay outside callbacks.

`MixPlayback` reports both stream and project positions plus the pass ordinal.
The prepared graph, EQ state, smoothing and parameter-event clock do not reset
at a wrap. Events in this slice are stream timed. Repeating project automation,
tempo maps, plugin transport information and expressive-note behavior require
separate implementation and acceptance; this change does not qualify them.

The native/device and capture clocks remain monotonic. Manual take In/Out
commands use stream boundaries; each input's existing latency/postroll can extend
past a project wrap. Completed groups retain their original stream capture
extents and device origins. Explicit admission can place their inactive take
clips at the project loop beginning without rewriting raw media. Stop joins
workers and verifies partial checkpoints. Cancel marks still-owned groups
canceled and preserves independently committed checkpoints; it does not promise
that an uncommitted prefix was fully written or silently adopt a partial take.

## Offline behavior

An explicit `ExportSettings.loop` repeats the same project range for a finite
stream duration using the shared reader and private graph. Loop exports start
processing at their explicit stream origin. They do not apply the linear
arrangement's earlier-clip preroll heuristic. Existing bounded output, tail,
publication and destination-preservation rules still apply. No loop is inferred
from an ordinary range export. Default linear playback/export is unchanged.

There is no automatic loop-seam crossfade or level adjustment. Authored clip
samples/fades repeat as stored; click-free loop editing and listening quality
are separate gates. A finite export does not imply an endless transport.

## Acceptance scope

`sc-loop-playback-tests` uses owned synthetic sources and actual capture writers;
it opens no physical endpoint or VM. It checks:

- Independent frame/pass enumeration for periods1/2/7/53/256, block sizes1/13/256,
  nonzero origins, near-limit arithmetic and a partial final pass.
- Repeated raw arrangement samples against an independent source-frame oracle;
  continuous nonflat EQ and a parameter event across a wrap; 13/256-frame live
  partitions and a 37-frame offline float-WAV export.
- A distant stream origin with 44.1kHz media in a48kHz project, fractional source
  timing, overlapping clips and fades. Each wrapped lap agrees with an explicitly
  rendered linear project lap across read partitions and export.
- Five actual two-input recorded passes with seven- and29-frame input delays,
  17-frame callbacks and overlapping postroll. Raw sample values, monotonic
  device origins, complete group extents and ten accepted command receipts are
  checked. Retained takes, grouped comp Undo/Redo and Save/reopen preserve hashes.
- Stop and cancel during overlapping postroll: started partial groups and pending
  command credits are retained, unused preparations retire, incomplete/canceled
  groups refuse complete-take adoption and the saved project is unchanged.
- Resource refusal and callback allocation/free/blocking-lock counters at zero.

The source-bound Linux build, logs and outcomes are recorded in
`tests/results/M2/2026-10-11-loop-stream`. The native Windows CI target and test
selection include this workflow. Its result must be qualified independently;
Linux compilation or synthetic acceptance is not native Windows or hardware
qualification. No new shipping dependency or copied proprietary algorithm.

## Next useful preview and remaining gaps

Add visible loop locators, enable/disable and pass status to the desktop transport,
then drive this mapping through the existing Linux/Windows native owners. Persist
loop intent with undo and migration rules. Replenish upcoming recording slots
with explicit resource admission/backpressure rather than allocating in the
callback or imposing the prepared-slot count as a take inventory limit. Preserve
partial final-pass preview and explicit retention policy.

Take audition, comp crossfades, linked edits, project automation/tempo repetition,
MIDI loop modes, extended fault/load/physical/native/installed workflows and all
reference-specific suboptions remain open. Processing-quality, content and
native-project compatibility axes are not promoted. Existing stretch-quality
failures and full M2–M11/X004/X005/all-Europe requirements remain visible.
