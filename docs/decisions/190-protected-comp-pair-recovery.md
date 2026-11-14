# ADR 190: Explicit protected pair policy and deterministic recovery

2026-10-15 UTC. Status: accepted for demonstrated paired selection/recovery;
protected paired assembly, canonical adoption and desktop entry remain pending.

Keep UniformOnly as the default. Add an explicit ProtectedOriginal policy with
a distinct saved format. Select each producer from the original nominal clip's
semantic protection, never from a slope heuristic, DSP failure or filename.
Protected history and unsupported timing must fail through the protected adapter;
they must not silently fall back to uniform processing.

Bind the policy in the immutable manifest format and preserve child request hashes.
Reconstruct the entire selection and exact request bytes from the trusted recipe
when opening a saved project. A caller-provided digest alone does not establish
semantic validity. Old uniform manifests continue to rederive through their
original policy and serialization.

Reuse the existing publication, source-verification, ownership and recovery
machinery. Readiness requires both verified child outputs or genuinely unneeded
existing-media sides. It supplies no authority for model adoption. Stale, partial,
active, canceled and invalid work must remain inspectable without being playable.
A late cancel marker cannot invalidate an already verified completed result.

Keep the desktop's default policy unchanged until paired publication, media
provenance, Review/Apply and history are qualified. Mixed uniform/protected and
existing-media pairs need explicit acceptance before that entry is enabled.
The first recovery bank includes actual cancel/wait retirement and a labeled
synthetic interrupted-after-start state, not crash-during-render qualification.

No dependency or license change. All work is serialized control/disk processing,
not an audio callback. See [checkpoint 302](../302-protected-comp-pair-recovery.md)
for retained local evidence, original CI and its detailed protected-core retention
gap. Independent processing quality and the full product scope remain open.
