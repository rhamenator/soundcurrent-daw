# ADR070: lease prepared execution payloads from the project parent

Status: accepted for the bounded X006 foundation checkpoint, 2026-10-07.

## Decision

Use the existing off-RT ResourceLedger through trusted execution options. Reserve
one aggregate lease per prepared playback graph, separate reader buffer/binding
leases and one lease per shared media cache. Suppress nested EQ/graph/reader leases
where their aggregate owner already pays. Use the same project parent for live
execution and offline export; explicitly supplied child domains remain possible.
Declare leases before payload members and retire them only after control-side
quiescence/destruction. Reuse existing PipeWire/JACK infrastructure and keep the
engine independent of Qt/backend objects.

## Reasons and consequences

Independent local ceilings allowed multiple generations, caches and exports to
fit individually while their combined declared payload exceeded project policy.
Shared ownership makes overlap visible and refusal actionable without inventing
a track-count ceiling. The approach preserves existing local policies and standalone
callers, introduces no dependency or project schema change, and permits headroom
and rendering behavior to remain unchanged.

The current conservative weights do not measure exact allocations/RSS or CPU
capacity. Validation/trial metadata, capture/writer/other IO pools and Qt overhead
remain separate work. Full budget coordination and sustained native Windows/Linux
qualification are still required. See [the scope and acceptance contract](../85-graph-memory-resources.md).
