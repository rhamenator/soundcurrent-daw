# ADR 183: share complete historical verification within one paired operation

Status: accepted for implementation; native and Debug qualification pending.
Date: 2026-10-14.

## Evidence and decision

The original CI run 38655828613 exceeded the existing 180-second version-two
worker test deadline after six successful cases, required failure cases, and
preparation of the final sample-rate case. The artifact does not identify a
stalled child. Inspection found that span workers, assembly publication/public
verification and adoption each review both complete source histories separately,
even when the first review covers both paired outputs and all ancestors.

Use a bounded two-slot helper. Check both current source snapshots first. Perform
one complete original-history review, retaining its resource lease and dependency
snapshot. A later source may share that review only if every exact historical
record in its closed source history is contained in the completed review. The
single-source reviewer has already verified all unique original assemblies,
complete PCM and provenance, and expanded dependencies from unchanged siblings.

Source IDs, shared raw media, equal history lengths or matching selection IDs do
not establish coverage. Distinct and partially covered histories receive their
own complete review. No union of unverified records is trusted. The helper
checks all source snapshots again, including a covered source's distinct live
clip, and rechecks retained review metadata before returning. Cancellation and
the caller's cooperative deadline poll remain active between stages and during
coverage comparison. This does not introduce a hard OS-level I/O deadline.

The return value has two optional review slots and requires no unbounded
container allocation. Each retained history review owns its existing conservative
joined memory reservation. Adoption builds its dependency guard from all returned
reviews and the original source selection; covered current sources remain part
of canonical Apply freshness validation.

## Lifetime and integrity boundaries

Sharing lasts for one call. Every existing before/after publication and
verification operation calls the helper afresh. Holding a previous result never
permits skipping new filesystem checks. Original immutable-owned-file assumptions,
job leases, complete PCM comparison, receipt binding, current metadata checks and
existing worker/test/job deadlines remain unchanged. This is serialized control
and disk work, never a real-time callback operation.

## Required evidence

Test both source orders, a single incoming source, no history, stale current clips
on either side, changes to the covered clip during review, cancellation before
and after the first review, joined memory and sample limits, covered output
corruption, and corruption between calls while a previous review is retained.
The additional coverage runs in a separate test suite, preserving the original
history corpus and all existing deadlines. Produce a separate real assembly to prove disjoint histories are each verified
in either order and corruption in the second assembly is rejected. Test original
history faults and actual repeated paired/mixed publication, adoption, Undo/Redo,
reopen and export. Count complete comparisons and record paired/separate timing;
do not gate correctness on a noisy wall-clock speed ratio.

The application repeated-render gate remains until its actual desktop workflow
is qualified. This optimization makes no independent processing-quality claim.
