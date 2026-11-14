# ADR 157: explicit processing region for a generated comp join

Status: proposal under owned processing experiments; existing model/media
verification remains unchanged.

The strict preserved-center recipe produces large joins when newly rendered R3
context meets an older artifact. Original raw/nominal assets must remain immutable
and recoverable, but that requirement is distinct from forbidding processing of
every derivative sample inside a crossfade that the operator has requested.

Evaluate a separate explicit join blend. A trusted control plan must name the
permitted half-open physical region and the blend width. Only an inner prefix or
suffix contained in that region may mix current PCM with the context render.
The old raw/nominal files remain unchanged, and current samples outside the two
declared blend intervals must match bit for bit. One shared complementary curve
applies to all channels; no channel-specific phase fit or silent normalization.

This proposal does not authorize automatic editing outside the requested project
transition. Production preparation must derive physical permissions from the
exact clip map and account for resampler neighborhoods at other rates, fractional
playback origins and linked speeds. Job identities, portable state, independent
review, atomic Undo and recovery must retain that processing region and provenance.

Before changing adoption, test whole PCM and source hashes, exact permitted scope,
partition independence, invalid/overlapping region refusal, linked-channel behavior,
actual boundary behavior, transient/pitch/phase artifacts and blinded listening.
A smaller boundary discontinuity alone is not a professional quality result.
No new schema or relaxation of the unity verifier is part of this experiment.
