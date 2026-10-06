# ADR-036: isolate native processing intervals with test-only wrappers

Date: 2026-10-06. Status: accepted for bounded Linux diagnostics.

## Context

Removing settled EQ ramp traversal modestly improves an isolated benchmark but
does not resolve sustained native misses. A new failed callback consumes
25.807899 ms wall / 25.806142 ms thread CPU with no reported faults or switches.
Whole-callback accounting cannot locate the expensive interval. Production must
remain free of timing queries, logging and profiling allocations in callbacks.

## Decision

Build a separate, uninstalled Linux diagnostic executable. Wrap unresolved GNU
ABI calls between existing static-library objects to record bridge, raw capture,
EQ-driver and inclusive mix wall/CPU intervals. Keep one audio-owned fixed summary
and four worst complete callback snapshots with their original native clocks.
Reject unknown interval coverage explicitly. Retain all original whole-callback,
sample, current-period and RT audit gates, including observer overhead.

Verify ABI/actual boundary coverage using wrapped versus direct original bridge
execution with exact samples, failure/terminal scope and allocation/lock audits.
Declare nested measurements and static-build/inlining limitations. Qualify this
tool independently from the product and Windows. Adopt no new dependency.

## Consequences

Short measured tails vary between raw capture and EQ; averages do not establish
the long-run cause. No production processor/backend API, clocks, arithmetic,
queues, channel policy, parameters, schema or durability changes. Original failed
audio and all eighteen historical observations remain preserved. Repeat the
unchanged long workload with this separate tool to obtain same-cycle evidence
before selecting another processing change. See the
[contract](../46-native-processing-stage-diagnostics.md).
