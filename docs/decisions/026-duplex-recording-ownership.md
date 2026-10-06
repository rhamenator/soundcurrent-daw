# ADR-026: own simultaneous recording outside the callback

Date: 2026-10-06. Status: accepted for M2d2 foundation.

## Context

ADR-025's bridge borrows prepared pools/run; it does not own native activation,
disk workers or recovery receipts. Late combined admission could allocate many
capture pools before rejecting a generation. A single exception during joining
must not abandon other writers, and canceled disk owners can retain activity
leases after their threads finish.

## Decision

- Add a framework-free `DuplexRecordingRun` for immutable preparation, aggregate
  capture admission before allocation, activation-time writers, joined shutdown
  and independent per-lane receipt/error/progress/job retention.
- Use one `PipeWireDuplexRecording` filter with explicit packed input/master
  output routes; reuse PipeWire and existing adapter routing/lifetime policy.
  Native callback join always precedes pipe finishing and disk joins.
- Attempt all writer preparation before native activation. Retain the original
  activation error independently of earlier empty takes. A constructor failure
  may leave diagnostic partial files; do not fabricate successful recovery or
  delete existing media as rollback.
- Join every worker, then retire its disk owner while retaining plain receipts
  and errors. Cancel every worker before joining any. Expose the reader exception
  separately so raw prefixes survive playback-read failure.
- Keep one existing fair reader and one writer thread per armed track for this
  bounded foundation. No new dependency or license change. Admission bounds are
  conservative payload limits, not operating-system resource/deadline guarantees.
- Qualify the production native owner with independent owned sources/sink before
  wiring desktop grouped arm and verified-take transactions. Windows shares the
  generic lifecycle core but needs its own native adapter and execution evidence.

## Consequences

Playback/live monitoring/raw recording now share the admitted callback clock
through a production native owner. The callback retains no disk/GUI/allocation/
blocking work. Multi-track UI handoff, native Windows, load/duration, process-kill/
disk-full and physical timing remain required. This decision changes no frozen
functional/quality/content/native-format acceptance or parity status.
