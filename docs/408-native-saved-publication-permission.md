# Native saved publication permission

The native I/O layer now has a typed dispatched saved stage and a bounded,
move-only, single-use publication permission. The parent independently captures
the current empty controller owner, original source proof, history/operation,
project/asset/content identity and actual checker/worker PIDs. Empty epoch and
revision zero remain valid; no accepted Session is fabricated.

`DispatchedSavedRestoration::prepare` verifies the original dispatch before the
same source/audit is moved into its live stage. After staging, the unchanged-root
stamp is no longer a valid test: the stage itself created an exclusive operation
directory. The permission instead binds captured owner metadata and an exact
planned saved record, while the child retains and revalidates the original native
stage transition, pinned/named historical facts, source and staged bytes.

The strict flat ASCII `sc-saved-restoration-permit-v1` frame contains the original
source control proof, planned record, history operation, root and independently
observed checker and worker PIDs.
Its 64 KiB frame and parser/value storage are admitted before allocation. Unknown,
duplicate, missing, nested, wrong-type, altered, oversized and non-ASCII input
refuses. A moved permit cannot publish. A presented permit is consumed before
validation. A pre-attempt refusal (stale owner, PID, quota or cancellation) burns
that permit but leaves the live stage unattempted only when refusal occurs in
the outer permit validation. Once DispatchedSavedRestoration::publish is entered,
the stage is spent, including an inner verification, quota or cancellation refusal
before publication writes. A fresh permission may enter a still-unattempted stage
once. The v2 parent must track these boundaries separately: an error alone proves
neither stage availability nor media visibility. Native publication
checks the process's real OS PID again; historical record fields are not authority.

The original complete root transition is checked before any publication directory
creation. Immediately before rename, original pinned/named files and source are
rechecked, and a freshly opened named root must still identify the original held
native root object. The owned stage directory/intent/media names and every
canonical destination parent are checked against their held native objects again
after the final pre-rename observer yields, after full hashing and fsync; no
full-file hash or fsync remains between these final native name checks and rename.
Staged media and intent size/identity/link-count/write/change timestamps are
captured before verification, compared after hashing/flushing, and checked again
after the last observer,
checking same-inode, same-length edits without adding another hash. Metadata is
best effort at native timestamp granularity, never a replacement for the content
hash or trusted serialized-writer contract. Timestamp-mutation fixtures separate
events before their intentional edit. Pending receipt bytes are also verified
again after the postcommit observer before final receipt publication. Verification
opens never create names. Freshly created nested canonical paths and
intermediate-parent swaps are covered alongside existing-directory cases.
The native staging layer performs exclusive no-overwrite
rename and separates media visibility, receipt visibility and postcommit durability
failure. Cancellation after media visibility cannot report that publication did
not occur. Partial files and ambiguous outcomes are preserved for inspection.
As elsewhere, roots and namespace writers must be trusted and serialized; this is
not an atomic namespace snapshot against hostile same-user writers. Native I/O is
cooperatively cancellable, not forcibly preemptible inside a kernel call.

This slice exposes an engine I/O API, not a controller command or recovery UI.
The stage-only v1 executable keeps EOF-before-work and has no publication route.
A separate bounded v2 READY/permit process exchange, real parent cancel/close mutex,
independent canonical verification and explicit strict-Open/adoption remain the
next controller work. A saved record or exited stage DTO cannot recreate live
owners or this permission. Installer, GPL/source, processing, import, equipment,
all-Europe localization and full product gates remain independent requirements.
