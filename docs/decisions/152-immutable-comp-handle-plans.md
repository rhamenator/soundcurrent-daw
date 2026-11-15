# ADR 152: immutable comp handle plans with revision binding

Status: accepted for metadata planning; generation and admission remain open.

## Context

A retained comp fragment can require samples outside its existing processed
artifact. Ordinary stretch replacement preserves a nominal raw anchor and must
not be used to silently reinterpret that anchor. An asynchronous operation also
needs to reject stale project, workspace, take and media declarations.

## Decision

Prepare geometry in the framework-independent control-side engine. Store the
result in a const, move-owned envelope with resource credit that outlives its
snapshots and map. Disable copy and move assignment. Pair preparation owns both
results and retires the first if the second fails.

Bind plans to a control-owner project revision/workspace identity and exact
expected clips, takes, assets, rate and layout. Identity is supplied by the
controller; the planner does not resolve paths or verify PCM. Share the shipping
warp interval-ratio arithmetic rather than maintaining a second implementation.
Validate project state before pair end-coordinate arithmetic. Bound output frames,
estimated bytes, map points and identity length before allocating the result.

## Consequences

The prepared data cannot be changed through the API after planning. Metadata
coverage, immutable ownership and resource cleanup can be tested independently
of rendering. Caller revision/root integration remains required when the UI and
worker use the planner.

The next state protocol must distinguish an immutable nominal stretch anchor
from an extended processed window. This decision grants no exception to existing
stretch adoption validation. Generation, worker artifact review, atomic paired
admission, project migrations and PCM/edge quality are separate required work.
There is no new dependency, callback operation or proprietary algorithm.
