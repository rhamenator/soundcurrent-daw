# ADR 158: immutable transition permissions and post-SRC processing

Status: accepted control-side planning; renderer/state/adoption integration open.

Derive processing permissions from the actual transition and immutable source
envelopes. Map its project window exactly into artifact coordinates and include
the full positioned FIR footprint of adjacent outside-window audible samples.
Use physical processing only when the requested inner width and newly generated
edges cannot leak into those output samples.

Otherwise retain the width in project frames and require blending after the two
streams have been independently resampled. Do not replace the general path with
a narrower physical-only feature or shrink the width to make a scope check pass.
Original sources stay immutable and the operator's project window bounds changes.

One-frame intervals have an explicit half-weight blend. This defines behavior,
not sonic quality. Persisted recipe identities, independent media/PCM review,
portable state, shared live/offline processing and native/quality gates must be
implemented before these plans can authorize a model edit.
