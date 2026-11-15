# ADR 171: verified comp adoption at the canonical owner

Status: implemented; qualification is recorded in checkpoint 286.

Keep model-level `ApplyCompAssembly` separate from desktop authority to submit
that edit. The general session API remains usable for deterministic editing and
format validation. The desktop project owner rejects a raw `ApplyCompAssembly`
and accepts only an immutable `VerifiedCompAssemblyAdoption` from its joined
memory ledger, with the current project ID, root and opening epoch.

Construct that value off the GUI and audio threads. Bind it to independent saved
selection/request references, verify actual owned files and complete PCM, prepare
both playback-source records, and preflight the atomic edit. Its private
constructor prevents a caller from turning a completion label or supplied hash
into verification authority. Retain its reservation through the bounded command
queue and history preflight. The canonical owner rechecks expected clips, takes,
source declarations and references before committing either side.

Main-window Review is read-only. Explicit Apply requests fresh background
verification instead of reusing the earlier Review result as authority. This
detects files changed while the user reviewed the dialog. The immutable value
records what was verified; it does not promise that an external process cannot
later change a file. Playback/export continue their own asset-integrity checks.

The review dialog lists saved pending requests in bounded pages. It does not
discover authority by scanning completion directories. Reopening the same root
invalidates old UI ownership, and closing the review cancels pending verification.
An already accepted canonical edit completes and is recoverable through Undo.
Window shutdown waits asynchronously for the controller to close. Save remains
explicit, matching other project edits.

This does not replace the existing unity/same-rate handle verifier, certify
general sonic quality, or implement new transition creation. Retain those gates
and gaps separately. The full workflow still needs user-facing preparation of
new paired requests, repeated source-aware regeneration and processing-quality
qualification. Disk cancellation remains cooperative; no OS sandbox or hard
stuck-I/O shutdown bound is claimed.
