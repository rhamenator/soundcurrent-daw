# ADR106: Explicit transient-protected maps after candidate timing evidence

2026-10-10 UTC. Status: next original implementation selected; not shipping yet.
Full frozen-reference F/Q/C/N and ADR104 tolerances remain unchanged.

The preregistered anchored alternative scheduler in checkpoint147 preserves exact
map geometry and latency bookkeeping. Its impulse diagnostics pass, but uniform/
nonuniform attack diagnostics exceed the <1ms gate, and cancellation/phase/group
observations do not qualify a phase-preserving recording mode. Do not adopt this
configuration or weaken the gate based on finite exact duration.

Next implement an original control-side **transient-protected map mode**. User
anchors retain exact raw coordinates and desired output coordinates. Explicit
protected source/output spans around an anchor have unity local slope, preserving
the attack samples and physical channel offsets there. Their boundaries become
normalized derived map points with recorded provenance and stable semantic state.
Stretch the remaining actual-source intervals through the already owned worker.
This is a different, named piecewise map/edge/algorithm policy; never present it
as the identical audio resulting from an unmodified linear-between-markers map.

Before rendering, define protected span sizes, boundary/crossfade/source-context
policy, interval admission and deterministic processor/chunk identity. Refuse
reversed/overlapping/unrepresentable spans explicitly. Preserve exact raw bytes
and markers. Do not detect fixture names or copy known output peaks as a timing
repair. First test user-marked percussion/attack cases, then general transient
analysis and difficult short/edge/group cases. General warp, arbitrary material
and pitch/formant quality remain in scope, with unsupported cases explicit.

Preregister onset/pre-echo/cancellation/group-offset/phase gates and retain full
source/generated/output PCM, failures and processing metadata. Protecting an
attack does not qualify phase between attacks or arbitrary microphone material.
Only demonstrated supported acoustic cases advance to one editable marker,
supervised review/Apply, Undo/Redo, save/reopen, split/crop/flatten/recovery and
shared playback/export. Continue broader M2/M8 and all other milestones afterward.

No new shipping dependency is selected. The MIT candidate is retained only as
licensed reproducible experiment evidence; its exact source pins/notices and
Linux-only scope remain explicit. Existing shipping Rubber Band identity,
schema1.14/protocol4/default processing and equalizer trees remain unchanged.
