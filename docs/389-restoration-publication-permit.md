# Restoration publication permit

Preparation must not itself grant permission to publish. The bounded
`sc-restoration-permit-v1` control message binds publication permission to the
current accepted project/root, asset/content, operation UUID, epoch/revision,
request, checking job, restoration job, autosave token and observed child PID.
The parent first verifies its dispatch ticket and the child's Planned record,
including the checked whole-file byte extent. A Published record cannot stand
in for preparation evidence.

Encoding rechecks the CURRENT accepted owner. Decoding uses independently held
expected ticket/preparation metadata and the actual child PID, never echoed
owner fields as current state. It admits at most 16384 bytes of ASCII flat JSON;
Unicode path names use lowercase hex. Duplicate, missing, unknown, nested,
wrongly typed and mismatched fields refuse, including floating-point numeric
aliases. Parsing, output and retained-value banks are admitted to the ledger;
cancellation and failed admission retire their grants. This is declared bank
accounting, not an allocator/RSS sandbox guarantee.

A decoded `RestorationPublicationPermit` is move-only and one-use. Its publish
method rechecks scope and regenerates the expected message from the ACTUAL
private prepared owner, current accepted owner view and actual PID. Only an
exact match can reach the existing one-attempt physical publication path.
A stale or wrong-child permit touches no canonical media. Repeating the same
object refuses; decoding the same wire again cannot defeat the prepared
operation's one-attempt/exclusive-directory/no-overwrite rules or reconstitute
retired native handles.

Invalid permission consumes that permission object, while leaving unattempted
physical preparation inspectable. A new correctly bound permission may still
use that live prepared owner. Once physical publication runs, existing committed
truth, final receipt visibility and durability remain separate. Post-rename
cancellation completes the receipt attempt; later observer/audit/flush failure
cannot turn visible canonical media into an unpublished result.

This message belongs on a temporary trusted parent/child control channel. It
contains an ephemeral destination root, never source-root reopening authority,
and is not a durable restart credential. Admission of matching metadata does
not independently prove physical staging or canonical bytes. The child still
needs its approved native preparation capability; after abnormal termination,
the parent must freshly reconcile durable evidence and canonical content.

The caller MUST serialize the actual accepted model/root/I/O/inhibition owner
through child completion, not merely during message encoding. This core does
not implement that controller transaction, acquire an autosave token, frame an
IPC stream, spawn a process, or build a recovery dialog. Those P084 steps,
passive-manifest recovery before strict Open, and complete localized/native
workflow qualification remain open.

The existing Linux/native Windows restoration-owner corpus now tests every
permit field, strict schema/type/size rejection, stale/PID/scope/cancellation and
quota refusal, move/one-use admission, matching physical publication and
truthful post-publication cancellation/observer failures. No quality gate,
ProjectStore behavior, installed preview or frozen Windows producer changes.
