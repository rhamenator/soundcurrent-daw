# Live saved restoration worker exchange

`sc-saved-restoration-live-worker` is a separate development I/O executable built
from the same shared worker source as the stage-only v1 executable. Its compile-time
v2 selection accepts `sc-saved-restoration-worker-v2` request framing. The v1
executable still accepts only its original protocol, requires EOF before any work,
and never publishes canonical media. Qualification fixture modes remain restricted
to their separate test target.

The v2 child reads one bounded request line, independently reconstructs the
original private source proof against its native saved audit and actual completed
checker report, and binds dispatch metadata before staging mutates the root. It
retains the same source, audit and typed live stage while emitting one bounded
READY line and awaiting permission. Its owner view describes the captured empty
worker context; the future parent supervisor must supply actual serialized
controller ownership and capture its original dispatch before launching work.
Decoded READY metadata cannot recreate owners after this child retires.

One bounded publication-permit line followed by EOF is required before the native
publication attempt. A duplicate or extra frame refuses before publication.
The strict permit binds original source proof, planned record, context, history,
fresh operation and actual native worker PID. The native layer reverifies original
root/file/source/stage facts and performs exclusive no-overwrite publication.
A terminal v2 result separates media visibility, receipt visibility and durability
failure. If an exception occurs after entering permission publication, the error
reports `publicationMayExist=true` and `published=null`; it does not claim the
canonical media stayed absent. Pre-entry failures report `published=false`.
Partial stage/history files are preserved for inspection.

Pipe waits are intentionally blocking non-audio work. A supervising parent must
bound framing and deadlines, drain stdout/stderr, serialize permission delivery
with the real cancel/close mutex, and kill and actually reap the child while
retaining all original native owners and admitted credit. A child error, killed
pipe or absent terminal after permission cannot resolve ambiguous publication.
Independent canonical verification after native child handle retirement and
explicit strict Open/adoption remain required. This slice qualifies the isolated
native child, not an enabled controller publication command or recovery UI.

Existing controller tests launch the real child and checker, preserve the original
parent dispatch, observe READY before closing stdin, and verify normal publication,
EOF, malformed/wrong-PID/wrong-phase/duplicate/truncated permission, wrong v1 request,
kill while waiting, and conservative ambiguity when an independently created
canonical file blocks the valid native attempt. Independent final bytes, receipts
and strict Open are read after actual retirement; no Windows pathname read through
live exclusive writer handles is claimed. Linux and actual native Windows checks,
full logs/archives, non-Copilot and available Claude review, installer/GPL/source,
processing/content/import/equipment/all-Europe localization and release gates
remain mandatory. No installer or release promotion follows from this executable.
