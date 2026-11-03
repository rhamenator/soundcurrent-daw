# Restoration intent and publication records

Exact asset restoration writes a bounded `sc-asset-restoration-v1` intent into
its exclusive UUID operation directory before creating or copying staged media.
The record identifies the operation, saved asset, project epoch/revision/request,
portable canonical path, exact whole-file SHA and byte count, frames, sample rate
and channel layout. It persists no source root or reopening authority. A failed
preparation leaves whatever partial evidence was written; only a complete valid
record may be admitted for inspection.

The immutable record and its codec use the source resource ledger. The wire is
at most 8192 bytes of ASCII JSON; UTF-8 paths are encoded as lowercase hex and
validated against the portable project path rules. Decoding reserves parser and
value banks, rejects duplicate, missing, unknown, nested, wrongly typed and
out-of-range fields, and observes cancellation. Live verification binds the
record to the current saved asset, context, operation, phase and ledger. These
checks perform no media I/O and never grant destination access.

Publication prepares, flushes and reads back a `receipt.partial` carrying the
Published phase before the canonical media rename. That partial is prospective
evidence and cannot establish that the rename happened. After the final trusted
observer boundary, intent, pending receipt and staged media are checked again.
The canonical rename remains exclusive and is the media publication point.
Only after that point is `receipt.partial` renamed exclusively to `receipt.json`.

`MediaCommitOutcome.published` reports actual media publication;
`receiptPublished` separately reports the final receipt rename. Cancellation
after the media rename cannot erase either fact and does not interrupt receipt
publication. A receipt-rename, post-publication observer or directory-flush
failure preserves `published=true`, sets `postCommitFlushFailed=true` and leaves
inspectable evidence. The legacy failure flag covers post-publication audit
failures as well as directory flushes. A final receipt name reports visibility,
not successful directory flushes; use the separate durability field. Linux
flushes the intent and its directory ancestry before copying and the publication
directories on success. Windows reports flushed files only. Retire the prepared
owner before opening held files on Windows.

Records are evidence, not a restart transaction. A crash between media and
receipt publication can leave exact canonical media and only a partial receipt.
Conversely a copied or fabricated final record is not proof of canonical media.
Recovery must freshly verify the independently admitted manifest, canonical
content and current ownership before deciding what happened. Old process epochs
and request tokens do not authorize automatic resumption. Trusted destination
writers remain part of the existing contract.

Focused tests cover strict schema and ownership admission, Unicode paths,
interrupted intent/copy/receipt preparation, final receipt contents, pre-rename
tampering, post-rename receipt failure and cancellation, and native handle and
credit retirement. Existing exact-byte restoration and staging assertions remain.
The same new corpora are explicitly selected by native Windows CI; local cross
syntax checks are only compilation evidence. Native runtime qualification is
recorded separately when that CI finishes.
The Windows operation directory retains read/write sharing needed for native
handle-relative receipt renames while denying delete sharing. Failure diagnostics
emit explicit UTF-8 fixture markers; the bounded CI collector retains generated
restoration media, intents and partial receipts under the same no-link,
no-overwrite and exact-copy rules used for other owned failure fixtures.

P084 remains open: isolated worker/controller integration, admitted passive
manifest recovery before strict Open, model/root/autosave serialization and the
complete localized desktop recovery workflow are still required. Strict
ProjectStore media gates and the frozen Windows preview are unchanged.
