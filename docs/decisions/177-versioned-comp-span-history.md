# ADR 177: Versioned comp span workers with original-history review

Date: 2026-10-14. Status: implemented for standalone uniform span jobs.

## Decision

Retain the version-one request and processor for selections without regenerated
sources. A selection with either regenerated side uses `sc-comp-span-v2` and
`soundcurrent.comp-span-positioned-rubberband4-r3-v2`, even when the particular
child renders an original source beside an unchanged assembled source.

The version-two request binds raw and output sample rates, the exact rational
backend clock factor, accumulated baked pitch, and processing-grid step. Decode
reconstructs the recipe and span and compares these fields exactly. Readiness,
intent and completion carry the same binding. Reject downgrades, unknown fields,
changed clocks, floating-point substitutes for integers and executor mismatches.
Write and verify output RF64 at the prepared output rate, retaining raw source
rate for reading and DSP. The DSP arithmetic follows ADR 176.

## Original files and layering

The disposable worker reviews both sides' original saved assembly histories
before readiness and again after rendering, before publishing completed media.
An unchanged assembled sibling is a dependency. Missing or changed historical
jobs invalidate the new candidate. Review uses the iterative bounded history
reviewer from ADR 175 and checks current recipe metadata. Current source extent
admission also covers resolved ancestral assets.

The high-level assembly library owns this integration, and the span worker links
that library. The low-level span codec stays independent of historical assembly
verification, avoiding a circular library dependency. `verifyOwnedCompSpan`
checks the candidate and its immediate file bindings; version-two consumers must
also invoke `verifyCompSpanHistory`. A completed candidate is not authority to
modify canonical project state.

## Resource and safety boundaries

The containing child retains its memory ceiling, deadline watchdog, original
process observation and cancellation protocol. Historical review uses bounded
unique-assembly and sample-comparison budgets. Each side is reviewed separately,
so shared historical assemblies may be checked twice. Cancellation is cooperative
inside history review; the containing worker's watchdog supplies the hard process
deadline. Neither mechanism is a privilege sandbox.

The caller's current maximum source extent is preflighted before historical PCM
verification, including original child spans and assembled outputs. This relies
on the existing immutable-owned-file contract. It is not an OS-enforced I/O
budget, a cumulative byte limit, or protection against arbitrary concurrent
external replacement. Historical jobs still retain their original recorded
verification policies.

## Remaining integration

Keep regenerated paired preparation and adoption gated. Before enabling them,
carry the complete historical dependency snapshot through the bounded Apply
queue and recheck it at the canonical owner. Separate internal file verification
from public historical verification to prevent recursive assembly review.
Qualify repeated assembled generations, mixed pairs, retained assembled takes,
Undo/Redo, save/reopen, export and unchanged PCM outside the edit. Standalone
worker success establishes neither those workflows nor processing-quality parity.
