# ADR 021: one project cursor and explicit multitrack matrix

Date: 2026-10-06. Status: accepted for M2c1.

## Decision

Prepare independent existing EQ/event nodes per stable track and sum their
outputs through an explicit sparse channel matrix at one project cursor. Use
one read-ahead pipe per lane and one fair read owner; gap silence and late-data
discard retain timestamps. Aggregate payload/file-reference admission applies
before activation. Preparation, file decoding and destruction stay outside RT.

Route both static selected-track and multitrack offline exports through this
same processing implementation and the existing publication transaction. Keep
monitoring/correction separate from default print/export state. Preserve the
single-track public spec via shared settings, while a mix spec carries its own
plan. Generation-scoped ordinals are transient; model/parameter IDs remain UUIDs.

## Alternatives and consequences

Independent playback owners would drift or start at different boundaries and
cannot establish simultaneous overdub alignment. A separate offline summing path
would duplicate DSP semantics and weaken live/export comparisons. A full arbitrary
DAG/plugin/PDC compiler in this step would conflate that later contract with the
first multitrack clock/routing implementation. The star topology is an explicit
foundation; general buses/feedback/delays remain staged requirements.

Per-track readers currently duplicate file handles/hashes; total references and
payload are checked. A shared immutable source cache can later reduce that cost.
No external dependency/license is added. Existing EQ, queues, media and native
worker infrastructure remain in use; the equalizer checkouts are untouched.
See [contract](../31-multitrack-mix.md).
