# ADR 155: exact raw intervals on a separate processing grid

Status: accepted timing primitive; comp producer and processing quality open.

Represent a processing grid by its raw origin, exact positive raw interval and
integer processing frame count. Its exact constant step feeds the existing FIR
through `SourceFrameMap`; no clip playback setting or saved nominal anchor changes.
Keep the existing effective rate bounds and explicitly refuse an unrepresentable
step or endpoint. This adds no dependency or project schema.

The integer backend frame count represents samples of this prepared grid, not a
rounded raw duration. Select the output count independently and compensate the
input resampling pitch factor. Full quality, formants and piecewise landmark
alignment need separate evidence before a production render can be adopted.

The actual full rerender experiment changes every nominal sample in its retained
case. General edge generation must therefore preserve old inner artifacts and
qualify joins; the grid primitive alone cannot meet that admission requirement.
Do not replace the preservation check with a metadata-only map check or loosen a
professional quality target to the experiment's feasibility tolerance.
