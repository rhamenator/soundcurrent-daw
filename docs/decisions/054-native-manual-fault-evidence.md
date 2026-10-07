# ADR 054: preserve native manual fault evidence before UI integration

Status: accepted, 2026-10-07 UTC. GPL-3.0-only. No new dependency.

## Decision

Qualify the actual native manual adapter with an opt-in fault/recovery fixture,
independent raw and nonflat output oracles, explicit partial adoption and copied
checkpoint recovery. Preserve each original failure before further runs or builds.
Keep successful finite observations separate from native alignment, deadline and
sustained qualification. Hold a retired hash fault until delayed postroll completes;
never replace it with an easier active failure. Treat early zero-durable cancellation
as preserved empty state, not a fabricated recovered take.

## Consequences

The new tests expose native channel alignment and callback CPU/deadline failures.
They remain required work before claiming native manual recovery or moving this
checkpoint into a user-visible manual-control workflow. Diagnostics retain only
owned route metadata and bounded traces. Source/owner/sink deadline gates, original
errors, original media and all prior observations remain intact. See
[contract](../68-native-manual-fault-recovery.md).
