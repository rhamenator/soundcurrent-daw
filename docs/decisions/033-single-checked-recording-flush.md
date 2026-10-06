# ADR-033: use one checked audio flush before journal publication

Date: 2026-10-06. Status: accepted for the existing recording writer.

## Context

The instrumented long native attempt passes callback timing but exhausts lane 25's
capture reserve after repeated slow checkpoint work. Current AudioFile checkpoints
call libsndfile 1.2.2 sf_write_sync and then flush the same owned descriptor again.
Pinned source shows the library call only invokes an unchecked OS sync.

## Decision

Remove sf_write_sync from that path. Retain checked header/error handling and the
application's checked fsync/FlushFileBuffers before the unchanged journal flush,
atomic publication and directory flush. Preserve frame cadence, reserve, layout,
RT/data flow and sample/deadline qualification gates. Re-audit on library upgrades.

Inject EIO at the actual application audio fsync on Linux and verify publication
is refused, capture is informed and recovery uses the earlier valid prefix without
mutating originals. Scope the wrapper to an owned inode; it does not intercept the
library DSO or prove timing savings. Use short native/recovery gates before another
unchanged 1800-second qualification. Preserve all historical failures and sources.

## Consequences

Avoid one redundant OS flush per audio checkpoint without weakening the checked
persistence boundary. No new dependency/license or project format is introduced.
This does not prove the root cause of host storage slowdowns, full-duration success,
Windows execution, physical latency, power-loss durability or reference parity.
See [contract](../43-recording-checked-flush.md) and its source-linked evidence.
