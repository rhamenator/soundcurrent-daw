# Private manifest preparation control owner

Successful manifest-only source preparation now retains both admitted frames:
the actual checker protocol and the distinct manifest source control protocol.
The latter is encoded from the original private ticket while its original
native audit is still pinned on the I/O worker. Encoding rechecks that audit,
the actual report/PID, memory scope and original source context before and
after serialization. The selected approved source is rechecked afterward.
Cancellation or encoding/admission failure prevents readiness.

Both frames stay private to the same preparation owner as the ticket, actual
checker snapshot and request. They are not added to the public completion or
snapshot. Refused commands preserve that owner; admitted replacement I/O and
shutdown consume it under the existing controller ordering. Frame storage is
charged and retires with its owner. Native handles still retire on the I/O
worker before readiness; the control thread retains metadata only.

This removes the need for a future dispatch to reconstruct its control frame
from a newly captured destination baseline. Dispatch must still acquire fresh
native audit/source pins and compare them against this original fingerprint,
retain those pins through actual launch/reap, and enforce current private owner,
finite-media, cancellation and staging/publication permission checks. No live
worker, staging or publication command is added here. Readiness and the frame
alone grant none of those capabilities. Historical operations remain separate.

Existing actual controller preparation cases exercise the production encoder
on successful selections and preserve the source/project bytes, refused-command
ownership, native handle retirement and shutdown credit retirement. The strict
control codec has separate adversarial actual-checker-backed coverage. Actual
Linux and native Windows qualification, exact-head reviews and original evidence
retention remain required before integration.
