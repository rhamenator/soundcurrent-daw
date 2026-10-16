# ADR 188: Own protected comp geometry before worker integration

2026-10-15 UTC. Status: core accepted for the demonstrated integer-clock subset;
worker/publication/UI and independent acoustic qualification pending.

Use a move-only plan that owns its source declaration, original exact raw map and
rebased semantic protected geometry. Do not keep borrowed pointers into a session
or a temporary comp envelope. Reserve payload before metadata/map allocation,
retain the reservation with the owner and release storage before credit.

Verify equal interpolation at both maps' knot union. Preserve anchor identities
and protected core/halo policy; do not infer semantic protection solely from a
unity-slope interval. Use an absolute raw-frame callback with explicit origin
translation. Poison read buffers and reject incomplete/nonfinite source samples.
Keep exact duration, bounded callback blocks, cancellation and float headroom.
All preparation/rendering/destruction is off the realtime callback.

Assign a distinct protected-comp processor identity. A future owned protocol must
bind the complete geometry, provenance and build identity before publication.
The uniform comp request and worker keep their current refusal until that adapter
is implemented. Callback failure after partial output cannot constitute success.
Third-party allocations still require the containing worker's memory ceiling;
payload leases alone are not an RSS/process-limit guarantee.

The first adapter refuses unsupported fractional acoustic positions, changed
clocks, previous handle artifacts and history regeneration. These paths remain
product requirements; the refusal is not a permanent scope reduction. The
successful core tests do not qualify gaps/joins/real-music/group-phase quality.

No new dependency or license is introduced. The existing GPL implementation and
pinned protected renderer are reused unchanged. See [checkpoint 300](../300-owned-protected-comp-spans.md)
for exact source, retained PCM, failures/lifetime coverage and next integration.
