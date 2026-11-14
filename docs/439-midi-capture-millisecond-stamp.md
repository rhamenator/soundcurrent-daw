# Original MIDI capture millisecond stamps

`MidiMillisecondCaptureStamp` preserves original unsigned 32-bit milliseconds since
native stream start, with an application endpoint/stream/capture epoch identity,
caller sequence, original data/consumer-behind notice, and optional caller-declared
uncertainty in 100 ns units. It is an owned, bounded, trivially copyable value with
no invalid default. Its checked factory rejects zero identities and unknown notices.
A timestamp or sequence of zero is valid. Unknown uncertainty remains distinct from
an explicit zero declaration; declarations are not validated measurement evidence.

This domain is explicit in the type and accessor. Neither the timestamp nor its
uncertainty is a QPC origin or a project frame. Original 32-bit rollover and uint64
sequence values are preserved without extension, arithmetic or inferred ordering.
The caller owns sequence and capture epoch assignment, including restart/rollover
policy. Capture epochs are separate from transport scheduling generations: project
seeks and loops must not silently restart native capture time. There is no implicit
conversion into the existing project-frame MIDI ingress or audio clock correlator.

The consumer-behind notice preserves a notification alongside the original event
stamp; it does not assert that this event was dropped. Native callback adapters,
serialization/reentrancy, message/stamp pairing, endpoint registration, clock
correlation, bounded critical delivery/panic and late-event policy remain required.
Graph-cycle sample offsets, QPC-native clocks, UMP/SysEx payloads, device handles,
recording and hardware timing qualification are outside this narrow value contract.

Focused tests cover identity/notice refusals, zero/max timestamps and sequences,
unknown/zero/max uncertainty, original ownership, copy equality, raw rollover and
capture restart epochs. CI selects the same C++20 source on Linux and actual native
Windows. Linux allocation/pthread and Windows C++ allocation instrumentation retain
the existing audit limits; zero counters are not complete callback-safety proof.
