# ADR141: owned native monitoring and original recording workers

Accepted 2026-10-11. GPL-3.0-only; no new dependency.

## Decision

Own successfully prepared native endpoints, immutable route storage and separate
processing/disk workers in one control object. Bind before publishing pending
recording state; publish pending state before activation. Join all native/worker
owners before settling outcomes or retiring callback dependencies. Preserve first
errors and actual missing/failed native ends, with inactive common-prefix reporting
after releasing the raw job lease when full finalization fails.

Keep QPC, native positions, opaque monitor ordinals and project timing separate.
Read current QPC after pumping original clock records. Expose fixed lock-free
copied/released source-frame receipts rather than mutable processing statistics.
Empty cancellations do not become empty finalized takes or storage failures.

## Alternatives and consequences

GUI-owned callbacks/disk writing violate the existing real-time contract. Detached
threads cannot preserve callback lifetimes. Treating a valid raw file as completed
monitoring loses native failure evidence. Increasing queues or replacing original
acceptance deadlines to hide instrumentation/fixture pressure would weaken evidence.

The genuine WASAPI adapter reuses existing stream wrappers. Controlled virtual
devices verify lifetime/state workflows; independent capture verifies original
queue pressure. Physical scheduling/latency and cold-start/arming behavior need
their own qualification. Monitoring EQ/controller integration remains next; see
[checkpoint212](../212-native-monitored-recording-owner.md).
