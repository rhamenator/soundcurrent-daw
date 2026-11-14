# Saved publication parent supervisor

The shared non-audio I/O layer exposes `runSavedPublicationProcess` separately
from the stage-only API. Both reuse the original saved-source preparation,
checker, admission, native pins, bounded drain, cooperative watchdog and actual
retirement machinery. The v1 route still closes stdin after the request, qualifies
only a reaped stage response, and cannot issue publication permission.

A v2 caller must freeze its actual empty controller owner and private preparation
context while the supervisor runs, and must use the supplied permission mutex for
cancel and close. No controller publication command or accepted Session is
created here. The supervisor captures its original dispatch before any child
stage mutation, retains original audit/source/root/proof owners outside exception
scopes through actual retirement, observes the actual child PID, and sends the
v2 request only after launch qualification.

Exactly one bounded READY is decoded against the original dispatch, current
captured owner and actual child PID. Permission encoding and callbacks happen
outside the mutex. Under the real shared cancel/close mutex, only atomic owner
flags are checked, permission ambiguity is marked before any write prefix can
reach the child, and the permit is queued followed by EOF. No clock callback or
process wait executes under that mutex. The parent continues bounded stdout/stderr
draining, deadline and cancel/close checks; every failure kills and actually reaps
the child before native pins or logical child credit retire.

After permission, a missing, malformed or unsuccessful terminal, diagnostic,
crash, broken pipe or empty stderr never establishes media absence. The result
preserves unknown publication and a reconciliation requirement. A valid terminal
separates media, receipt and durability facts; independent canonical hash and
receipt record checks run only after child writer handles retire. Fresh named-root
identity must still select the originally held root before and after verification.
These are bounded current observations under trusted serialized namespace writers,
not an atomic filesystem snapshot or adoption authority. Late cancel/close,
verification error or auxiliary retirement failure preserves observed publication
facts and keeps reconciliation conservative. A kill request never attributes a
raced child exit to that request.

Direct native supervisor qualification exercises success, prelaunch/source-chunk
and launch cancellation, cancel/close before and after permission, malformed READY
and terminal frames, deadline boundaries, missing executable, child admission
limits, late cancellation/retirement failures, foreign canonical preservation and
named-root replacement. Real child/checker execution and post-retirement bytes,
receipts and strict Open are checked on Linux and native Windows. Existing
stage-only tests and unchanged deadlines remain required.

Controller command admission, final control-thread ownership checks, a persistent
ambiguity latch/reconciliation interaction and explicit strict Open/adoption remain
following work. No GUI, installer or release is promoted by this supervisor. Full
processing, content, import, equipment, all-Europe localization, frozen product
reference, GPL/source and native release qualification gates remain in force.
