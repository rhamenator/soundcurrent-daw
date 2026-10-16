# ADR 181: Historical verification around repeated paired production

Date: 2026-10-14. Status: implementation under qualification; application entry
point remains gated.

## Verification boundaries

Public assembly publication and verification review the original histories of
both source envelopes before and after the operation. This includes an assembled
sibling whose current media needs no new child span. Publication checks history
again after the final progress callback and before committing completion.

The iterative ancestry reviewer calls a private one-assembly file verifier.
That verifier checks original intent, completion, full media hashes, complete
sample recomputation and permitted edit scopes, without launching another
ancestry traversal. Public callers use the history-aware verifier. This preserves
bounded iterative traversal and prevents recursive review of every ancestor.

Low-level span candidate inspection and fragment streaming continue to report
file integrity and current metadata only. They confer neither model-adoption
authority nor permanent historical file integrity. High-level assembly operations
and verified adoption supply the history checks. Queued Apply still requires the
immutable dependency guard from ADR 179.

## Nominal and retained sources

For regenerated sources, verify the original nominal asset using the original
nominal clip and stretch settings. The current assembled playback clip is not a
new nominal rendering of the raw source.

Handle planning accepts a retained assembled take only when its recorded raw
source matches. Convert its allowed coverage through the recorded playback/raw
map, clamp an exclusive endpoint to the retained asset extent, and enforce the
same coverage limits as canonical session validation. This does not authorize
reconstruction outside the retained take's allowed range.

## Integration and limits

Allow low-level paired request preparation so actual repeated production can be
tested. Keep the application preparation entry point gated until the repeated
workflow and its user interface have acceptance evidence. The test corpus must
include successive paired adoptions, an unchanged retained assembled sibling,
stale queued Apply, historical-file failures before and late in publication,
Undo/Redo, saving/reopening and export consistency with live core playback.

Historical file admission uses the current assembly's maximum file extent plus
the existing unique-assembly/sample budgets and original per-job policies. Checks
poll cooperatively before and after each side's history review; this is not a
hard total I/O or OS-enforced deadline. The existing immutable-owned-file and
serialized-session contracts remain. DSP consistency is not independent sonic
quality or frozen-suite parity.
