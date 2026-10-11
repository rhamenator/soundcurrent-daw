# ADR110: inactive retained lanes and explicit comp copies

Status: accepted for the first retained-take implementation; full loop/comp parity
remains open. See [checkpoint170](../170-retained-take-lanes.md).

Store retained takes as inactive lane clips with group/capture-range provenance.
Keep the established arrangement clips as the audible playlist. Select a comp
region by an atomic control-side crop/copy transaction, retaining raw assets and
stable source references. This shares existing playback/offline rendering and
undo/redo without adding lane enumeration, allocations or locks to callbacks.

Use one versioned clip codec for both lists and include nested lane/warp storage
in the existing resource budgets. Keep source track/lane/clip identities explicit
so cross-track moves do not discard provenance. Refuse uncovered ranges and
source deletion that leaves active references. Repeated identical selections
preserve IDs and history. Retained source edits are not implicitly propagated
into already selected arrangement copies; any later linked-update workflow needs
its own explicit operation and acceptance evidence.

Continuous recording transport and native device clocks remain distinct from
future looped project time. Retaining repeated manual punches provides a useful
first handoff, but does not implement wrap scheduling or loop playback. Follow
with desktop take controls, then bounded real-time loop transitions and independent
native-platform qualification. No dependency or license change is required.
