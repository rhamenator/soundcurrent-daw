# Saved evidence across an owned staging transition

Creating a fresh restoration operation directory changes its native parent
stamp. Ordinary RestorationAudit::reverify continues to reject that mutation.
It must not be made permissive merely to allow later cold recovery staging.

The read-only reverifyAfterOwnedStaging path instead requires a live, admitted,
unattempted PreparedAssetRestoration. Its private StagedMedia owner captures the
actual held parent's full native stamp before directory creation and after its
staging work. The same native stamp implementation is used by approved media and
staging. The original audit stamp must exactly match that captured before stamp;
the current audit root and the stage's held parent must match its captured after
stamp. A different native directory, subsequent parent mutation, moved owner,
foreign/sibling scope or attempted publication is refused. An audit created
after staging cannot retroactively establish the required before state.

The new operation must differ from the original saved operation, while its asset,
relative destination, checked bytes/hash, frame count, rate and layout match the
original saved target. The planned intent and staged content are checked through
this live owner's native handles, including on Windows where its exclusive
writer correctly prevents a generic pathname audit. Linux additionally verifies
operation/intent/media names against the exact held inodes and rejects extra
file links. Windows held operation/file handles deny deletion and replacement.
All original saved manifest, intent, receipt and media pins, named presence and
hashes are reverified between live-stage checks. Work is admitted in the same
ledger/scope and cancellation propagates.

The typed check does not update the audit's original stamp, fingerprint or
historical context. Repeating ordinary reverify still fails after a successful
typed check. No file is published, replayed, adopted, removed or retagged, and
successful verification is not a publication permit. The serialized I/O caller
retains both native owners and must establish current controller and isolated
child ownership separately before later use. Existing trusted-parent/same-user
writer and non-atomic snapshot limits remain; this does not claim a global
filesystem transaction or detect unrelated namespace writes during the owned
staging interval beyond the checked evidence and native ownership.

The existing accepted-owner prepare policy, including its nonzero project epoch,
remains unchanged. This proof does not fabricate an accepted Session or make that
API a cold-controller dispatcher. A future isolated saved-recovery owner still
needs its own current context, fresh selected source qualification/full finite
decode, one-use permission, canonical verification and explicit model adoption.
Original journal and unknown-publication facts never become current tokens.

The actual controller qualification corpus exercises successful and repeated
read-only proofs, unchanged strict generic rejection, moved and attempted owners,
wrong target/root/scope, cancellation, resource refusal/credit retirement,
subsequent parent changes, staged and original-media mutation/pinning, and
same-byte intent-name substitution. Actual native Linux and Windows execution,
full retained CI evidence and completed available bot reviews gate integration.
The shared-source policy and November 6 preview-freeze lift remain in effect;
all frozen product-reference, full product scope, localization and GPL/native
release requirements remain open and preserved.
