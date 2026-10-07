# ADR 056: independent priority signal for manual recording

Status: accepted scoped foundation, 2026-10-07 UTC.

Retain a separately owned, monotonic, lock-free Stop/Cancel signal per generation.
GUI/control producers do not access serialized endpoint/route/disk/pool methods.
Audio observes the signal at a callback boundary independently of control disk
work. Refuse further preparation/submission while preserving reliable replies
for already accepted commands. Join native callbacks before draining resources.

Check interruption between lane operations; do not forcibly terminate a constructor
or verifier already executing. Sample Cancel at finalization entry and preserve
original files. Late cancellation/adoption ordering requires controller policy.

Native tests hold real disk startup through callback completion, use an atomic-only
producer and retain full raw/recovery/output oracles. Join the observer after the
entire produced prefix is copied and before recorder deactivation. Preserve failed
originals and missing terms. Later passing runs cannot supply their diagnosis.

Qt manual UI, arbitrary track scaling, native Windows and sustained qualification
remain required. See [contract/evidence](../70-manual-priority-interruption.md).
