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
configured number of prepared FIFO slots. It classifies note-off, note-on with
zero velocity, CC64/66/69 below 64, CC120/121/123/124-127 at any value, System
Stop, and System Reset as releases. CC121 can release held sustain by resetting
controller state; CC124-127 imply All Notes Off; CC122 Local Control is excluded.
Accepted release and ordinary messages retain one FIFO order and the same strict
accepted-sequence watermark.

Status and first-record behavior:

- `Accepted`: the event enters the FIFO.
- `Full`: admission failed because the FIFO push found no available slot; this
  is also the result for queue exhaustion when reserve is zero.
- `ReservedForRelease`: an ordinary event was refused while some slots remained
  protected by the nonzero reserve. This is backpressure, not a capture fault.
- `ReleaseReserveFull`: a release event could not enter a full queue despite a
  configured nonzero reserve; reserve is bounded preference, not guaranteed
  delivery.
- `firstFault()` retains the first identity, sequence, or actual capacity fault.
  `firstReservedRefusal()` retains only the first reserve refusal; there is no
  refusal counter, so producers needing a total must count returned statuses.
  `firstReleaseOverflowFault()` retains release capacity failure separately,
  even if another actual fault already occupies `firstFault()`.

With a nonzero reserve, ordinary traffic gets `ReservedForRelease` only while at
least one slot is available. An already-full queue returns `Full` and records
that capacity fault. The constructor rejects a reserve larger than the effective
power-of-two capacity with `ProjectError(InvalidParameter)`. The original
three-argument constructor remains available; a four-argument overload opts in.
Callers opting in must handle the two new enum values, including exhaustive
switches; an in-repository search found no production consumer or switch over
this new enum yet.

A reserved refusal does not advance the accepted watermark. An accepted release
may therefore share a sequence value with a previously refused ordinary event;
consumers can see gaps and orphan note-offs, which do not prove that the matching
note-on arrived.

This is bounded admission preference, not guaranteed critical delivery: enough
release messages can fill the entire queue. It does not synthesize note-offs,
panic, reconcile voice state, or establish callback serialization. The reserve
defaults to zero to preserve the existing all-events-equal admission behavior.
A reserve equal to the effective power-of-two capacity is allowed and
deliberately configures a release-only FIFO, which refuses every ordinary event.

Tests cover preparation/counter wrap, raw timestamp wrap, exact identity,
accepted-only sequence watermark after full refusal, first-fault ownership and
immutability, release classification/reserve boundaries/overflow visibility,
producer-available empty/partial/full boundaries across counter wrap, sequence
exhaustion and maximum capacity. Bounded joined-owner fixtures observe
first-fault and reserve-refusal publication concurrently without a second payload
handoff signal. Instrumentation retains Linux C/C++ allocation/pthread and Windows
C++ allocation limits. Zero counters do not establish full native callback safety.
Native devices, multi-producer callbacks, clock mapping, guaranteed critical
delivery, voice recovery, audio routing, recording and MIDI/performance parity
remain outside this foundation.
