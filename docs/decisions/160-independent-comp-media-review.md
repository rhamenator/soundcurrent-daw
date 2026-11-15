# ADR 160: separate actual media scope review from adoption authority

Status: accepted disk-side integrity/scope review; completion/producer/adoption open.

Independently check actual raw/current/nominal/retained media, declared companion
byte bindings and both candidate output files against the immutable recipe.
Physical results preserve decoded current samples outside actual blend intervals;
both domains preserve original project PCM outside the transition with actual
positioned SRC. Keep bounded approved-object readers, ledger credit and cooperative
cancellation/deadline work off audio and GUI threads.

Return a separate move-only review object with digest/context/output identity and
comparison counts. Scope and hash correctness alone cannot prove curve conformance,
render-job ownership, producer alignment or sound quality. Do not relax the old
unity verifier or expose a general adoption token until these additional gates and
post-SRC state/execution representation are implemented. Original files, anchors,
paired Undo and recovery remain required.

Retain actual fixture files and immutable input-bank lineage. Deduplicate public
archive payloads through an audited hash manifest while preserving every original
byte. Keep the original full archive and earlier failures locally; a packaging
change must not be presented as another DSP execution or processing-quality proof.
