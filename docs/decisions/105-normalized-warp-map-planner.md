# ADR 105: Original bounded rational warp-map planner

2026-10-10 UTC. Status: experimental Linux acceptance; native Windows gate pending.
Frozen baseline and full F/Q/C/N requirements are unchanged.

Keep exact geometry independent of GUI, media decoding and the stretch processor.
An original C++20 `WarpMap` owns normalized monotone interior points with stable
IDs and implicit endpoints. Physical source-grid offsets, raw asset origin,
physical rate, derived output and visible source interval are separate fields.
Project/tempo domains must enter through explicit adapters, not reinterpretation
of those offsets. Forward/inverse maps are exact, reduced and closed at endpoints.

Use the existing portable source-coordinate helpers, with checked rational
addition/subtraction and reduced uint64 segment ratios. Preserve canonical
identity directly. Refuse arithmetic outside the admitted representation instead
of introducing floating timing. This conservative experimental representation
does not establish support for every mathematically valid huge rational map.
Future source-rate/project/tempo adapters and production project schema must retain
exact original anchors and expose unsupported conversion explicitly.

Reserve payload before owned storage, bound marker count/work/scratch through a
trusted policy and return credit after failure/destruction. Constructor, sorting,
vendor-map allocation and final retirement are control-side operations. The
planner is not part of a realtime callback and no callback safety is inferred.
Its CMake probe is opt-in/defaultOFF and has no install rule or application hook.

An integer-only vendor adapter excludes both implicit endpoints and refuses
fractional markers. It never rounds the exact model to make a processor accept.
It is a candidate adapter, not a processing-quality contract or a new shipping
algorithm identity. Overall ratio remains separate from the interior map.
The pinned R3/R2 comparison runs actual multiple-anchor/nonuniform cases and
retains full finite PCM, warnings, drain and input-partition effects.

The observed partition dependence means a future processor identity/key must
bind a canonical chunk policy. Different vendor input chunks need not be silently
treated as the same algorithm. Shared live/offline behavior continues to consume
one independently verified prepared artifact. No shipping defaults, schema1.14,
protocol4 or legacy derivative identity change in this increment.

Local planner arithmetic acceptance does not satisfy the audible event/group
gates in [ADR104](104-local-warp-and-group-quality.md). The exploratory detector
flags measurement-window censoring; resolved output measurements still require
separate onset/pre-echo/listening and group phase acceptance. Do not promote from
exact drain, stable IDs or a named synchronization option. Next evaluate a
source-pinned alternative or original anchored strategy before exposing supported
marker processing in the app. Keep normalized geometry and all raw/media/Undo/
reopen/crop/split/recovery obligations when adopting that adapter.

No new dependency/source asset/driver is adopted. Original planner/probes use
repository GPL-3.0-only, existing session/resource/math/JSON code and the already
licensed pinned worker library. Native Windows oracle qualification and processing
quality remain independent gates. See [checkpoint146](../146-normalized-warp-map-planner.md).
