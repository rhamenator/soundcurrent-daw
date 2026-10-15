# ADR 198: Recover protected comp clocks from complete original geometry

Status: accepted for the off-thread adapter; application integration pending.
Date: 2026-10-15.

## Context

Repeated comp rendering can retain only a crop of the original semantic map.
Original anchors outside that crop still determine protected windows and analysis.
Ancestors can also contain baked varispeed and use a different sample rate.

## Decision

Own the complete original protected geometry and extend its outer gap slopes over
the current candidate and its own ancestors. Prove each retained map's affine
clock over the union of both maps' knots. Preserve original raw identity and
explicit baked pitch/rate semantics. Use the versioned processor identifier
`soundcurrent.comp-span-protected-clocked-v1` for future worker integration.

## Consequences

Cropped artifacts no longer define new intrinsic analysis regions. Endpoint-only
checks and unsupported protected pitch/context are refused. Preparation requires
bounded, credited off-thread storage and cancellation. File verification, worker
protocols, publication and project adoption must separately establish authority;
metadata fixtures cannot stand in for those workflows. Existing processor outputs
and schemas remain usable. See [evidence](../311-comp-clocked-protected-span.md).
