# ADR 048: one-shot audio publication of a prepared capture start

Date: 2026-10-06. Status: accepted prerequisite for runtime manual punch.

## Decision

Allow explicit deferred-start capture preparation with a zero placeholder frame.
Keep `CapturePipe::config()` immutable. Audio publishes a separate exact start
once through a release/acquire flag, then produces normal timestamped slabs.
`recordingConfig()` returns a resolved value snapshot, or absence before start.
It never reads the mutable producer cursor. Origin publication is separately
required before the first captured sample. Starting does not reset queues,
reallocate buffers, create a job, change processor state or relax fault handling.

The disk owner must bind its normal recording specification to the published
resolved snapshot. Unresolved specs and stale/guessed start values fail before job
creation. The existing journal schema records the actual start and needs no new
runtime field. Fixed-start owners refuse deferred bindings until a runtime command
owner implements reliable acknowledgement and safe take-slot retirement.

## Alternatives and consequences

Changing the shared preparation config races worker reads. Guessing a future
start rounds or delays a manual command and can miss the deadline. Opening every
possible take job during preparation performs unwanted disk work and records
fabricated start metadata. Reconstructing punch takes by saving all preroll audio
changes capture semantics and does not implement actual punch activation.

A prepared pool provides bounded startup reserve, not an unlimited disk stall
promise. Deferred start remains one-shot; repeated takes use separately admitted
slots retired after audio's final use and disk join. Reliable command/receipt
credits, latency postroll, continuous monitoring/EQ, native adapters, desktop and
failure recovery remain the next implementation task. This prerequisite does not
establish manual-punch or full frozen-reference parity.
