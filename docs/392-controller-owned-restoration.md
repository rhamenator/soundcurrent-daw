# Controller-owned restoration transaction

`ProjectController` now accepts a typed `RestoreAsset` command referencing an
admitted completed replacement checker snapshot, its asset and original request.
The control worker rechecks the CURRENT accepted model/root/epoch/revision and
actual checking job/PID, refuses an active gesture, I/O or inhibition owner, and
atomically starts its own serialized restoration I/O job. It assigns a fresh
dispatch identity and atomically acquires the caller's fresh monotonic autosave
token/epoch from the existing shared token namespace. It does not secretly
advance a recording caller's next token. Refused acquisition consumes
neither identity nor token. The immutable Session remains unchanged. Restoration request IDs increase across
successful dispatches for this controller lifetime; completion and cancellation
are correlated with the controller-assigned dispatch job.

While that job owns the project, every model, history, policy and competing I/O
command refuses, including a matching generic inhibition release. Read-only
barriers remain usable. Scheduled autosave also stays ineligible. This is an
actual accepted-owner guard, rather than a snapshot check or a changed generic
recording inhibition policy; ordinary inhibition still permits existing editing
workflows outside restoration.

The owned I/O worker builds the existing bounded child request using the exact
prior checker protocol. It captures the restoration child's actual native PID,
strictly validates READY fields and Planned record against its admitted ticket,
then issues the one child-bound publication permit and closes stdin. Permission
issuance shares a short mutex with cancel and shutdown: cancellation ordered
before permission prevents it. No callback or process wait holds that mutex.
After permission, cancellation may race with publication and is not rollback.

The supervisor bounds retained channel/parser/copy banks, requires an absolute
trusted executable and enforces a default 20-second deadline around process
progress. Overflow, malformed framing/schema/PID/record, timeout and exceptions
kill and reap the exact owned child before returning its work credit or releasing
project ownership. Qt's internal buffers and OS I/O are not hard RSS or wall-time
sandbox guarantees; a blocked OS read or process reap still depends on the OS.
No blocking work, process management or disk operation occurs on an audio thread
or the GUI thread. Qualification hooks are trusted application seams only.

After a normal strict terminal result and child retirement, the parent opens the
canonical media through approved native handles and independently verifies its
whole-file byte extent and SHA before releasing ownership. Third-party sample
decoding remains isolated in the child, which already freshly verified finite
WAVE content. Canonical hash equality preserves those exact checked bytes.
Publication, canonical verification, final receipt and durability remain distinct
completion fields. The original Session, manual-save identity, history, root and
project manifest are not edited. Windows reports file flushing only.

`published` is optional: it remains unknown when permission may have reached a
child but no complete valid terminal result was accepted. A malformed terminal
can coexist with exact canonical bytes. Such outcomes latch
`assetRestorationNeedsReconciliation`, blocking explicit writes, model changes
AND timed autosave. A receipt or absent reply cannot clear that latch. Its
operator recovery/reconciliation workflow is still open; closing the controller
is supported but reopening still uses existing strict project validation.
There is no placeholder recovery dialog or automatic source-root reopening.

Shutdown first marks the controller closing, then joins its I/O owner. That
owner retires the child and returns its terminal/ambiguous outcome; only then
are inhibition and `closed` published. Cancellation uses the exact current
dispatch job. Old requests cannot cancel a later child, reuse a consumed dispatch
or bypass no-overwrite publication.

The focused real-process corpus uses independent complete WAVE bytes and covers
accepted-prefix command rejection, token ownership, canonical verification,
wrong cancellation identity, canceled preparation, close before spawn and after
READY, stale/inhibited/busy admission, malformed/oversized/foreign READY and
terminal replies, preparation deadlines, post-permission ambiguity, and timed
autosave inhibition after ambiguity. Failed fixtures retain original bytes via
the existing bounded UTF-8 marker collector; successful owned fixtures are
removed only after child retirement. Native Windows CI remains a required gate.

This is a development controller API. The installed frozen preview, package
inputs, language qualification and release claims remain unchanged. Passive
manifest admission before strict Open, durable restart reconciliation, complete
localized recovery UI and the full P084 workflow remain open. The existing
source/destination writer trust and native filesystem contracts in docs386–391
continue to apply.
