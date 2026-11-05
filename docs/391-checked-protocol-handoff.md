# Original checker protocol handoff

The checking report already retains its strict original protocol privately.
`WaveCheckReport::copyProtocol` now provides an independently owned copy for
worker request preparation, preserving exact ASCII bytes, including harmless
whitespace and line endings. It does not reconstruct JSON from parsed fields
or report echoed fields as newly observed process evidence.

The copy requires the report's original resource scope, a live non-moved owner,
and uncanceled work. A conservative bank is reserved before allocating bytes;
the returned `OwnedInspectionProtocol` retains actual string-capacity credit
and retires bytes before credit. Failed admission, foreign/sibling scope and
cancellation leave the source report intact. This declared-bank accounting is
not a hard allocator or RSS guarantee.

`copyWaveAssetReplacementProtocol` first applies the existing CURRENT accepted
project/asset/revision/epoch/request/job/PID and complete-success checks, then
copies those same retained bytes. The result can outlive the checking facade,
cleared snapshots and parsed report. It retains no native file handle and is
not source reopening authority. A child still freshly validates the selected
source as required by the restoration endpoint.

This is a request-preparation prerequisite. It does not freeze the live project,
acquire autosave/I/O ownership, supervise restoration, frame a request, admit a
permission, or reconcile canonical media after crash. The actual controller
owner must serialize the accepted project through child completion. A copy or
an old successful checker snapshot alone cannot grant publication permission.
The existing owner/lifecycle investigation and docs390 remain applicable.

The core corpus compares exact noncanonical-layout bytes rather than encoder
round trips, tests independent retirement, moved-from/foreign/sibling/quota/
cancellation rejection, and preserves the original report. The desktop corpus
uses an actual checker PID/reply, verifies stale/current context refusal and
retained bytes after facade retirement. Hosted native Windows execution remains
required; a cross-compiler syntax check alone does not qualify the workflow.
No installed preview, catalog, package, real-time callback or release changes.
