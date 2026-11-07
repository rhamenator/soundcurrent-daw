# ADR 173 — resolve source history before repeated comp rendering

Status: accepted, 2026-10-14. Producer integration remains required.

## Decision

Repeated comp rendering must resolve the original raw source, retained take and
nominal producer through the saved assembly history. An assembled artifact can
be the current PCM baseline whose untouched regions must be preserved. It must
never become the raw recording or nominal stretch source merely because a clip
now references it.

`resolveCompRegenerationSource` validates canonical metadata and captures a
bounded, immutable, joined-ledger snapshot. The closure includes both sides of
every encountered assembly, ancestors, original assets, trusted selection
references and historical retained-take origins. Traversal is iterative and
deduplicates identities. Stale checks reject owner/root/epoch, clip, layout,
asset, history, parent-reference or retained-take changes. An unrelated project
title edit does not invalidate the source.

The normalized map converts current artifact-frame coordinates to original raw
coordinates using exact fractional arithmetic. Crops and differing clocks are
not rounded to integer sample positions. Piecewise maps remain piecewise; this
resolution does not promise a nonuniform producer.

## Pitch and clock domains

Each historical `ProjectAfterSrc` stage has baked its original clip's playback
speed into PCM. Physical-artifact stages leave that speed for subsequent playback.
The resolver accumulates only the already baked speed as an exact positive
rational value. Current clip playback speed stays separate.

It also exposes that value multiplied by `raw sample rate / current artifact
sample rate`. A future raw-rate producer must combine this clock correction with
the original nominal pitch setting and the inverse explicit processing-grid step.
Simply reusing the first-generation pitch setting would shift pitch again after
an earlier post-SRC adoption. Neither this decomposition nor source-map equality
establishes DSP quality; the producer must be independently tested.

## Boundary

Resolution performs no file I/O, verifies no completion marker and grants no
adoption authority. Its consumers must verify the actual raw/nominal/current
media and original owned assembly jobs before and after producing new audio.
Producer protocols need explicit output-clock/pitch binding and compatible
recovery of older requests. Existing renderer refusals remain in place until
those paths are implemented. This decision does not reduce the requirement for
usable repeated regeneration, native parity or processing-quality qualification.
