# Controller saved publication admission

`PublishSavedRestoration` is an internal controller command distinct from the
stage-only command. It accepts only the exact current private source-preparation
job and a strictly increasing publication request. The accepted controller must
remain empty, with the original epoch/revision and no active I/O, gesture,
autosave inhibition or unresolved publication ambiguity. Path, source override,
inspection and accepted-restoration DTO inputs refuse. Rejected admission retains
the previous private preparation and previous completion. Successful I/O admission
consumes the private preparation once; retry requires a fresh preparation.

The owned I/O job retains the original admitted private preparation, source ticket
and actual checker snapshot and invokes the qualified live supervisor with the
real controller close/cancel atomics and the same permission mutex used by cancel
and shutdown. `CancelSavedPublication` requires the exact active publication job.
Competing commands cannot steal the empty owner or change its context. Stage-only
commands and cancellation retain their separate semantics.

Completion checks the real current empty owner, epoch/revision, job, actual
retirement, normal exit, successful terminal and independent canonical/receipt
verification under the shared cancel/close ordering. A READY-record fact alone is
not successful completion. Late cancel/close or qualification failure preserves
actual observed publication, receipt and durability facts, while operation error
and reconciliation remain conservative. Normal verified publication changes no
accepted Session, root, history or revision and performs no automatic adoption.

After any possible permission prefix without full current-owner qualification,
the controller establishes a reconciliation latch before diagnostic allocations.
It retains the original admitted private preparation and operation identity,
not a reconstruction from an exited child DTO. The latch survives rejected
commands and later snapshots for this controller lifetime. Until fresh current-fact
reconciliation is implemented and qualified, it refuses every further command
except a passive barrier; asynchronous shutdown remains available and does not
claim resolution. No API clears the latch from a historical receipt, child exit,
replayed request or caller-supplied observation. This is an intentional incomplete
internal workflow, not a new recovery GUI or a claim of restart-persistent state.

Native controller tests use actual private preparation, checker and live worker
and qualify successful publication, admission refusal/preservation, competing
commands, stale cancellation, cancel/close through launch/READY/permission and
verification boundaries, late control-thread close and hook failure, malformed
terminal, deadline, source/root changes, canonical corruption and persistent latch
guards. Independent bytes/receipt and strict Open are checked after retirement;
accepted snapshots stay cold. The unchanged outer deadlines and both actual native
platforms, full original logs/archive integrity and exact-head reviews are gates.

Fresh passive saved current-fact reconciliation, explicit strict Open/adoption,
localized recovery interaction and restart recovery remain following work. Full
frozen product-reference, processing/content/import/equipment/all-Europe language,
GPL/source, native installer and release qualification requirements remain. No
installer, release or full-product completion follows from this internal command.
