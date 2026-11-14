# ADR 176: Keep comp span input and output sample clocks explicit

Date: 2026-10-14. Status: accepted for direct DSP; owned regeneration remains gated.

## Decision

A prepared uniform comp span records the original raw asset, its exact processing
grid, the current artifact's output sample rate, and a rational backend clock
factor. A regenerated span uses the original nominal processing settings rather
than looking for a stretch anchor on the current assembled clip.

Let `P` be the original nominal pitch multiplier, `B` the product of varispeed
already baked into previous project-domain assemblies, `Rin` the raw sample rate,
`Rout` the current artifact sample rate, and `G` the raw-frame processing-grid
step. The raw-rate backend receives pitch factor:

`P * B * Rin / Rout / G`

Output interpreted at `Rout` then has the intended physical frequency `f * P * B`.
The current clip's unbaked playback rate remains a separate playback/assembly
operation. Time scaling continues to use the exact prepared output-frame count
divided by processing-frame count. No padding, truncation or output-duration
repair is added.

The direct-copy path requires an exact unit backend pitch factor and equal input
and output frame counts. Rational equality is used for the clock/grid terms.
For legacy equal-clock spans, the original floating-point arithmetic order is
preserved; multiplying by a redundant clock factor is avoided.

## Boundary

Preparation and callback-based rendering operate on metadata and caller-supplied
PCM. They confer no filesystem or model-adoption authority. The owned span/pair
decoder still rejects regenerated recipes, including mixed pairs with an unchanged
assembled side. The current protocol and worker remain version one.

Next integration must bind explicit clocks in a versioned request/intent, write
and verify WAVs at the declared output rate, review historical owned files around
production, and carry the complete dependency guard through queued adoption.
Direct tone experiments establish pitch/duration observations only; they do not
qualify repeated owned-job rendering, formants, transients or overall sound quality.
