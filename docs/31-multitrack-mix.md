# M2c1: shared-clock multitrack EQ and mix graph

## Implemented processing path

`PreparedMixGraph` prepares each selected track's independent EQ/event driver and
a sparse track-channel-to-output matrix. All lanes advance from one signed
64-bit project frame cursor. Track identity remains a UUID; parameter/enable
preparation resolves UUIDs to generation-scoped ordinals outside RT. Per-lane
scheduled/immediate queues retain the existing bounds and applied-frame receipts.
Matrix/state changes require preparation of another generation. Scalar EQ events
can enter the active generation through its serialized parameter producer.

The initial topology is independent track EQ → explicit matrix → one output bus.
Summing uses float64 scratch in prepared track/map order, then finite float32
output. It preserves overs above 0 dBFS. Invalid views do not advance the clock;
timing or processor failure is sticky and silences the output. Numeric overflow
is reported, never published as infinity. Nonfinite input sanitation remains
counted by the EQ; offline export refuses sanitized/faulted audio.

`MixPlayback` consumes one read-ahead pipe per track at that same cursor, then
runs the graph. A missing track contributes counted silence while healthy tracks
keep their offsets; late slabs are discarded at their original timestamps.
`missingTrackFrames` sums missing **track-frame** units, not a union of missing
output-frame intervals. Per-lane reports identify the individual gaps. Completion
can also report gaps; it never converts missing source data into a successful
exact offline render. A reader/processor failure stops the whole prepared mix.

`MixReader` opens/verifies read-only source bindings and fills at most one slab per
track per round. `MixPlaybackRun` prefills before publication, then uses one fair
disk worker. Only its `process` method belongs on the audio owner. Stop/cancel
requests use fixed-width atomics; control destroys/joins after callbacks have
stopped. It can be owned by `RtObjectExchange`; the core fixture verifies token
replacement and off-RT retirement. This does not implement crossfades, DSP state
migration, tail borrowing or device worker epochs for a general graph.

## Explicit layout and resource admission

Plans support the current mono/stereo/discrete layouts, 1–256 output channels
and up to 256 tracks. Source/destination indices, unique track IDs and unique
channel pairs are validated before DSP preparation. Coefficients are finite
within ±64 (zero and negative polarity are explicit choices). `identityMix`
requires exact matching layouts; it never guesses a downmix or omits a track.
The API accepts custom matrices for differing layouts. Named surround/Ambisonics
semantics still need their later model/normalization work.

The default total payload budget is 128 MiB, with 65,536 routing entries and the
existing maximum block size 65,536 frames. A caller can raise total/routing
admission explicitly. Individual legacy playback pipes retain their 256 MiB
allocation cap. DSP queues, wet/raw/sum/silence buffers, read-ahead pools, decoder
scratch and conservatively counted binding/file payload are admitted. This is
payload accounting, not a hard allocator/RSS or third-party decoder memory bound.
The reader defaults to 64 open assets per track and 256 open asset references
across the mix; duplicate files in distinct readers count independently. Limits
fail visibly rather than truncating the mix. Shared file caching remains future
work.

All allocation, file I/O, hashing, model validation and matrix preparation remain
outside callbacks. Callback loops are bounded by admitted tracks/channels/maps,
frames and the existing per-lane event budgets. Scratch clearing touches only the
active block extent. Large valid configurations still require measured CPU and
deadline admission; no deadline/load or hardware guarantee follows from these
structural bounds or allocation/lock audits.

## Offline WAV and concrete developer workflow

Both `exportTrackWav` and new `exportMixWav` use the same `MixPlayback`/graph path.
They retain private preroll from the earliest selected clip and bounded zero-input
tail processing. Existing file publication, source/destination revalidation,
confirmed replacement, cancellation, RF64 and durability reporting share one
transaction implementation. `ExportSettings` holds their common options; each
spec carries its own track ID or explicit mix plan.

```sh
.cache/build-desktop/sc-export-tool render-mix PROJECT_DIRECTORY OUTPUT.wav
.cache/build-desktop/sc-export-tool render-mix PROJECT_DIRECTORY OUTPUT.wav --start 113 --end 5000 --rf64
python3 tests/verify_mix_cli.py
```

The CLI renders every project track using matching-layout unity routes. Mixed
layouts are refused with an explicit matrix requirement; the core API supports
those matrices, and the GUI master/pan/routing editor remains required. Neither
command modifies the project/media or opens a device. Existing `render` continues
to export the saved first track. Monitor/room-correction paths are absent from
these plans and must not enter default exports when later integrated.

## Acceptance and remaining requirements

[Evidence](../tests/results/M2/2026-10-06-shared-mix-graph.json) separates:

- Independent direct-form-I EQ plus matrix oracle, variable block sizes,
  float headroom, event/receipt/generation/identity and failure checks.
- 32-track 29,000-frame source-coordinate/live-callback/offline WAV corpus;
  independent Python 32-track RIFF/RF64, hashes, replacement and actual SIGTERM.
- A one-track late-slab fixture preserving its healthy peer, and blocked/failed
  disk-worker fixtures retaining a shared clock and exposing gaps/errors.
- 1/2/8/32/256-channel core identity checks and zero RT allocation/free/lock
  observations on audited paths; token lifetime transport.
- Linux debug/sanitizers, unchanged legacy export workflows, Windows headless
  cross-compilation and the existing owned native single-track roundtrip.

Production PipeWire/Qt playback still prepares one selected track. The new mix
run is callback-ready and used by headless acceptance; it is not yet connected
to the production native owner/UI. No simultaneous capture/overdub, mixer project
state or general buses/sends/sidechains/PDC are claimed. The 32-track corpus is
short, not the M2 ten-minute synthetic or 30-minute declared-device gate. Native
Windows, hardware, module-unload memory, filesystem/power-loss and full
F/Q/C/N/localization/X004/X005 qualification remain open.

Next concrete task: integrate the prepared shared-clock mix into the Linux native
playback owner and desktop with explicit master layout/output routing and stable
per-track parameter reconciliation; qualify owned-node output against offline
samples. Then build one shared playback/capture callback for simultaneous armed
tracks and overdub. Punch/loop/takes/comping/fades and all later milestones remain
required without changing the frozen product target.
