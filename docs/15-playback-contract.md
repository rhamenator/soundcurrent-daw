# S6a bounded read-ahead and take playback

**Implemented foundation; first recording slice and full DAW remain incomplete.** `SoundCurrent::Playback` is Qt/OS/file-library-free; it links the existing capture admission bounds and engine. Disk-side `TrackReader` and `PlaybackRun` live in `SoundCurrent::Media`. [Evidence](../tests/results/SLICE-001/2026-10-05-playback.json) separates simulated callback, real PipeWire, sanitizer and Windows cross-build results.

## Preparation and ownership

`PlaybackConfig` declares session rate/layout, maximum callback size, slab size, memory budget, nonempty signed frame range and nonzero preparation generation. Reuse the S4 pool admission rules:32 slabs, two-second default reserve,256–65536-frame slabs,8–384 kHz,1–256 channels,128 MiB default/256 MiB maximum **pool** budget. Explicitly small slabs reduce reserve. This budget does not account for all simultaneously prepared generations, EQ input buffers, reader scratch buffers, file handles or a future multitrack graph; combined admission remains required.

One disk producer obtains one writable interleaved slab and commits the exact token, count and contiguous timeline start. One audio consumer borrows ready slabs, transposes to planar scratch buffers and returns tokens after its last sample use. Free/ready SPSC queues have64 entries for32 actual slabs. There is no shared buffer reset during playback, no overwrite of unread data, and no RT refcount/destructor. Pool construction touches its memory off RT; memory locking is not qualified.

`PlaybackRun` constructs the pipe, private EQ instance and reader on a **preparation owner**, validates and prefills at most32 slabs before publication, then starts a disk worker if the range extends beyond the pool. Linux uses a standard thread; Windows uses a Win32 thread with the existing MinGW toolchain. Every file/decoder/hash/mix/seek/read operation stays on preparation or disk owners. The audio `process` method uses fixed scratch/views, the pipe and the existing bounded EQ event driver.

`requestStop()` is a control request consumed by audio; it does not mutate live DSP from the GUI. Terminal completion/stop/failure cancels a blocked-on-full-pool reader through an atomic flag. A control cancellation/retirement then joins the reader before destruction. `waitReader()` must be called after consuming the range or requesting/canceling stop; an active reader can legitimately wait for free slabs. Joins/file close/destruction must never occur inside the callback.

## Source and timeline semantics

The reader validates the session and selects intersecting clips from one track. It checks owned relative paths/ancestor directories, file hashes and WAV/WAVEX/RF64 metadata against the asset, then binds read-only descriptors. The default open-asset budget is64, configurable up to4096; overflow fails admission explicitly. A bounded descriptor cache for larger working sets remains M2 work. General codec/security sandbox qualification is not inferred from owned recording fixtures; hostile concurrent filesystem replacement remains outside this owned-project contract.

For each slab, intersection frames map to `clip.sourceFrame + (timelineFrame - clip.startFrame)`. Subtraction occurs before addition, avoiding an intermediate signed overflow at very large timeline positions. Trimmed source portions remain intact. Gaps between clips are deliberate zero input and count as available data. Overlapping clips sum in project clip order using worker-side double accumulation, then convert once to float32; ordinary overs are preserved. Nonfinite source/sum results are replaced with zero and counted through an atomic reader diagnostic.

All source rates must match the session rate. Different rates fail explicitly rather than silently play at the wrong speed; prepared resampling remains M2/M8 work. Layouts must match the track. Named surround layouts, general graph mixing/PDC, clip fades, stretch/pitch and plugin routing retain their milestones.

## Underflow, completion and bounds

Every valid render advances the timeline by the requested extent within the declared range. Missing disk data is zero source input, with exact missing-frame counts; processing may retain the EQ's normal response/tail. A later slab whose timestamp is behind the current cursor is skipped or partially trimmed. It cannot delay the song, replay stale samples or renumber frames to hide a gap.

A callback processes at most32 stale slabs plus the number of minimum admitted slabs needed by the maximal callback, plus two boundary transitions. This fixed budget prevents producer catch-up from turning a callback into an unbounded drain loop. Exhausting it counts the unfilled suffix as underflow. Final range extents are exact and unused output-buffer suffixes are zero. `Complete` can coexist with a nonzero final missing count; consumers must inspect the counters before claiming a successful playback.

Unexpected timeline jumps latch a timing failure; invalid views/capacity are rejected without dereferencing uncertified pointers. Reader failure stops/silences processing and is surfaced by `waitReader()` after join. Occupancy counts include a partly consumed active slab and are point-in-time diagnostics, not a sample-exact prefill horizon. Counter loads are lock-free on the admitted x64 platforms.

The playback range deliberately crops at its end. Automatically appended processor tails and export tail policies are S7/M4 work. Scheduled EQ events use session-frame timestamps and existing generation/budget/smoothing rules; GUI immediate-event ingress and end-to-end fader latency still need S6 integration and measurement.

## Seek generation retirement

Prepare a new `PlaybackRun` with the requested start and a new generation off RT. Publish it through the existing2-ready/8-retired/12-owner `RtObjectExchange`. At a block boundary, audio adopts the prepared instance and reserves retirement capacity before releasing the previous one. Control drains retired tokens, cancels/joins their disk workers and frees resources. Backpressure retains the active instance or caller-owned candidate; no object is destroyed to relieve RT pressure.

The fixture proves that the new instance starts at the requested source/timeline extent and rejects parameter events prepared for the old generation. This is safe lifetime/timestamp transport, not a qualified seamless crossfade, general graph migration or production seek/UI controller. Those remain open and must not be inferred from a successful hard replacement.

## Usable developer checks

```sh
.cache/build-core/sc-play-tool verify PROJECT_DIRECTORY
python3 tests/verify_play_cli.py
python3 tests/verify_pipewire_fixture.py \
  --binary .cache/build-core/sc-pipewire-playback-fixture
```

`verify` reopens a saved project and processes its first track without a device, project/media writes or export. It honors a nonempty export selection; otherwise it checks from the saved playhead through the last clip. It prints frame extent, missing count and peak, and returns failure for missing frames/errors. The CLI fixture first records a new synthetic take, then verifies480000 frames and hashes every project/media file before/after. Unicode paths are exercised on Linux; Windows execution remains unqualified.

The opt-in native fixture independently creates a raw RF64 source, saves/reopens it, prepares the file reader and routes an **owned playback node → owned sink** using the existing PipeWire adapter. An independent disk capture of the sink is compared sample-by-sample against the original known source through a private EQ at a different block size. It tests ten-second mono48 kHz playback and sink removal. Initial unmapped-output priming does not consume file frames. Test clock checks and node ownership are fixture integration, not a complete production playback/device controller. The graph/default preservation harness runs native fixtures **serially**.

## Qualification and remaining work

Headless fixtures cover32-bit-boundary and near-int64-limit timeline positions, layouts1/2/8/32/256, callback sizes16/64/127/512/2048, trim/overlap/gaps, shared-EQ control events, nonfinite counts/overs, underflow/stale data, token/range admission, stop, worker read/preparation failure, source hash/rate/metadata mismatch and Linux symlink rejection. They include callback allocation/free/direct mutex instrumentation and control-side worker retirement.

Linux ASan/UBSan headless tests pass with leak detection. Native sanitizer runs use the explicitly documented `PIPEWIRE_DLCLOSE=false` diagnostic; **normal PipeWire module-unload memory qualification remains open** from S5. No suppression or production environment change is introduced. Native tests are owned mono ports, not physical hardware, native Windows, all-channel/multitrack or deadline/load evidence.

Next is **S6: the first Qt recording/playback/EQ UI**, including asynchronous preparation, production device/transport ownership, immediate control ingress, meters, keyboard/focus/undo, recovery/error presentation and desktop integration. Follow with S7 transactional offline WAV export. Preserve the frozen full-suite, Windows, localization, native import and remaining S4/S5 gates.
