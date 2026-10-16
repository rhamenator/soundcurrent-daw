# ADR 193: Bind protected regeneration to original semantic history

2026-10-15. Status: implemented; local qualification recorded in checkpoint 305.
Native and repeated desktop qualification remain separate.

Use original raw/nominal provenance to reconstruct protected regions in repeated
comp renders. Invert the admitted current output-to-raw map at each original
semantic anchor, preserving its identity and protection policy. Require exact
agreement at the union of old and reconstructed map knots. Keep owned geometry
and reservations independent of the borrowed envelope's lifetime.

Introduce protected span protocol/processor v2 when a paired recipe requires
history, with explicit clock, pitch, original-clip and regeneration bindings.
Retain original protected v1 and uniform v1/v2 encoding. Reconstruct bindings on
read; never accept a protocol downgrade that drops historical verification.
Reuse original source/history admission before render and before completion,
publication and Apply. Do not bypass existing paired or nominal receipt checks.

Admit complete protected windows with integer raw geometry, unchanged sample
clocks and unchanged baked pitch. Explicit refusal is temporary for cases needing
fractional, changed-clock, pitch or cropped-window adaptation. Do not silently
round or render those inputs as uniform audio.

Distinguish unchanged source PCM from unchanged audible output. Replacing an
existing fade affects both the old envelope and the new envelope. Compare audible
output outside their derived union and the separately recorded processing-edge
guard; keep the existing source blend/scope checks unchanged. Preserve the failed
initial assertion and correction in evidence.

No dependency or license change. Processing quality, current native runtime,
repeated protected desktop interaction and installed delivery remain required.
