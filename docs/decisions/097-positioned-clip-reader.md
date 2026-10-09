# ADR097: exact positioned clip reading

Date: 2026-10-09. Selected for fixed physical sample-rate conversion of owned
clips. Full rate automation, pitch/stretch and frozen quality parity remain open.

| Choice | Evidence and integration cost | Decision |
|---|---|---|
| Restart a streaming converter at the rounded source frame | Discards fractional phase and preceding context; split/seek samples change | Refused |
| Replay every source prefix after seek | Correctness could be demonstrated, but preparation grows with source position | Refused |
| Copy upstream private ring/state structures | Version-sensitive private ABI, restoration and resource obligations | Refused |
| Adapt pinned BSD best-sinc recurrence to absolute rational positions | Same unmodified coefficient table; bounded random source windows; a distinct algorithm/version and actual shared-reader tests are required | Selected |

The original streaming adapter remains separately identified and tested. The
positioned adapter has no history or mutable phase. It samples the exact source
coordinate for every project frame. Source context is fetched from the complete
owned asset, including samples outside a cropped clip; only asset boundaries are
zero extended. Thus splitting/cropping does not introduce new filter boundaries.
Clip fades operate in project frames after conversion and before overlap mixing.

Keep cache indices/decoding in physical source frames. Charge the maximum required
source window and one converted slab per track, plus binding metadata and existing
mix buffers. Reserve before allocation or admission reads. Disk access, kernel
evaluation, validation/exceptions and retirement stay on the worker. Native audio
callbacks consume existing bounded slabs; they do not call this API.

Schema1.10 stores unsigned rational source fractions and the stable positioned
algorithm ID. Neutral schema1.0–1.9 clips migrate explicitly. Old mixed-rate clips
are refused because their former length domain is ambiguous. Future/unknown
algorithm IDs and non-representable common denominators are refused. Never coerce
fractional positions to float project state. The unsigned64 denominator bound is
an explicit representation limit, not arbitrary-precision project support.

Integer source range editing preserves existing fractions and refuses off-grid
source deltas. The project crop command supports signed project offsets and
shifts fade anchors in the same domain. Grouped edits and Undo/Redo retain strong
atomic validation. Desktop insertion computes a ceiling project duration for an
owned asset; it does not certify foreign project placement or effect conversion.

[Checkpoint131](../131-positioned-clip-playback.md) records acceptance evidence,
license/provenance, platform qualifications and unresolved frozen workflows.
