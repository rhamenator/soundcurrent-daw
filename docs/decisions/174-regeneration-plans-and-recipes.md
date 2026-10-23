# ADR 174 — preserve original history in repeated-rendering plans

Status: accepted, 2026-10-14. Producer integration remains required.

## Decision

Handle planning for an adopted assembly owns the immutable source resolver from
[ADR 173](173-comp-regeneration-source-history.md). Its current PCM baseline is
the adopted artifact; its raw map and nominal producer come from the original
source history. Exact map extrapolation extends the requested handles within
the retained take's permitted raw interval. No assembled file is retagged as a
raw recording. The complete history remains attached to stale checks and joined
resource accounting for the lifetime of the plan.

`nominalAsset` retains its existing immediate comp-window meaning. For layered
assemblies it remains empty; the original nominal producer is accessed through
the resolved history. This preserves the canonical layered playback schema.

Recipes containing any assembled source use `sc-comp-processing-recipe-v2`.
Their source selection includes all paired ancestors, assets, trusted selection
references and historical retained takes. Collections are deduplicated and
sorted by stable identifiers. Each regenerated fragment explicitly binds its
original nominal identity and exact baked-pitch and backend-clock ratios.
Decoding reconstructs the plan and compares the complete canonical document.

First-generation recipes continue to use v1 with their original fields and
canonical bytes. A v2 label on a first-generation recipe, or a v1 label on a
regenerated recipe, fails recomputation. Runtime root paths and opening epochs
are supplied by the current owner rather than serialized into recipe identity.

## Boundary and alternatives

Copying only the current clip and raw asset would omit the paired assembly and
retained-source dependencies needed to validate its timing. Copying the entire
session would include unrelated state and make recipe identity depend on edits
outside the selected work. The bounded dependency closure provides the required
history while allowing unrelated edits.

Preparing a plan or saving recipe metadata does not authorize production. The
span producer explicitly rejects regenerated envelopes until its output-clock,
pitch and historical owned-file verification paths are implemented. Existing
requests remain readable; new producer semantics will be bound explicitly in
their own versioned protocol. Repeated real rendering, adoption and export still
require independent acceptance evidence.

The paired-job factory and request decoder also reject any pair containing an
assembled source, including an unchanged assembled side paired with a new
first-generation render. Checking only the side sent to the worker would leave
that mixed-source path without historical verification.
