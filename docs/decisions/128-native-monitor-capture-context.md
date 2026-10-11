# ADR128: bind native capture only after its actual preparation

Status: accepted for compiled adapters and prepared components; activation gated.

The native capture capacity is known only after SDK preparation, but the SDK
thread needs a stable callback address before construction. Use a pre-existing
one-shot binding, expose the admitted descriptor, construct the matching
fanout/clock/Stop context off RT, then bind and activate. Publish the context
with a release CAS. An SDK exit racing binding acquires the winning state,
retains its original end, and never reads an unpublished context pointer.

Preserve original packet bits and clock metadata. Publish raw first, clocks
second, original native end before source EOF. Keep route pressure, unavailable
HRESULTs, SDK Stop, processing faults and missing owners separate. Context and
external queues outlive all owner joins; native stream retirement precedes
binding retirement. Budget complete context objects independently from the
rest of the controller envelope. No new dependency, driver or schema is needed.

The source compiles against the Windows SDK and prepared tests pass Release,
ASan/UBSan and TSan, including actual bind/end races and composed fake capture.
Synthetic prior fits and exact synthetic 64 kHz clocks are explicitly fixture
inputs, not startup qualification. Do not activate the duplex factory until
native render drain, durable original SDK-end transaction state, admitted live
clock startup, SDK failure/retirement and installed desktop workflows pass.

[Checkpoint196](../196-native-monitor-capture-context.md) records exact source,
retained evidence and unresolved platform/product gates.
