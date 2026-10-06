# ADR-043: Anchor native punch verification independently of capture origins

Date: 2026-10-06. Status: accepted fixture design; full product parity open.

A punch take's first origin is its first captured sample, which can occur after
preroll and differs with declared input latency. A native monitor verifier must
therefore retain a separate first advancing playback cycle rather than reuse the
aggregate capture origin as playback start.

Use existing fixture callback instrumentation with fixed per-lane clock/offset
storage and immutable release/acquire playback-anchor publication. Simulate
independent channel delays, verify exact timeline-coordinate raw samples, verify
full-range output, and inspect journals/alignment/grouped undo/Save-reopen after
native and disk join. Keep this in a separate opt-in target, preserving previous
native default fixtures and production processing unchanged.

Admit no native timing qualification without complete elapsed/CPU/thread-resource
and current-cycle coverage, unchanged finite thresholds and no cycle overrun.
Retain original execution artifacts and supervise exact owned process handles.
Synthetic execution qualifies the oracle; native execution qualifies only its
actual owned routes and workload. Neither establishes measured physical latency,
Windows behavior, sustained performance or full reference punch modes.
