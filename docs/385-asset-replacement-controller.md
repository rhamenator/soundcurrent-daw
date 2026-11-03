# Asset-bound asynchronous replacement review

`WaveCheckController::submitAssetReplacement` takes an accepted project snapshot,
an asset ID, an owner request number and an explicitly selected absolute file.
It refuses absent, closing or busy project state and assets outside that model.
The controller admits a bounded identity and ticket before publishing the job,
assigning the ticket the actual queue sequence under the same single-flight lock.
Failed admission consumes no job number. Revision zero remains valid.

Only asset ID, saved content hash, frame count, rate and channel layout are copied.
Project-relative paths and native evidence do not become candidate path authority.
The existing supervised child opens the explicit selection and produces the
unchanged strict WAVE v2 report; the controller compares that report to the saved
asset identity before publishing Complete. Wrong content yields MediaMismatch
with no successful report. Hashing and full finite decoding stay off the GUI.

`verifyWaveAssetReplacement` is the control-side consumption guard. Call it with
the current accepted project snapshot, asset ID, request and parent ledger. It
checks complete/non-canceled state, actual delivered job and observed child PID,
then rechecks the immutable ticket against the current epoch, revision and asset.
An older accepted project, changed asset or request cannot qualify for use. A
retained result may outlive the facade, while its leases remain charged; clearing,
ordinary subsequent jobs and shutdown retire the controller's replacement owner.

The result is evidence for review only. It neither reopens an input path nor
publishes media, modifies Session, grants file restoration authority or provides
an end-user recovery dialog. The later restoration transaction must revalidate
against the then-current accepted model and preserve project IDs, edits, takes,
provenance, Undo/Redo and save/reopen behavior. P084 remains open until that path
and its Linux/Windows desktop workflows are qualified. The frozen preview stays
unchanged.

Cancellation and successful terminal publication are serialized under the same
controller mutex. A stop request that wins before publication discards the
verified report and publishes Canceled, including after the verifier's last
stop-token poll. A deterministic off-thread qualification seam holds that exact
window; the unchanged regression fails without the terminal check. Cancellation
after terminal publication observes an already idle controller.
