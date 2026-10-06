# S5 shared audio bridge and Linux PipeWire foundation

**Implemented and narrowly qualified; S5 and SLICE-001 remain incomplete.** `SoundCurrent::AudioBridge` is Qt/backend-free. `SoundCurrent::PipeWire` is the Linux adapter. [Results](../tests/results/SLICE-001/2026-10-05-native-audio.json) distinguish the native owned-source tests from simulated clock faults and Windows compilation.

## Callback and control ownership

Prepare the session, `PreparedEq`, capture pools and bridge off RT. One audio callback owns the mutable processor/transport/capture producer. One control producer submits bounded sample-timed scalar events; one control consumer drains the 64-entry observation queue. A full observation queue drops new summaries and increments a counter; metering cannot delay capture. `prepared()` is for immutable event preparation, never direct live DSP mutation.

The bridge processes planar float input through the same prepared EQ used by private offline tests. Raw capture precedes processing. An optional processed test tap uses a separate pool and disk worker; it is an integration fixture facility, not a second normal recording path. Peaks and nonfinite/numeric-fault counts accompany device/engine timestamps. Internal float overs are preserved. No GUI work, file I/O, allocation/free, ordinary blocking lock, logging or worker wakeup occurs in the host callback path.

Stop requests and native faults are atomic control-to-audio latches. Terminal states are sticky: a late device event cannot turn a completed range into a failed take. Callbacks finish pipes on terminal transitions; if device removal prevents further callbacks, the control owner stops/joins the native adapter before `finishQuiescent()` transfers ownership. Disk finalization/join follows callback shutdown. Pipes and callback context must outlive both owners.

## Timing and fault semantics

`DeviceBlockClock` carries unsigned device position/duration/monotonic nanoseconds, clock ID, cycle, rational sample period and signed driver delay. Engine frames remain signed session sample indices; they do not start at the machine's large device position. The first valid callback publishes an immutable `CaptureTimingOrigin` into the raw/test-tap pipes before sample publication. Journal1.1 persists this origin and the terminal reason; old1.0 journals remain readable. The device origin belongs to the take journal, not a change to project schema1.0.

PipeWire's **clock.cycle advances on each processing cycle**. It is diagnostic, not a hardware generation ID. Continuity requires the same clock ID and the next device position equal to the preceding position plus duration. Initial/cumulative device position may exceed32 bits. The bridge accepts changing quanta within its prepared capacity, but stops on rate change, oversized quantum, reported xrun/discontinuity, a clock gap/ID change, missing buffers, capture exhaustion or processor failure. It never hides a hole by renumbering frames or silently resampling.

The first range test captures exactly480000 frames even though its final callback is1024 frames; the final raw/test-tap slab is shortened to the requested extent. Monitoring still processes the whole valid callback. Stopped/faulted callbacks silence only capacity-certified output views. A native quantum beyond the adapter's own buffer bound cannot be safely mapped or zeroed; it delivers an invalid block without uncertified pointers, and control must shut down/reprepare the adapter. Silence for such an unsupported native quantum is not qualified.

Rate/quantum changes currently produce a recoverable stopped prefix. **Automatic reprepare/restart, general device reconnection, measured input/output latency and PDC are not implemented.** Driver delay is recorded for diagnosis and is never substituted for measured recording alignment. A separately supplied S4 latency still shifts/trims clips non-destructively.

## Native adapter and routes

The current Linux implementation requires **libpipewire0.3 API1.6.2 or later**, with runtime qualification on installed1.6.2. This development minimum is not yet a supported-distribution policy; older Ubuntu/RHEL APIs need feature-gated adaptation and independent qualification before packages are offered.

An inactive `pw_filter` publishes mapped mono float ports with `PW_FILTER_FLAG_RT_PROCESS`; it runs on PipeWire's data loop. A separate thread loop handles registry, port selection, links and lifecycle. The library uses the existing user daemon. It does not set global default metadata, autoconnect, force a rate/quantum, create a replacement server, or use the equalizer's filter-chain route.

Native port descriptors include node ID/serial/name and port ID/name/direction. All selected channels are preflighted against the current registry before any link is created. Locally failed creation rolls back pending proxies; asynchronous server rejection remains a control error requiring shutdown. Repeated route setup requires a new inactive adapter. Numeric IDs alone are insufficient for selection; persistent project routing stores intent and will need explicit resolution/placeholder UI after restart.

Link removal on either side, filter/core errors and bounded inventory failures notify control without reconnecting to an arbitrary default device. Node/float-port/link inventories are bounded to16384/65536/65536 entries on the control loop. Successful local proxy creation does not prove the server has activated the link. The current fixture proves activation by independent native sink samples.

`stop()` disconnects/destroys the filter, removes listeners/proxies, destroys its context (joining owned data loops), then stops/destroys the control loop and balances its reference-counted `pw_init()` with `pw_deinit()`. It must run on the control owner, never inside the callback. The lifecycle review uses exact upstream1.6.2 [filter](https://github.com/PipeWire/pipewire/blob/1.6.2/src/pipewire/filter.c), [context](https://github.com/PipeWire/pipewire/blob/1.6.2/src/pipewire/context.c) and [scheduler](https://github.com/PipeWire/pipewire/blob/1.6.2/src/pipewire/impl-node.c) source; hashes are in [the review manifest](../research/pipewire-1.6.2-review.json). Framework-free core destruction remains separate from backend joining.

## Evidence and limits

The opt-in native fixture routes **owned source → owned track → owned sink**, creates three production disk workers, compares every raw/processed/independent-sink sample, attaches the raw take, and saves/reopens the project. Normal recording is ten seconds at48 kHz mono, server quantum1024. Source removal stops after a measured prefix. Stale identities, wrong directions and repeated setup are rejected. The Python harness samples actual links/default metadata before/during/after, verifies pre-existing links remain intact and all owned nodes/links disappear. These tests may share an existing hardware scheduling clock without connecting an audio port to hardware.

Host callback allocation/free/direct mutex instrumentation reports zero. It does not intercept every operation inside shared libraries, prove a callback deadline, guarantee memory locking, or establish full multichannel/native-device qualification. `memoryLocked()` reports **false**. Linux headless tests also cover simulated rate/quantum/xrun/clock/queue faults and sample-timed EQ edits; they are not evidence of physical device renegotiation.

The five headless CTest groups pass with ASan/UBSan and leak detection. Native ASan/UBSan audio/sink/disconnect tests pass with `PIPEWIRE_DLCLOSE=false` (modules retained), while normal module unloading reports **4084 bytes in34 allocations** at exit. An independent [context-only probe](../experiments/pipewire-lifecycle-probe.cpp), with no DAW/ports/routes, reproduces **11031 bytes in102 allocations** under installed1.6.2. Init-only and retained-module baselines pass. This narrows the report to dependency/context loading; it does not prove normal unload is leak-free. No suppression is applied and the native unload/memory qualification gate remains open.

To reproduce the independent diagnostic, compile the probe separately with installed PipeWire headers/libraries and `-fsanitize=address,undefined`; run it normally and with `--init-only`, then compare `PIPEWIRE_DLCLOSE=false`. It changes no audio routes and is not an automatic CTest or a production runtime setting.

Windows cross-compiles the shared bridge and journal1.1 recovery fixtures. No native WASAPI/ASIO adapter or native Windows runtime evidence exists yet. Hardware round-trip alignment, native deadline/load tests, mlock/degraded UI, multitrack admission, production playback controller/GUI, offline export and full frozen-reference parity remain open.

## Next implementation task

S6a's bounded disk read-ahead, non-destructive take playback and off-RT seek retirement now exist; see [the playback contract](15-playback-contract.md). Next connect the first Qt recording/playback/EQ UI (S6), followed by transactional offline WAV export (S7). These tasks retain the native import, Windows, localization and outstanding S4/S5 qualification requirements.

S6f replaces fixture-only capture assembly with the [production recording owner](21-native-recording-owner.md). The current fixture copies raw input before EQ and compares a separate native monitor sink; monitoring Off has no output ports. Native recovery/destruction/activation failure are exercised separately from physical device and deadline qualification.
