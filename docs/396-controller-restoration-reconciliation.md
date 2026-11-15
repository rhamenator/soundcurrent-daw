# Explicit live restoration reconciliation

When a permitted restoration ends ambiguously, the controller now retains a
private binding to its original project, asset, operation, epoch and revision.
The original child must have a captured PID and completed/reaped exit before the
binding qualifies. The sticky write guard is established before diagnostic
allocation; an unavailable binding keeps it blocked. Public observations and
historical journal tokens cannot manufacture this binding.

`ReconcileAssetRestoration` takes the same correlated request shape as inspection,
but acquires a fresh serialized I/O owner. It requires the live ambiguity guard,
the exact private original binding, the current accepted model and a saved
manual-model snapshot. Requests share inspection's increasing namespace. Active
I/O, edit gestures, foreign recording inhibition, stale context and exhausted
identity refuse. Early refusal does not consume a request. Barriers and exact
inspection-job cancellation remain available; competing mutations refuse.

The I/O worker opens a fresh native audit with the existing shared resource
ledger and trusted encoded/parser/state/media budgets. It requires the passive
manual manifest to equal the retained saved model, consistent required intent
and optional receipts, and independently verified canonical extent/full SHA256.
The accepted model may contain unsaved edits: it is not replaced by that passive
manifest. Manifest content drift refuses even when the selected asset matches.
A prior inspection cannot authorize release. Missing canonical media, staged
bytes, or a final receipt alone do not qualify. Reconciliation does not reopen
the selected source or publish, repair, finalize or delete operation files.

The audit re-verifies pinned bytes and current relative names after the trusted
verification hook. Native owners retire on I/O before the fixed-size completion
reaches control. Control rechecks the actual job/root/session/epoch/revision,
saved-model pointer and original binding under the existing cancel/closing
mutex. Only a successful fresh reconciliation clears the live guard and returns
`ambiguityReleased=true`. Cancellation, closing or any error suppresses qualified
observations and leaves the guard in place. Ordinary inspection always leaves
that flag false. This work does not acquire or release an autosave token.

The original restoration completion is historical evidence. Its unknown
publication result, child PID/exit, permit, receipt/durability and needs-review
facts remain unchanged after live release. Exact expected bytes currently being
available does not attribute their publication to the original child. The
Session, history, revisions, root and manual manifest remain unchanged during
reconciliation; later ordinary edits, undo, explicit save and due autosave can
resume through their existing gates.

This is a snapshot under the existing trusted destination-writer contract, not
an atomic namespace guarantee against hostile same-user directory writers or a
guarantee of future media availability. Audit hashes reuse the original checked
asset/content binding; they do not introduce a new parent-process audio decoder
or independently establish finite-audio processing quality. Cold-open adoption,
retry with a newly selected checked source when canonical bytes remain missing,
operation cleanup and localized recovery UI remain open. This is not full P084,
Windows preview promotion, an official release or whole-product completion.

The actual controller corpus covers dirty-model success after passive inspection,
wrong original owners and replay, source/record preservation, saved-manifest
drift and recovery, prior observation followed by missing/changed canonical
media, receipt-only and staged-only states, malformed/contradictory records,
shared-budget refusal and credit retirement, competing commands, exact/wrong
cancellation and shutdown/join. Linux changes during the final verification are
detected; Windows pins must block native write access. Explicit save, undo and
timed autosave must resume only after qualified release. Required native Windows
CI execution and exact-head review remain gates before integrating this slice.
The frozen Windows preview producer and delivery are unchanged.
