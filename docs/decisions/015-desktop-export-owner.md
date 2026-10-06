# ADR 015: canonical snapshot export on a single-flight I/O owner

Status: accepted for the desktop preview, 2026-10-06.

Use S7a's framework-independent exporter on an independent Qt worker. Keep one job
reserved through inspection, consent and publication; admission cannot replace a queued
job. The accepted-command prefix is an immutable ProjectController barrier receipt.
Capture it again at Start, so dialog defaults do not freeze out newly accepted edits.
Do not save implicitly or apply later live edits to the renderer's private instance.

Hash/validate the destination on the worker. Existing-file approval is an asynchronous
window-modal GUI decision keyed by job; the worker retains the inspected hash, then
passes it to the core only after Yes. Require revalidation before publication. Preserve
No/close/cancel and changed-file failures. This is an owned-filesystem protocol, not a
security promise of atomic conditional replacement against hostile processes.

Use priority cancel/shutdown flags and one latest progress snapshot with 50 ms publish
throttling. Wait for actual cleanup/worker completion before the final window exit or
project replacement. Keep progress/cancel outside the scroll area. I/O, hashing and
allocation never enter a real-time callback. Low worker priority is helpful scheduling
policy, not a substitute for resource/deadline qualification.

Retain existing EQ/core/header/hash contracts, RIFF/RF64, float headroom, tail and
publication-warning semantics. No new runtime dependencies. Qualify actual written
outputs and GUI/worker/native failure sequences, including later live edit receipts.
Windows native UI, latency/automation/bus/stem rendering and M1 external-state gates
remain separate requirements; do not infer them from a successful Linux widget test.
