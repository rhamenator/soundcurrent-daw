# Consumer-owned MIDI 1.0 block staging

`PreparedMidi1BlockStaging` bridges the existing typed SPSC ingress to transactional
`admitMidi1Block`. A single consumer owns its pending events and all queue pops.
Preparation binds one particular ingress instance with its already validated immutable
endpoint/generation and reserves 1,025 owned validated events outside callbacks.
The ingress must outlive staging; there is no API to switch queues or reset identity. It never resets, resizes or discards
pending events during use; retire after queue owners join. It is not copyable or
movable. The borrowed pending view expires on mutation or retirement.

Each call first validates the complete block window and exact ingress/transport
identity, without popping on refusal. A retained event before the requested start
refuses explicitly rather than being clamped or silently dropped. The consumer
then transfers at most 1,025 events, stopping at the first event at or beyond the
exclusive block end. Accepted ingress ordering means later events remain future.
An empty queue ends that call's observed prefix; this is not a guarantee that a
producer cannot publish a later event timestamped inside the current window.
Late-event handling remains an explicit higher-level policy.

The report distinguishes queue-to-staging `transferred` ownership from the block's
`emitted` count. Events popped from the queue remain owned in prepared staging
until block admission succeeds. Output-capacity or event-budget refusal leaves
output unchanged and retains every transferred pending event for inspection or
retry. On success only the emitted prefix retires; the first future event carries
into later calls, preserving equal-frame FIFO. At 1,025 active events the existing
1,024-event block limit refuses; subsequent retries do not drain more queued data.
This bounded storage does not supply critical delivery, panic/voice recovery or an
automatic overload policy. No drop/reset API or silent success is provided.

Hot work has fixed bounds and uses reserved vector capacity with nothrow owned
value copies. Tests cover boundary carry, equal-time FIFO, output refusal/retry,
retained late input, generation/window refusals before popping, exact-limit success, and overload with
both pending and queued ownership preserved. Linux allocation/pthread and Windows
C++ allocation counters retain existing limits. Zero counters are not complete
callback-safety proof. No timestamp projection, native device callback, audio-graph
wiring, recording, critical-message reservation or MIDI/performance parity is added.
