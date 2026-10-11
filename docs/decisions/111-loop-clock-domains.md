# ADR111: keep stream/device clocks monotonic during project loops

Status: accepted for the finite audio-loop foundation; full transport pending.

## Decision

Use an immutable, framework-independent `PlaybackTimeline` to map monotonic
stream frames to a wrapped project region. Split source reads on the existing
disk owner. Keep queued slab timestamps, prepared graph state, parameter events,
recording origins and device clocks monotonic. Expose both positions in reports.
Live playback and explicit finite loop export use the same mapping and reader.

## Alternatives and consequences

Resetting the graph/pipe at a wrap would reset DSP and complicate ownership,
latency postroll, accepted events and capture clocks. Duplicating project clips
for each pass would grow session state and bind temporary performance duration
to media inventory. The selected mapping avoids both costs and has bounded
scalar segment queries, including one-frame loops.

The disk owner may revisit cached source pages; worst-case short-loop performance
still needs measured load qualification. Authored discontinuities repeat without
automatic crossfades. Project-timed automation, plugin musical-position reporting,
MIDI loops, indefinite native transport and desktop persistence are separate work.
No dependency/license change. See [checkpoint172](../172-loop-playback-recording.md).
