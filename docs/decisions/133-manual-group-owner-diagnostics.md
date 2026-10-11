# ADR133: inspect original take ownership before fixing a group timeout

2026-10-11. Accepted diagnostic API and bounded fixture experiment; original
Windows take-group cause remains unresolved.

The original timeout has a continuing audio owner and one occupied group, but
filename/size logs cannot distinguish capture retirement, pending command credit,
worker completion, disk finalization and control reclamation.

Expose a bounded, identity-bound control-owner diagnostic using only control
state, atomic accessors and audio-published immutable facts. Preserve unavailable
facts after reclamation instead of inferring them. Keep diagnostic observations
separate from authority to join/reclaim/adopt. Use fixed UUID storage and retain
original disk observation phases in the test with bounded coherent snapshots.

Validate against an actual worker held at final asset hashing, including exact
media/receipt preservation and post-join identity retirement. Correct the separate
test check-counter race through an always-lock-free atomic. Keep original reports,
assertions, deadlines and failed Windows cohort.

Run at most three original20-take Windows workflows, stop on first failure and
preserve both raw output streams. A60-second outer process experiment budget is
separate from the unchanged five-second fixture acceptance deadline. A green
bounded experiment is limited non-reproduction, not a causal fix.

No recording algorithm, disk format, driver, dependency, GUI, branch protection or
native factory change. [Checkpoint201](../201-manual-group-owner-diagnostics.md)
records evidence and remaining work.
