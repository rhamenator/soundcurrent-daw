# ADR 200: Version clocked protected pair selection and select before execution

Status: accepted for application preparation and paired adoption; fractional desktop adoption qualified; repeated post-SRC desktop qualification pending.
Date: 2026-10-15.

## Decision

Introduce `CompPairSpanPolicy::ClockedProtected` with a distinct persisted manifest.
Select it in application preparation from source and retained clock metadata.
Protected children use the exact original-geometry worker identity; uniform and
existing-media sides keep their own existing processing paths. Decode saved policy
explicitly and reconstruct digest-bound requests. Preserve old saved policies.

## Reasons and consequences

Fractional edits and baked speed changes need complete original geometry even when
semantic anchors lie outside a cropped ancestor. Selecting the policy before any
worker starts preserves auditable provenance; falling back after failed rendering
would change the meaning of the edit. The new manifest also prevents old readers
from silently interpreting a new selection as the legacy algorithm.

The application preparation service now reaches this path, with a fractional desktop adoption checked; repeated post-SRC desktop and native
qualification remain required. Short uniform windows still follow the existing
backend refusal. See [checkpoint 313](../313-comp-clocked-protected-pair.md).
