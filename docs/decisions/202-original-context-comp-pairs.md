# ADR 202: Bind original-context rendering to an explicit paired selection

Status: implemented; qualification in progress.
Date: 2026-10-15.

## Decision

Add `CompPairSpanPolicy::OriginalContext`, persisted as
`sc-comp-pair-selection-original-context-v1`. Uniform children use the explicit
original-context worker. Protected children use the clocked original-geometry
worker. Existing-media sides remain retained inputs and need no child process.
Loading reconstructs the exact requests and checks their recorded digests and
immutable bytes. Older formats continue to select their original producers.

Application preparation chooses this policy when a pair has regeneration history
and at least one uniform side needs processing. This includes a retained assembled
take paired with an original uniform clip. The selection happens before execution,
without probing a failed worker or guessing a fixed backend window threshold.
Same-clock requests without history retain their existing selection rules.

## Rationale and limits

A short crop should retain its original analysis domain and baked pitch history.
Its suitability must not depend on an arbitrary minimum-frame constant in GUI
code. An explicit saved format makes the change auditable and prevents old
selections from acquiring different audio when reopened or retried.

This changes the producer for newly prepared uniform edits with history. Their
PCM may differ from an older crop-local render. Adoption still requires review
and explicit Apply; opening a project does not rerender existing audio. Work is
off the audio thread and continues to use the owned worker's resource limits.

The paired and desktop acceptance workflows must cover short fractional crops,
mixed retained takes, rederived selection bindings, Undo/Redo, save/reopen,
unchanged regions, live/export agreement and failure cleanup. Native Windows and
installed preview qualification remain separate gates. A genuinely short whole
original context remains unsupported by this producer.
