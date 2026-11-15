# ADR 194: Acquire readiness before dependent observations

2026-10-15. Status: implemented; scoped Linux qualification in checkpoint 306.

A producer's readiness publication orders its preceding PCM/counter/EOF writes.
The consumer must acquire that publication before reading dependent bank counters.
A counter read earlier in the callback cannot validate a later ready receipt.

Independent time supplied by the caller is also a snapshot. Another owner may
publish after that snapshot and before the consumer reads readiness. Validate the
receipt's structure and ownership immediately, but retain silence/warmup when its
timestamp is newer than the caller's sample. Admit it only with a fresh clock.
Keep existing warmup frame/time limits, terminal checks and finite extent checks.
Preserve original readiness as diagnostic evidence even when it was not admitted.
Do not resample system time inside this framework-independent callback, substitute
a device clock, mutate the receipt or consume any deferred PCM.

Deterministic tests reproduce the caller-clock interleaving in both consumers.
Counter ordering also follows the C++ release/acquire contract; no claim is made
that the original uninstrumented CI failure's exact predicate was recovered.
Retain failed tests and explicit test-contract corrections with final evidence.

CI continues to enforce all acceptance and archive checks. Update placement weights
from original run observations and add a third Linux shard, with complete/disjoint
coverage and unchanged per-test and per-job deadlines. Scheduling estimates are
not performance qualification. Keep the required aggregate dependent on every shard.

No new dependency/license or persisted-state/UI change. Native Windows runtime,
installed playback, processing quality and the full product scope remain open.
