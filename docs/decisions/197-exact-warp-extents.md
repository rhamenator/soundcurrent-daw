# ADR 197: distinguish exact semantic extents from sample counts

Status: accepted for internal geometry, 2026-10-15.

## Decision

Add an explicit exact-domain constructor path with rational input/output extents.
Share the original piecewise and protected-window machinery. Preserve the legacy
integer-grid constructor and refuse its region/vendor adapters on exact plans.
Clocked rendering and protected-core sampling consume the exact accessor.

Do not round or clamp semantic endpoints. Validate actual sample positions in the
prepared sampling adapter, independently of exclusive semantic coordinates. Preserve
historical fractional-origin asset-edge behavior and checked arithmetic.

## Consequences

This requires callers to opt into the semantic model and prevents accidental
integer vendor adaptation. Project state and worker identity remain unchanged.
The comp/history adapter, worker protocol, adoption and independent quality gates
still require explicit integration. See [evidence](../310-exact-warp-extents.md).
