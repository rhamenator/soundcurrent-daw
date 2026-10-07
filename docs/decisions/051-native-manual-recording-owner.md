# ADR 051: explicit native manual recording control ownership

Status: accepted, 2026-10-07 UTC. GPL-3.0-only. No new dependency.

## Decision

Adapt `ManualRecordingRun` through the existing PipeWire filter and require one
serialized off-audio/off-GUI control owner. Keep native callbacks limited to raw
capture and continuous mix processing. Preparation admits deferred pools, while
service binds consumers from actual published starts. Use explicit inactive input
and output routes; do not change defaults, rate or quantum. Join native callbacks
before engine finishing, disk joins, group verification and object retirement.

Do not expose mutable engine ownership to the GUI. A future Qt controller must
provide bounded messages/progress/replies and own its service worker. Atomic
observer reads remain separate from the serialized producer/control methods.

## Consequences

Repeated take adoption can update canonical state while the prepared playback
generation remains unchanged. Existing reply/result backpressure and failure
policies remain available. Finite owned-route acceptance demonstrates exact complete
and late-serviced take workflows; native fault/recovery, desktop, independent
Windows and sustained/physical gates remain required. See [contract](../65-native-manual-recording.md).
