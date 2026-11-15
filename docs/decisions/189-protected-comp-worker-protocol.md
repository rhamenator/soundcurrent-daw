# ADR 189: Bind protected comp geometry in a distinct owned protocol

2026-10-15 UTC. Status: accepted for isolated worker use in the qualified integer,
same-clock subset; paired recovery, model adoption and desktop integration pending.

Own uniform and protected span plans as a variant. Require an explicit protected
encoder and distinct protocol/processor identities. Preserve the existing v1/v2
serialization and DSP paths; the legacy encoder continues to refuse protected
geometry. Select the common source/output interface only after typed preparation.

Derive canonical geometry from the authenticated recipe. Bind raw source identity,
rate/layout, exact origin/extent/map, semantic anchor identities and spans, halo/
protection policy, chunk size and algorithm IDs. Charge staging before construction
and cap serialization. Compare typed bindings when decoding; rederive them for
ready and completion verification. Request, recipe and executable hashes remain
independently checked. Do not trust a parent-supplied geometry digest by itself.

Reuse the isolated worker's process ceiling, watchdog, pre/post source admission,
ownership lease and atomic publication. A ready packet cannot serve as completion;
failed/canceled/source-mutated jobs cannot publish a verified candidate. Preserve
floating headroom and exact output duration. Work runs outside realtime callbacks.

Add paired selection/recovery through deliberate policy and acceptance tests.
Do not enable the controller merely by removing its uniform-geometry refusal.
Fractional clocks, prior artifacts and history remain required follow-up work.
Matching the core implementation is a regression oracle, not independent acoustic
qualification; ADR 104 continues to apply.

No dependency or license change. See [checkpoint 301](../301-protected-comp-worker.md)
for source identity, full retained evidence and remaining gates.
