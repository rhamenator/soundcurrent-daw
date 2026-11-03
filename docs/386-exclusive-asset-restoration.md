# Exclusive exact-asset restoration core

`PreparedAssetRestoration` is a serialized control/I/O owner for restoring a
missing owned asset. It takes an explicitly approved source file handle, an
explicit project root and the saved asset identity. Preparation validates the
portable `media/` path and trusted byte budget, fully decodes finite WAVE samples,
and matches the original whole-file SHA, rate, channels and frame count. A child
report or saved source name never grants permission to reopen a file.

The existing staging engine then freshly hashes the source, copies through the
held handle, flushes and reads back the owned staging file, and rechecks source
identity. The prepared owner retains admitted metadata and native staging
handles. Preparation creates only an exclusive UUID staging directory, with no
canonical asset publication. Failed or canceled operations leave inspectable
staging; destructors retire handles and credit without deleting files.

Publication is one attempt against the current owner-supplied project epoch,
revision, request, asset ID, canonical path and content identity. The caller must
serialize that current model and approved project-root ownership through the
call. Stale metadata refuses before creating canonical directories. The saved
path has at most 64 components of 255 UTF-8 bytes, below the existing 1024-byte
project limit. Missing parent directories may be created; existing parent
components must be plain directories. Linux uses contained openat2 lookups with
no links or crossed mounts; Windows uses relative native directory handles and
rejects reparse points. There is no pathname-opening fallback.

After the final cancellation/qualification boundary, publication rehashes and
flushes the held staged file. Linux uses renameat2 with RENAME_NOREPLACE and
Windows uses native handle-relative rename with replacement disabled. A target
file, directory or a racing name is never overwritten. As with existing staging,
the destination parent and its writers must be trusted: this API does not claim
to exclude hostile same-UID namespace/content writers during native I/O.

The atomic rename is the publication point. Later cancellation does not become
an unpublished failure. Directory-flush or observer failures after publication
return published=true with postCommitFlushFailed=true. Linux reports completed
file/directory flushes when supported; Windows reports file flush only. Published
media remains held until the prepared facade retires, so retire it before opening
the asset for playback on Windows. A stagedRelativePath is an inspection name for
preparation/failure, not a path authority or a post-publication media receipt.

Tests restore a previously valid saved project after simulated loss, compare
the complete file independently, and preserve clip/take IDs, edits, Undo/Redo and
save/reopen. They also cover mismatch, stale context, bad paths, native handle and
budget retirement, linked parents, existing/racing targets, cancellation,
non-finite samples, tampered staging and truthful post-publication outcomes.
Native Windows CI explicitly builds and runs the new corpus.

This API changes no Session and does not provide a durable restoration intent,
project command, recovery dialog or completed P084 workflow. Strict ProjectStore
media checks remain intact. Later integration must also handle recovery before a
missing-media project can pass strict Open, serialize publication with model and
autosave ownership, and qualify the complete desktop workflow. The frozen
Windows preview is unchanged.
