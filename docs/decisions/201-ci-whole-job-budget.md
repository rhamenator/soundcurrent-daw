# ADR 201: Allocate CI work against the whole job budget

Status: accepted; fresh runner qualification pending.
Date: 2026-10-15.

## Evidence

Original run 38717457857 completed all 216 Linux CTest cases. Shard zero used
637 seconds to build and 936.23 seconds for tests, then verified retained evidence
and uploaded its archive. GitHub cancelled the whole job at its 30-minute limit.
The failed run, timeout annotation, full logs and original artifacts remain retained.

## Decision

Use five Linux shards for the current 218-test inventory. Keep weighted assignment
from observed complete test timings; label the two new desktop estimates separately.
Every test is assigned exactly once, and CTest's own regex selection must match the
computed partition. Keep the existing 30-minute job limit, individual deadlines,
retained-evidence checks and required aggregate. Add the two new desktop banks to
the existing native repeated-workflow job.

## Consequences

The current estimates allocate about 576.48 seconds of test work per shard, leaving
more of the whole-job budget for builds, checkout, archive verification and uploads.
Two additional runners repeat the build and archive checks; that extra CI work is
accepted to retain all gates without a deadline increase. Scheduling estimates are
not acceptance evidence or a promise of runner timing. Fresh CI must pass.

The Windows native failure in the same run has a separate path-observation fix;
sharding cannot resolve it. See [checkpoint 314](../314-comp-clocked-repeat-ui.md).
