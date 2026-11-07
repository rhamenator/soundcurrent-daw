# Fresh native source qualification for saved recovery

Saved-source preparation retains metadata and exact checker bytes. It does not
retain a native source handle or promise that the selected pathname still names
the same bytes. The next dispatch foundation now provides
verifySavedRestorationSourceFile for an I/O owner that has freshly approved an
explicitly selected source through ApprovedMediaRoot/openSelectedFilename.

The function accepts that native handle rather than reopening a stored path. It
requires the file's own and root's admission credits to belong to the same
ledger/scope as the current saved audit, pre-dispatch source ticket and completed
checker report. ApprovedMediaFile::ownedBy exposes that check without allocating
new credit or conflating sibling scopes. A moved native owner is refused.

The current owner context, actual completed checker PID, selected relative name,
saved fingerprint and checked source/audio tuple are verified before hashing.
The source's extent and native unchanged checks must pass; a bounded streaming
hash over the pinned file must equal the checked SHA-256. Source unchanged and
fresh saved-audit/report binding checks run again after the read. Cancellation
and resource admission apply to the existing bounded native hashing path. A
trusted per-chunk hook exists for deterministic cancellation/fault qualification.
No second decoder, report schema, namespace fallback or full-file allocation is
introduced.

ProjectController saved-source preparation now performs that fresh explicit
source approval on I/O after the real checker completes and before retaining
private metadata/protocol readiness. The source root/file retire on I/O before
completion; no native capability migrates to control or GUI. An adversarial
controller case changes a finite selected WAVE after checker reaping and before
final preparation; readiness must be refused on Linux and Windows.

The caller supplies an approved explicit native file under exclusive serialized
I/O ownership. This function verifies its content against the saved target; it
does not independently infer its selected pathname, dispatch a checker, observe
a child exit, or grant source/destination/publication authority. The actual
checker must already have decoded finite audio, and the caller must supply its
observed terminal identity. This is an instant qualification, not a durable
permission token. The native owner stays on I/O, and later copy/dispatch must
revalidate it and current saved/controller ownership. Stage creation, one-use
publication permission, canonical verification, model adoption and localized
recovery controls remain separate work. Original journal/unknown-publication
facts are preserved; they never authorize replay.

The actual controller corpus uses a real shared checker and native audit, then
qualifies a freshly opened source without adopting a model. Cases cover full
hashing, same/sibling/foreign admission scopes, stale request/PID before a read,
pre-read and per-chunk cancellation, resource refusal and credit retirement,
moved native owners, changed extent/content, and saved-name visibility changing
during source hashing. The last case must fail final saved-audit re-verification
on Linux and Windows. Both platforms execute this code from the same sources;
exact-head native CI and completed bot reviews qualify integration.

The November 6 preview-freeze lift remains in effect. Historical deliveries and
all frozen product-reference, processing, content, import, equipment, European
localization, native installer and GPL/source release gates remain preserved.
