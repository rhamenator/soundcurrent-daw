# Prepared typed MIDI 1.0 ingress

`PreparedMidi1Ingress` connects owned validated timed MIDI values to the existing
prepared SPSC storage without weakening `Midi1ShortMessage` construction or
introducing another atomic queue. It uses trivially copyable optional slots:
preparation creates empty slots, and the producer publishes engaged values only.
This permits the deliberately non-default-constructible message type to cross
the queue while preserving its byte-validation invariant.

Preparation runs outside callbacks and validates a nonzero immutable endpoint
token and transport generation. Requested capacity is one through 4,096, rounded
up to a power of two by the existing queue. Invalid preparation throws the existing
parameter error. The optional initial counter prepares an empty queue at that
counter, allowing explicit wrap qualification; it never resets an active queue.
There is exactly one producer and one consumer. Both owners must join before
retirement/destruction; there is no reset or resize during use.

The producer submits nonnegative absolute frames for that exact prepared identity
in nondecreasing accepted order. Equal-frame events retain FIFO ingress order.
Only acceptance advances the producer watermark. Full, invalid-frame,
invalid-identity and out-of-order submissions return explicit status without
overwriting accepted values. The caller owns unaccepted input and its retry or
recovery policy. A consumer pop returns one owned engaged value; an empty pop
leaves the caller's optional unchanged. Available-count observations belong to
the consumer and do not reserve producer capacity.

Tests cover capacity/wrap, retained accepted prefixes, failure without watermark
advance, identity/frame/order refusals, equal-time order, source ownership, maximum
capacity and 100,000 concurrent ordered events with joined owners. Hot-operation
instrumentation uses the existing platform limits: C/C++ allocation and pthread
mutex entry points on Linux, C++ allocation on Windows. Yielding and thread
management belong to the test harness, outside the instrumented operations.
Zero counters do not establish complete platform callback safety.

This is a bounded data lane, with no reserved critical-message capacity. It may
refuse note-off or stop just as it may refuse any other full-queue submission.
Critical delivery reservation, overload/panic recovery, sustain/seek/loop voice
lifetime, device loss/reconnect, endpoint registration and audio-block lookahead
remain required before live-performance use. No native device handles, project-ID
format, device adapter, timestamp correlation, recording or MIDI parity is added.
