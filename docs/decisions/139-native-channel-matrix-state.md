# ADR139: versioned native monitoring matrix artifact

Status: accepted for prepared state and simulated startup; native factory disabled.
Date:2026-10-11.

## Decision

Represent a monitor matrix as a bounded canonical binary artifact and bind it by
SHA256 and widths in auxiliary native state 1.2 and the fixed startup receipt.
Publish the artifact before pending native state using the existing exclusive
file publication, flush and inactive-job lease infrastructure. Verify it under
that lease before admitting stored native state. Hash, allocate and read only
on disk/control owners. Keep original capture/converter width and raw media.

Keep old 1.0/1.1 keysets exact and refuse matrices for those writer versions.
Explicit migration preserves original observations and absent mapping, supports
selected versions, rejects downgrade and is idempotent. Do not infer a route
from unequal legacy widths. General project/raw formats remain unchanged.

## Alternatives and consequences

Embedding 65,536 JSON routes would exceed the established journal/parser budgets.
Reducing the route count would contradict the prepared mapper's accepted bounds.
A fixed basename and strict version/length/hash/geometry validation retain the
full matrix without relaxing the 16KiB journal or 128KiB metadata-read budgets.
Large artifact reads use 64KiB exact chunks plus EOF checking.

Reuse the repository's existing GPL matrix type and SHA provider; no new
third-party library or algorithm license is introduced. The format fixes route
order and IEEE754 float64 gains. It does not define speaker positions, pan laws,
live editing or portable alignment. Maximum route admission remains a bounded
work/storage contract rather than a hardware deadline guarantee.

A mapped full-FIFO startup now has a verifiable declaration and output width.
Factory activation still depends on native owner/SDK/deadline/recovery evidence.
The existing required Windows job failure remains unresolved and must not be
reclassified as passing because these simulated or cross-compiled tests pass.
