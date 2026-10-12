# ADR138: explicit prepared monitor channel matrix

Status: accepted for bounded non-startup processing; native factory disabled.
Date:2026-10-11.

## Decision

Keep raw capture and causal conversion at the original input width. Map only the
monitor block after conversion, with an immutable sparse matrix using the
existing GPL ChannelMix type. Preserve the absent-map identity path. Provide an
explicit unity mono-to-stereo helper; infer neither a pan law nor speaker slots.

Reserve matrix and scratch storage before activation. Accumulate in double,
validate the whole result against finite float range, then publish once. Preserve
mapped bytes across output pressure. No allocations, locks, logging or disk work
occur in processing. Whole-block refusal preserves raw data and first fault.

Native auxiliary1.1 cannot declare this matrix. Refuse explicit startup mapping
until versioned admission, persistence/migration and composed native tests exist.
Do not change old readiness-channel meaning or relax its validation.

## Alternatives and consequences

Instantiating PreparedMixGraph would add unrelated per-track EQ/event/planar
state. Guessing a channel map or duplicating the raw recording would hide intent
or change the take. Mapping before conversion widens converter/storage work for
mono duplication; a constant linear matrix belongs after conversion here.

The bounded matrix may still exceed a small hardware buffer deadline at maximum
route count. Native factory admission must qualify actual geometry/work budgets.
Live editing requires a later graph/state replacement protocol; this matrix is
immutable for its prepared lifetime. No new third-party license is introduced.
