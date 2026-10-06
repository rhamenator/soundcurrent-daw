# ADR013: recording result handoff before project close

Status: accepted for S6 preview, 2026-10-05. [Contract and limits](../22-desktop-recording.md).

Use an asynchronous recording owner separate from canonical project control and project I/O. Keep explicit input/monitor selection, monitoring Off by default, arm/start admission and native/writer joins on the preparation worker. Follow immutable canonical models with bounded latest-state coalescing, preserving an exact admitted-event suffix and generation-matched applied receipts.

Retain one finalized take until the canonical asset identity proves attachment or the user explicitly keeps its files for recovery. New jobs cannot overwrite that handoff. Stop invalidates old queued transport by epoch and acknowledges only after joining. Normal window close waits for stop and receipt resolution before its canonical barrier/dirty prompt, then joins every worker asynchronously.

Manual recovery inspects an owned job, previews verified extent, revalidates/copies to a new UUID and uses the same verified attachment path. A changed prefix or wrong identity never silently enters the model. Originals remain untouched. Full automatic discovery, native Windows, per-channel routing persistence, overdub/multitrack/Auto monitoring, general undo and deadline/physical qualification remain required; this foundation does not reduce the frozen-reference target.
