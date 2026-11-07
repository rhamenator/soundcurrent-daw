# ADR 153: separate nominal anchors from processed handle windows

Status: accepted for project state; generation and admission remain open.

## Decision

Persist a separate `ClipCompHandleWindow` with nominal asset identity, physical
prefix/suffix lengths and extended-artifact key. Retain the nominal stretch/raw
anchor unchanged. Emit schema 1.20 only for projects using this state, preserving
ordinary 1.18/1.19 encodings and decoding older formats without new metadata.

Reconstruct exact raw-coordinate maps from the nominal context/warp state plus
explicit window offsets. Validate rates/layout, nominal identity, retained take
coverage and actual sample bounds. Treat fractional exclusive endpoints separately
from actual samples. Include the new payload in snapshot and Undo resource budgets.

## Consequences

Split, paired edits, further preparation and project persistence retain provenance.
File verification, actual rendering, atomic source admission and sonic/PCM quality
are separate gates. Ordinary stretch replacement refuses these windows until the
paired regeneration path is implemented; the existing raw-anchor guard is retained.
This is a staged limitation, not an exclusion from the full DAW target.
