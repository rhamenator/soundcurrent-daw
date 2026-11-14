# Manifest-only parent process supervision

The shared Qt parent supervisor consumes the original private manifest source
request, source ticket, retained control and completed actual checker snapshot.
It reopens native project/source owners on the I/O thread, binds the original
control and source identity before launch, and holds these native pins through
actual child termination and reaping, including cancellation and error cleanup.
The fresh source path must exactly match the private checker selection's root
and leaf, report leaf and ledger. This binds the selected namespace metadata;
original preparation has no retained source inode capability. The source identity
stamp is acquired on this fresh reopening and held through actual child reaping.
Same-byte replacement at that same name before reopening is not distinguished.
The caller must serialize the actual empty controller owner and freeze competing
writers until return. This internal function does not establish that controller
ownership by itself.

A fresh operation uses the distinct manifest worker protocol, with no historical
operation field. A second strict verification after actual process start precedes
writing any request bytes or EOF. Direct cancellation/deadline checks also run
immediately before writing and before EOF. These checks are not an atomic permit
against a concurrent cancel store; conservative process facts and actual cleanup
still apply if cancellation races immediately after a check. Process observations retain actual PID, exit,
kill and reap facts even when validation fails. A failed launch creates no PID or
exit observation. Bounded request/reply framing, child admission credit, deadline
and cancel/close checks use the same trusted parent policy as existing staging.
The memory credit is admission accounting, not a child RSS sandbox.

Only after actual successful reaping does the parent independently inspect the
record, original manifest/canonical/source facts, intent and complete staged
bytes using its original dispatch binding. It never rebases the original audit
root stamp. Errors and late cancellation clear passive verification flags while
preserving actual process facts and conservative possible staging. Staged data
may survive a canceled request and requires later explicit inspection.

The focused regression cases use a real checker and real isolated native child,
with preparation native handles retired before parent dispatch. They exercise
success, malformed original control, checker PID mismatch, changed namespace or
source, prelaunch cancellation, cancel/close before input, late cancellation,
terminal PID/extent refusal, failed launch, admission refusal and deadline expiry.
Additional cases inject cancellation at the final input and EOF boundaries,
nonstandard exceptions after actual start, and same-byte files at different
names/directories or a missing checker selection, actual nonzero foreign-worker
protocol refusal, and duplicate/truncated terminal frames. Both standard and nonstandard
exceptions fall through actual child retirement before native owner destruction.
Native handle probes observe both project and source pins before kill and after
reap, then their retirement before return. Canonical media, saved manifest and
accepted model remain unchanged. Existing test deadlines and shard inventory
remain unchanged. Linux and native Windows execution are required qualification;
source preparation alone is not execution evidence.

This adds an internal parent foundation. The public serialized controller
command, installed helper delivery, publication permission, project adoption,
restart recovery and full recovery UI remain separate work and are not claimed.
