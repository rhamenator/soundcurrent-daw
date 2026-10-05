# S3 prepared EQ and real-time transport contracts

Implemented in the Qt-free `SoundCurrent::Engine` target. This is a processor/transport foundation, not the full workstation graph, audio backend or application. The root build uses no absolute equalizer checkout path and no automatic network fetch.

## Preparation and identity

`PreparedEq` validates a session/track off RT, allocates and touches per-band/channel histories, computes peaking coefficients, and freezes the track/processor/band UUID mapping under a nonzero generation. Limits: 1–256 channels, up to 64 peaking bands, rate 8–384 kHz, max quantum 1–65536 frames. Current session descriptors constrain frequency 20–20000 Hz strictly below Nyquist, gain ±24 dB and Q 0.1–18.

Control-side `parameterEvent` resolves stable UUIDs after renaming/reordering, validates unchanged rate/layout/band structure, and prepares a whole band's target coefficients from the updated canonical model. It never changes the audio-owned histories. A structural/rate/layout change requires a new prepared instance. EQ enable uses a separate event kind. Events are trivially copyable, at most **64 bytes**, contain an absolute int64 engine-frame timestamp and generation, and carry numeric slots rather than heap-owning IDs.

Mathematics/biquad recurrence are adapted from the audited GPL Studio snapshot in [reuse/studio](../reuse/studio/README.md); the original repo is unchanged. The [W3C Audio EQ Cookbook](https://www.w3.org/TR/2021/NOTE-audio-eq-cookbook-20210608/) is the primary mathematical reference. No forced automatic headroom or ±1 final clamp is adopted.

## Processing and smoothing

`process` accepts caller-owned planar float32 views, including same-channel in-place views. Cross-channel aliasing, insufficient backing storage and concurrent use are prohibited by the caller contract. Preflight validates shape, event count/order/generation and frame range before writing samples or changing filter state. On contract failure, output is untouched; a future device adapter must silence its valid output buffers and surface the error.

The same prepared class serves live and offline processing, with private state per instance. It reports zero algorithmic latency and an **until-silent IIR tail**, not a fabricated finite decay length. Exact callback/GUI/device latency and export tail policy remain later adapter work.

At the specified sample, start a linear normalized-coefficient ramp (default **10 ms**, configurable during preparation from 1–20 ms). The first step applies at that sample; the final step sets the exact target. No trig/pow, allocation, free, I/O, logging or locking is performed in processing. Repeated gestures ramp from the current coefficients and never reset histories. This is coefficient interpolation, not a claim of exact exponential-frequency or perceptually uniform interpolation. Stable denominator endpoints satisfy the second-order stability inequalities; tested interpolation paths preserve bounded output. Listening quality for extreme rapid gestures still requires qualification.

Enable/bypass uses the same-duration wet/dry ramp. Filters keep running while bypassed so their histories remain warm. Exact dry output and resumed unchanged wet history are tested at the end of the ramp. Tiny histories below 1e-30 are zeroed to avoid very small recursive values.

Internal float overs remain intact. Nonfinite input samples become zero with a report count. If a cascade exceeds float32 representational range or develops nonfinite state, report a numeric-fault sample, reset only that channel's histories, and output zero (or the finite dry sample when fully bypassed). This is an explicit representational-failure policy, not normal peak limiting. Future UI/backend handling must expose those counts.

## Live events and bounded queues

`EqLiveDriver` has one session-owner producer and one audio-owner consumer. It owns a **4096-record** fixed queue and a **1024-record** block-event scratch array. Full queues return `Full` without overwriting unread events or consuming producer ordering state. Equal timestamps retain ingress order; reversed timestamps/generations fail visibly. Future events stay queued until their block.

More than 1024 due events in a block, a late timestamp, or a processing contract error latches a stopped driver. It does not apply accepted events at an invented later time. Resume requires stopped control-side reconstruction/reset of the driver. GUI coalescing, interactive scheduling without reading audio-owned fields, automation precedence and backend error presentation are not yet implemented; they must be added before S6 controls ship. `frame()` and `stopped()` are audio-owner accessors, not thread-safe GUI polling APIs.

The SPSC primitive uses lock-free uint32 counters, acquire/release publication and separated cache lines. Capacity is a fixed power of two. Tests cover FIFO content across counter wrap, full/peek/backpressure and a million records transferred concurrently. There are no spin waits in queue operations; polling/yielding belongs only to the non-RT test producer/consumer harness.

## Prepared-object retirement

`RtObjectExchange<T>` supplies two ready tokens, eight retirement tokens and twelve control-owned slots. Publication preserves caller ownership on backpressure. Audio replacement reserves a retirement credit before consuming a ready token, retains the previous object for transition/tail use, and retires it only after the audio owner finishes its last access. Control collects and destroys retired objects. When credits are exhausted, retain the active object and leave the next publication queued. Stop callbacks before destroying the exchange.

This establishes **single-audio-owner lifetime transport**. It does not yet implement graph compilation, state migration, crossfade rendering, PDC, event rebinding across generations, plugin children, or DSP-worker epoch acknowledgments. A pointer swap or this token transport alone cannot prove those wider graph requirements.

## Evidence and remaining gates

[S3 results](../tests/results/SLICE-001/2026-10-05-prepared-eq.json) record rates, quantum sizes, gain/overs results, event partition/live-offline fixtures, queue wrap/concurrency and retirement-credit tests. Instrumentation covers ordinary/aligned C++ new/delete and, on Linux, direct linked malloc/calloc/realloc/free and pthread-mutex-lock calls in marked host regions. It does not intercept every transitive shared-library operation or syscall. No physical device or GUI path is exercised.

Remaining: actual capture/playback, graph transition and event integration, native Windows runtime, deadline/stress qualification on declared hardware, mlock/degraded-mode reporting, UI event-to-audio latency, and subjective ramp quality. These remain explicit gates in the full goal. Next is S4's capture slab pool/disk worker/journal before the S5 native adapter.
