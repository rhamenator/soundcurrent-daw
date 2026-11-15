# Original MIDI 1.0 capture ingress and first-fault publication

`PreparedMidi1CaptureIngress` reuses prepared SPSC storage for owned validated
packed MIDI captures, before any mapping into project frames. Its immutable
endpoint/stream/capture epoch is distinct from project transport generation.
Preparation validates identity and the existing 1..4,096 power-of-two capacity
policy outside callbacks. Optional trivially copyable slots are at most 80 bytes;
the queue's slot payload is bounded by 327,680 bytes, excluding owner/allocator
overhead. There is one producer and one consumer. An adapter must independently
establish native callback serialization before using this SPSC lane. Both owners
must join before retirement; no reset or resize is provided.

Accepted sequences increase strictly, with zero a valid first sequence. Rejected
identity, duplicate/backward sequence or full submissions never advance the
accepted watermark or overwrite accepted queue values. UINT64_MAX is retained
unchanged; a subsequent wrap to zero refuses and needs a new capture epoch/owner.
Original unsigned millisecond timestamps may repeat or wrap and are stored exactly;
this class performs no timestamp ordering, extension, correlation or projection.
The consumer receives owned values, and an empty pop leaves output unchanged.

Every refusal returns an explicit status. The first also publishes an immutable
owned copy of the rejected capture and its reason outside the data queue, so full
storage cannot prevent publication of that first fault fact. The sole producer
writes the optional payload once, then release-publishes an always-lock-free
uint32 flag; the consumer acquires the flag before copying it. After publication
the producer never mutates that payload. Later failures neither overwrite nor
clear it. There is no reset/read-and-clear race or failure-count claim.

The first-fault record is a recovery signal, not critical-message delivery, a
reserved release lane, panic output or voice recovery. Even note-off can refuse
when full. The control owner must observe outcomes and implement an explicit
recovery policy before live-performance use. Admission may continue after a
fault; this value does not pretend that subsequent acceptance repairs a lost
message. Consumer-behind notices, padding provenance and declared uncertainty
remain attached to their original values without interpretation.

An optional release reserve prevents ordinary messages from consuming the final
configured number of prepared FIFO slots. The release class includes MIDI
note-off, note-on with zero velocity, sustain/sostenuto/hold2 release (CC64/66/69
below 64), CC120/121/123/124-127, System Stop, and System Reset. CC124-127 are
reserved because those channel-mode changes imply All Notes Off; CC122 Local
Control is deliberately excluded. Accepted release and ordinary messages retain
one FIFO order and the same strict accepted-sequence
watermark. `ReservedForRelease` distinguishes ordinary admission stopped at the
reserve boundary from `Full`; `ReleaseReserveFull` reports that a configured
nonzero reserve still did not prevent release-priority messages from exhausting
the complete queue. With reserve zero all queue exhaustion, including release
messages, reports the compatible `Full` status. `firstReleaseOverflowFault()`
preserves the first capacity-overflowed release even when an earlier ordinary
event already occupies `firstFault()`; identity and sequence rejections do not
occupy this overflow-only record. The separate `firstReservedRefusal()` record
captures the first ordinary backpressure result without masking later identity,
sequence, or capacity faults reported by `firstFault()`.

CC121 is included because resetting controller state can release held sustain.
CC124-127 imply All Notes Off; CC122 Local Control is excluded. Reserved refusal
does not advance the accepted watermark, so an accepted release may share the
sequence value of a previously refused ordinary event and consumers may observe
sequence gaps. This is deliberate accepted-only ordering, not proof that the
matching note-on arrived.

This is bounded admission preference, not guaranteed critical delivery: enough
release messages can fill the entire queue. It does not synthesize note-offs,
panic, reconcile voice state, or establish callback serialization. The reserve
defaults to zero to preserve the existing all-events-equal admission behavior.
A reserve equal to the effective power-of-two capacity is allowed and
deliberately configures a release-only FIFO, which refuses every ordinary event.

Tests cover preparation/counter wrap, raw timestamp wrap, exact identity,
accepted-only sequence watermark after full refusal, first-fault ownership and
immutability, release classification/reserve boundaries/overflow visibility,
sequence exhaustion and maximum capacity. A bounded joined-owner
fixture observes first-fault publication concurrently without a second payload
handoff signal. Instrumentation retains Linux C/C++ allocation/pthread and Windows
C++ allocation limits. Zero counters do not establish full native callback safety.
Native devices, multi-producer callbacks, clock mapping, guaranteed critical
delivery, voice recovery, audio routing, recording and MIDI/performance parity
remain outside this foundation.
